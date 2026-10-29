#include "monitor.h"
#include "time_task.h"
#include "experiment.h"

#if CONFIG_M2_MONITOR_ENABLE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#define LOG_CAPACITY 32
#define EVENT_LOG_CAPACITY 32
#define TX_CAPACITY 16
#define FRAME_SIZE 1400
#define MAX_COMMANDS 8U
#define MAX_LOGS 16U
#define WORK_BUDGET_US 20000LL
#define RX_BUDGET_US 5000LL
#define BACKGROUND_CAPACITY (TX_CAPACITY - MAX_COMMANDS)
#define EVENT_TX_CAPACITY (TX_CAPACITY - 4U)
#if CONFIG_M2_MONITOR_TCP
#define PROTOCOL "TCP"
#define PC_SOURCE "PC_TCP"
#else
#define PROTOCOL "UDP"
#define PC_SOURCE "PC_UDP"
#endif

static const char *TAG = "MONITOR";
static QueueHandle_t s_jobs;
static QueueHandle_t s_event_jobs;
static QueueHandle_t s_releases;
static monitor_dispatch_t s_dispatch;
static monitor_cycle_t s_record;
static bool s_online;
static bool s_reconnect;
static uint32_t s_network_epoch;
static uint32_t s_log_dropped;
static uint32_t s_release_dropped;
static uint32_t s_tx_dropped;
static uint32_t s_bad_frames;
static uint32_t s_sent;
static uint32_t s_received;
static uint32_t s_ack_dropped;
static uint32_t s_event_log_dropped;
static uint32_t s_stream;
static int s_server = -1;
static int s_client = -1;
static bool s_peer_valid;
#if !CONFIG_M2_MONITOR_TCP
static struct sockaddr_in s_peer;
#endif
static uint32_t s_session;
static uint32_t s_last_sequence;
static monitor_command_t s_last_command;
static const char *s_last_status;
static monitor_line_t s_line;
#if CONFIG_M2_MONITOR_TCP
static unsigned char s_rx[256];
#endif
static size_t s_rx_pos, s_rx_size;
static int64_t s_timer_origin;
static uint64_t s_release_number;
static uint64_t s_last_time_cycle = UINT64_MAX;
static int64_t s_socket_received_us;
static unsigned s_report_next = EXP_TASKS;
static uint32_t s_report_sequence;
static bool s_config_pending;

typedef enum { TX_BACKGROUND, TX_EVENT, TX_CONTROL } tx_priority_t;
typedef struct { char data[FRAME_SIZE]; size_t size, base_size, offset; tx_priority_t priority; } tx_frame_t;
static tx_frame_t s_tx[TX_CAPACITY];
static size_t s_tx_head, s_tx_count;

static bool would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK; }
static bool nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool peer_ready(void) {
#if CONFIG_M2_MONITOR_TCP
    return s_client >= 0 && s_session != 0;
#else
    return s_peer_valid && s_session != 0;
#endif
}

static void clear_peer(void) {
    if (s_client >= 0) close(s_client);
    s_client = -1;
    s_peer_valid = false;
    s_session = 0;
    s_report_next = EXP_TASKS;
    s_last_time_cycle = UINT64_MAX;
    s_last_sequence = 0;
    s_last_status = NULL;
    s_line = (monitor_line_t){0};
    s_rx_pos = s_rx_size = 0;
    s_tx_dropped += (uint32_t)s_tx_count;
    s_tx_head = s_tx_count = 0;
}

static void close_network(void) {
    clear_peer();
    if (s_server >= 0) close(s_server);
    s_server = -1;
}

static bool queue_frame_v(tx_priority_t priority, const char *format, va_list args) {
    size_t capacity = priority == TX_CONTROL ? TX_CAPACITY :
                      priority == TX_EVENT ? EVENT_TX_CAPACITY : BACKGROUND_CAPACITY;
    if (s_tx_count >= capacity) {
        ++s_tx_dropped;
        return false;
    }
    tx_frame_t frame = {.priority = priority};
    int length = vsnprintf(frame.data, sizeof(frame.data), format, args);
    if (length <= 0 || (size_t)length + 170 >= sizeof(frame.data)) {
        ++s_tx_dropped;
        return false;
    }
    // Reserva espaco para timestamps e tamanho adicionados na chamada send.
    frame.base_size = frame.size = (size_t)length;
    size_t position = s_tx_count;
    if (priority != TX_BACKGROUND) {
        // Controle > eventos NAV/FS > amostras. Nao intercalar um JSON TCP parcial.
        position = s_tx_count && s_tx[s_tx_head].offset ? 1U : 0U;
        while (position < s_tx_count && s_tx[(s_tx_head + position) % TX_CAPACITY].priority >= priority)
            ++position;
        for (size_t i = s_tx_count; i > position; --i)
            s_tx[(s_tx_head + i) % TX_CAPACITY] = s_tx[(s_tx_head + i - 1U) % TX_CAPACITY];
    }
    s_tx[(s_tx_head + position) % TX_CAPACITY] = frame;
    ++s_tx_count;
    return true;
}

static bool queue_priority(tx_priority_t priority, const char *format, ...) {
    va_list args;
    va_start(args, format);
    bool queued = queue_frame_v(priority, format, args);
    va_end(args);
    return queued;
}

static void queue_frame(const char *format, ...) {
    va_list args;
    va_start(args, format);
    queue_frame_v(TX_BACKGROUND, format, args);
    va_end(args);
}

static void queue_ack(const char *format, ...) {
    va_list args;
    va_start(args, format);
    if (!queue_frame_v(TX_CONTROL, format, args)) ++s_ack_dropped;
    va_end(args);
}

// Nenhum send bloqueante. TCP conserva o deslocamento de envios parciais;
// UDP conserva o datagrama inteiro se o socket estiver temporariamente ocupado.
static void flush_frames(int64_t deadline) {
    for (unsigned attempt = 0; s_tx_count && attempt < 32 &&
         esp_timer_get_time() < deadline; ++attempt) {
        tx_frame_t *frame = &s_tx[s_tx_head];
        if (frame->offset == 0) {
            time_snapshot_t clock;
            time_task_snapshot(&clock);
            char utc[24] = "null";
            if (clock.synced) snprintf(utc, sizeof(utc), "%" PRId64, clock.unix_ms);
            size_t prefix = frame->base_size - 2; // Remove }\n para anexar metadados.
            const char *format = ",\"tx_mono_us\":%" PRId64 ",\"tx_unix_ms\":%s,\"tx_sync_count\":%" PRIu32 ",\"tx_bytes\":%5u}\n";
            int extra = snprintf(frame->data + prefix, FRAME_SIZE - prefix,
                                 format, clock.mono_us, utc, clock.sync_count, 0U);
            frame->size = prefix + (size_t)extra;
            snprintf(frame->data + prefix, FRAME_SIZE - prefix,
                     format, clock.mono_us, utc, clock.sync_count, (unsigned)frame->size);
        }
#if CONFIG_M2_MONITOR_TCP
        int sent = send(s_client, frame->data + frame->offset,
                        frame->size - frame->offset, 0);
#else
        int sent = sendto(s_server, frame->data, frame->size, 0,
                          (struct sockaddr *)&s_peer, sizeof(s_peer));
#endif
        if (sent < 0 && would_block()) return;
        if (sent <= 0) {
#if CONFIG_M2_MONITOR_TCP
            clear_peer();
#else
            ++s_tx_dropped;
            s_tx_head = (s_tx_head + 1U) % TX_CAPACITY;
            --s_tx_count;
#endif
            return;
        }
        frame->offset += (size_t)sent;
        if (frame->offset == frame->size) {
            ++s_sent;
            s_tx_head = (s_tx_head + 1U) % TX_CAPACITY;
            --s_tx_count;
        }
    }
}

static void acknowledge(const monitor_request_t *request, const char *status) {
    time_snapshot_t clock;
    time_task_snapshot(&clock);
    char utc[24] = "null";
    char rx_utc[24] = "null";
    if (clock.synced) snprintf(utc, sizeof(utc), "%" PRId64, clock.unix_ms);
    if (clock.synced && s_socket_received_us >= clock.last_sync_mono_us)
        snprintf(rx_utc, sizeof(rx_utc), "%" PRId64,
                 (clock.unix_ms * 1000 - clock.mono_us + s_socket_received_us) / 1000);
    queue_ack("{\"type\":\"ack\",\"protocol\":\"%s\",\"session\":%" PRIu32
                ",\"seq\":%" PRIu32 ",\"command\":\"%s\",\"status\":\"%s\","
                "\"rx_us\":%" PRId64 ",\"queued_us\":%" PRId64
                ",\"queued_unix_ms\":%s,\"time_sync_count\":%" PRIu32
                ",\"socket_rx_us\":%" PRId64 ",\"socket_rx_unix_ms\":%s}\n",
                PROTOCOL, request->message.session, request->message.sequence,
                monitor_command_name(request->message.command), status,
                request->received_us, clock.mono_us, utc, clock.sync_count,
                s_socket_received_us, rx_utc);
}

static void handle_message(const monitor_message_t *message, int64_t received_us) {
    const monitor_request_t request = {.message = *message,
        .received_us = received_us, .source = PC_SOURCE};
    if (message->session != s_session) {
        if (message->command != MON_HELLO) return;
        // Uma nova sessao nao reutiliza comandos/ACKs da sessao anterior.
        s_tx_dropped += (uint32_t)s_tx_count;
        s_tx_head = s_tx_count = 0;
        s_session = message->session;
        s_last_time_cycle = UINT64_MAX;
        s_last_sequence = 0;
    }
    ++s_received;
    if (message->sequence == s_last_sequence) {
        acknowledge(&request, message->command == s_last_command ?
                    s_last_status : "SEQ_CONFLICT");
        return;
    }
    if (message->sequence < s_last_sequence) {
        acknowledge(&request, "STALE");
        return;
    }
    const char *status = "OK";
    if (message->command == MON_HELLO) s_config_pending = true;
    if (message->command == MON_REPORT) {
        s_report_next = 0;
        s_report_sequence = message->sequence;
        s_config_pending = true;
    }
    if (message->command == MON_BEGIN) {
        time_snapshot_t clock;
        time_task_snapshot(&clock);
        if (clock.enabled && !clock.synced) status = "WAIT_SYNC";
        else status = s_dispatch(&request) == ESP_OK ? "STARTED" : "ALREADY_STARTED";
    }
    if (message->command == MON_NAV || message->command == MON_FS) {
        esp_err_t result = s_dispatch(&request);
        status = result == ESP_OK ? "QUEUED" :
                 result == ESP_ERR_INVALID_STATE ? "SAFE_ACTIVE" : "BUSY";
    }
    s_last_sequence = message->sequence;
    s_last_command = message->command;
    s_last_status = status;
    acknowledge(&request, status);
}

static void receive_commands(int64_t deadline) {
#if CONFIG_M2_MONITOR_TCP
    if (s_client < 0) {
        int client = accept(s_server, NULL, NULL);
        if (client < 0) return;
        if (!nonblocking(client)) { close(client); return; }
        clear_peer();
        s_client = client;
        ESP_LOGI(TAG, "Cliente TCP conectado; aguardando HELLO");
    }
    unsigned commands = 0;
    unsigned reads = 0;
    while (commands < MAX_COMMANDS && s_tx_count < TX_CAPACITY && esp_timer_get_time() < deadline) {
        if (s_rx_pos == s_rx_size) {
            if (++reads > 8) break;
            int received = recv(s_client, s_rx, sizeof(s_rx), 0);
            s_socket_received_us = esp_timer_get_time();
            if (received < 0 && would_block()) break;
            if (received <= 0) { clear_peer(); break; }
            s_rx_pos = 0;
            s_rx_size = (size_t)received;
        }
        int ready = monitor_line_feed(&s_line, (char)s_rx[s_rx_pos++]);
        if (ready != 0) {
            monitor_message_t message;
            ++commands;
            if (ready > 0 && monitor_parse_command(s_line.data, s_line.used, &message))
                handle_message(&message, esp_timer_get_time());
            else ++s_bad_frames;
            s_line.used = 0;
        }
    }
#else
    for (unsigned i = 0; i < MAX_COMMANDS && s_tx_count < TX_CAPACITY && esp_timer_get_time() < deadline; ++i) {
        char data[MONITOR_COMMAND_MAX + 1U];
        struct sockaddr_in sender;
        socklen_t address_size = sizeof(sender);
        int received = recvfrom(s_server, data, sizeof(data), 0,
                                (struct sockaddr *)&sender, &address_size);
        s_socket_received_us = esp_timer_get_time();
        if (received < 0) break;
        if (received <= 0 || received >= (int)MONITOR_COMMAND_MAX) {
            ++s_bad_frames;
            continue;
        }
        size_t length = (size_t)received;
        if (data[length - 1U] == '\n') --length;
        monitor_message_t message;
        if (!monitor_parse_command(data, length, &message)) { ++s_bad_frames; continue; }
        bool same_peer = s_peer_valid && sender.sin_addr.s_addr == s_peer.sin_addr.s_addr &&
                         sender.sin_port == s_peer.sin_port;
        if (!same_peer) {
            if (message.command != MON_HELLO) continue;
            clear_peer();
            s_peer = sender;
            s_peer_valid = true;
        }
        handle_message(&message, esp_timer_get_time());
    }
#endif
}

static void create_server(void) {
#if CONFIG_M2_MONITOR_TCP
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#endif
    if (fd < 0) return;
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {.sin_family = AF_INET,
        .sin_port = htons(CONFIG_M2_MONITOR_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (!nonblocking(fd) || bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd); return;
    }
#if CONFIG_M2_MONITOR_TCP
    if (listen(fd, 1) < 0) { close(fd); return; }
#endif
    s_server = fd;
    ESP_LOGI(TAG, "Servidor %s na porta %d", PROTOCOL, CONFIG_M2_MONITOR_PORT);
}

static void wifi_event(void *context, esp_event_base_t base, int32_t id, void *data) {
    (void)context;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        __atomic_store_n(&s_reconnect, true, __ATOMIC_RELEASE);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        __atomic_store_n(&s_online, false, __ATOMIC_RELEASE);
        __atomic_fetch_add(&s_network_epoch, 1U, __ATOMIC_RELEASE);
        __atomic_store_n(&s_reconnect, true, __ATOMIC_RELEASE);
        ESP_LOGW(TAG, "Wi-Fi desconectado; reconexao sera tentada");
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        __atomic_store_n(&s_online, true, __ATOMIC_RELEASE);
        __atomic_fetch_add(&s_network_epoch, 1U, __ATOMIC_RELEASE);
        __atomic_store_n(&s_reconnect, false, __ATOMIC_RELEASE);
        printf("MONITOR_IP | " IPSTR " | %s | porta=%d\n",
               IP2STR(&event->ip_info.ip), PROTOCOL, CONFIG_M2_MONITOR_PORT);
    }
}

static void initialize_wifi(void) {
    if (CONFIG_M2_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "SSID vazio: configure M2 no menuconfig; controle do drone continua");
        return;
    }
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    configASSERT(esp_netif_create_default_wifi_sta() != NULL);
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    // Credenciais ficam apenas no sdkconfig local, nunca sao impressas.
    wifi_config_t config = {0};
    _Static_assert(sizeof(CONFIG_M2_WIFI_SSID) - 1 <= sizeof(config.sta.ssid), "SSID longo");
    _Static_assert(sizeof(CONFIG_M2_WIFI_PASSWORD) - 1 <= sizeof(config.sta.password), "Senha longa");
    memcpy(config.sta.ssid, CONFIG_M2_WIFI_SSID, sizeof(CONFIG_M2_WIFI_SSID) - 1);
    memcpy(config.sta.password, CONFIG_M2_WIFI_PASSWORD, sizeof(CONFIG_M2_WIFI_PASSWORD) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}

static void release_cycle(void *context) {
    (void)context;
    // Timer de software libera a tarefa; nenhum socket ou printf no callback.
    int64_t planned = s_timer_origin + (int64_t)(++s_release_number) * MONITOR_PERIOD_US;
    if (xQueueSend(s_releases, &planned, 0) != pdTRUE)
    {
        __atomic_fetch_add(&s_release_dropped, 1U, __ATOMIC_RELAXED);
        experiment_lost(4, planned);
    }
}

void monitor_publish_job(const monitor_job_t *job) {
    if (s_jobs == NULL) return;
    if ((!strcmp(job->task, "FUS_IMU") || !strcmp(job->task, "CTRL_ATT")) &&
        job->job % CONFIG_M2_MONITOR_SAMPLE_N != 0) return;
    monitor_job_t stamped = *job;
    time_snapshot_t clock;
    time_task_snapshot(&clock);
    stamped.end_unix_ms = -1;
    stamped.time_sync_count = 0;
    if (clock.synced && job->end_us >= clock.last_sync_mono_us) {
        stamped.end_unix_ms = (clock.unix_ms * 1000 - (clock.mono_us - job->end_us)) / 1000;
        stamped.time_sync_count = clock.sync_count;
    }
    bool event = !strcmp(job->task, "NAV_PLAN") || !strcmp(job->task, "FAIL_SAFE");
    if (xQueueSend(event ? s_event_jobs : s_jobs, &stamped, 0) != pdTRUE) {
        __atomic_fetch_add(&s_log_dropped, 1U, __ATOMIC_RELAXED);
        if (event) __atomic_fetch_add(&s_event_log_dropped, 1U, __ATOMIC_RELAXED);
    }
}

static void transmit_logs_from(QueueHandle_t queue, tx_priority_t priority,
                               unsigned limit, int64_t deadline) {
    for (unsigned i = 0; i < limit && esp_timer_get_time() < deadline; ++i) {
        monitor_job_t job;
        size_t capacity = priority == TX_EVENT ? EVENT_TX_CAPACITY : BACKGROUND_CAPACITY;
        if (s_tx_count >= capacity || xQueuePeek(queue, &job, 0) != pdTRUE) break;
        char utc[24] = "null";
        if (job.end_unix_ms >= 0) snprintf(utc, sizeof(utc), "%" PRId64, job.end_unix_ms);
        bool queued = queue_priority(priority, "{\"type\":\"job\",\"protocol\":\"%s\",\"stream\":%" PRIu32
                    ",\"task\":\"%s\",\"source\":\"%s\",\"job\":%" PRIu64
                    ",\"event_us\":%" PRId64 ",\"start_us\":%" PRId64 ",\"end_us\":%" PRId64
                    ",\"miss\":%s,\"session\":%" PRIu32 ",\"command_seq\":%" PRIu32
                    ",\"end_unix_ms\":%s,\"time_sync_count\":%" PRIu32
                    ",\"safe_us\":%" PRId64 ",\"sample_sequence\":%" PRIu32 "}\n",
                    PROTOCOL, ++s_stream, job.task, job.source, job.job,
                    job.event_us, job.start_us, job.end_us, job.miss ? "true" : "false",
                    job.session, job.command_sequence, utc, job.time_sync_count,
                    job.safe_us, job.sample_sequence);
        if (!queued) break;
        xQueueReceive(queue, &job, 0);
    }
}

static void transmit_time(const time_snapshot_t *clock) {
    if (!clock->enabled || clock->cycle == s_last_time_cycle) return;
    char utc[24] = "null";
    if (clock->synced) snprintf(utc, sizeof(utc), "%" PRId64, clock->unix_ms);
    queue_frame("{\"type\":\"time\",\"protocol\":\"%s\",\"cycle\":%" PRIu64
                ",\"time_synced\":%s,\"unix_ms\":%s,\"mono_us\":%" PRId64
                ",\"sync_count\":%" PRIu32 ",\"cycles_since_sync\":%" PRId64
                ",\"sync_age_ms\":%" PRId64 ",\"release_dropped\":%" PRIu32 "}\n",
                PROTOCOL, clock->cycle, clock->synced ? "true" : "false", utc,
                clock->mono_us, clock->sync_count, clock->cycles_since_sync,
                clock->sync_age_ms, clock->release_dropped);
    s_last_time_cycle = clock->cycle;
}

bool monitor_network_state(uint32_t *epoch) {
    *epoch = __atomic_load_n(&s_network_epoch, __ATOMIC_ACQUIRE);
    return __atomic_load_n(&s_online, __ATOMIC_ACQUIRE);
}

static void transmit_report(int64_t deadline) {
    bool wait_pc = false;
#if CONFIG_M2_AUTO_WAIT_PC
    wait_pc = true;
#endif
    unsigned sntp_interval = 0;
#if CONFIG_M2_TIME_ENABLE
    sntp_interval = CONFIG_M2_SNTP_INTERVAL_S;
#endif
    int64_t begin, end;
    experiment_bounds(&begin, &end);
    if (s_config_pending && s_tx_count < TX_CAPACITY) {
#if CONFIG_M2_INPUT_AUTO
        const char *input = "AUTO";
#elif CONFIG_M2_INPUT_TOUCH
        const char *input = "TOUCH";
#else
        const char *input = "PC";
#endif
        time_snapshot_t clock;
        time_task_snapshot(&clock);
        wifi_ap_record_t ap = {0};
        esp_err_t ap_result = esp_wifi_sta_get_ap_info(&ap);
        bool queued = queue_priority(TX_CONTROL, "{\"type\":\"config\",\"revision\":\"M2-METRICS-4\",\"protocol\":\"%s\","
                    "\"policy\":%d,\"cpu_mhz\":%d,\"preemption\":%d,\"slicing\":%d,"
                    "\"input\":\"%s\",\"sample_n\":%d,\"time_enabled\":%s,\"duration_s\":%d,"
                    "\"auto_wait_pc\":%s,\"tick_hz\":%d,\"sntp_interval_s\":%u,"
                    "\"wifi_rssi_dbm\":%d,\"wifi_channel\":%u,"
                    "\"run_start_us\":%" PRId64 ",\"run_end_us\":%" PRId64 "}\n",
                    PROTOCOL, CONFIG_M2_PRIORITY_POLICY, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
                    configUSE_PREEMPTION, configUSE_TIME_SLICING, input,
                    CONFIG_M2_MONITOR_SAMPLE_N, clock.enabled ? "true" : "false",
                    CONFIG_M2_RUN_SECONDS, wait_pc ? "true" : "false", CONFIG_FREERTOS_HZ,
                    sntp_interval, ap_result == ESP_OK ? ap.rssi : 0,
                    ap_result == ESP_OK ? ap.primary : 0, begin, end);
        if (queued) s_config_pending = false;
    }
    for (unsigned n = 0; n < 2 && s_report_next < EXP_TASKS &&
         s_tx_count < TX_CAPACITY && esp_timer_get_time() < deadline; ++n) {
        experiment_snapshot_t a;
        experiment_snapshot(s_report_next, &a);
        bool queued = queue_priority(TX_CONTROL, "{\"type\":\"analysis\",\"report_seq\":%" PRIu32 ",\"task\":\"%s\","
            "\"run_start_us\":%" PRId64 ",\"run_end_us\":%" PRId64
            ",\"jobs\":%" PRIu32 ",\"misses\":%" PRIu32 ",\"lost\":%" PRIu32 ",\"invalid\":%" PRIu32
            ",\"exec_sum_us\":%" PRIu64 ",\"exec_max_us\":%" PRIu32 ",\"wcrt_us\":%" PRIu32
            ",\"latency_max_us\":%" PRIu32 ",\"safe_max_us\":%" PRIu32
            ",\"jitter_sum_us\":%" PRIu64 ",\"jitter_max_us\":%" PRIu32
            ",\"isr_jobs\":%" PRIu32 ",\"isr_latency_max_us\":%" PRIu32 ",\"isr_safe_max_us\":%" PRIu32
            ",\"p99_low_us\":%" PRIu32 ",\"p99_high_us\":%" PRIu32 ",\"hist_overflow\":%" PRIu32
            ",\"m\":9,\"k\":10,\"windows\":%" PRIu32 ",\"violations\":%" PRIu32 ",\"min_hits\":%" PRIu32
            ",\"event_us\":%" PRId64 ",\"start_us\":%" PRId64 ",\"end_us\":%" PRId64 "}\n",
            s_report_sequence, experiment_task_name(s_report_next), a.run_start_us, a.run_end_us,
            a.jobs, a.misses, a.lost, a.invalid, a.exec_sum_us, a.exec_max_us, a.response_max_us,
            a.latency_max_us, a.safe_max_us, a.jitter_sum_us, a.jitter_max_us,
            a.isr_jobs, a.isr_latency_max_us, a.isr_safe_max_us,
            a.p99_low_us, a.p99_high_us, a.histogram_overflow,
            a.windows, a.violations, a.min_hits, a.last_event_us, a.last_start_us, a.last_end_us);
        if (!queued) break;
        ++s_report_next;
    }
}

static void task_monitor(void *context) {
    (void)context;
    printf("MONITOR_CONFIG | protocolo=%s | periodo_us=%lld | deadline_us=%lld | "
           "prioridade=1 | sample_n=%d | core=0\n",
           PROTOCOL, (long long)MONITOR_PERIOD_US, (long long)MONITOR_PERIOD_US,
           CONFIG_M2_MONITOR_SAMPLE_N);
    initialize_wifi(); // Inicializacao separada dos jobs periodicos.
    esp_timer_handle_t timer;
    const esp_timer_create_args_t args = {.callback = release_cycle, .name = "MON_RELEASE"};
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    s_timer_origin = esp_timer_get_time();
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, MONITOR_PERIOD_US));
    uint32_t cycles = 0;
    uint64_t misses = 0;
    uint64_t response_max = 0;
    int64_t last_connect = -5000000LL;
    uint32_t network_epoch = 0;
    for (;;) {
        int64_t event_us;
        xQueueReceive(s_releases, &event_us, portMAX_DELAY);
        int64_t start_us = esp_timer_get_time();
        int64_t deadline = start_us + WORK_BUDGET_US;
        uint32_t epoch = __atomic_load_n(&s_network_epoch, __ATOMIC_ACQUIRE);
        if (epoch != network_epoch) {
            close_network();
            network_epoch = epoch;
        }
        if (__atomic_load_n(&s_reconnect, __ATOMIC_ACQUIRE) &&
            start_us - last_connect >= 5000000LL) {
            __atomic_store_n(&s_reconnect, false, __ATOMIC_RELEASE);
            esp_err_t result = esp_wifi_connect();
            if (result != ESP_OK)
                __atomic_store_n(&s_reconnect, true, __ATOMIC_RELEASE);
            last_connect = start_us;
        }
        if (!__atomic_load_n(&s_online, __ATOMIC_ACQUIRE)) {
            if (s_server >= 0) close_network();
        } else {
            if (s_server < 0) create_server();
            if (s_server >= 0) {
                // RX tem uma oportunidade antes dos envios, mesmo com backlog.
                receive_commands(esp_timer_get_time() + RX_BUDGET_US);
                if (peer_ready()) {
                    time_snapshot_t clock;
                    time_task_snapshot(&clock);
                    transmit_report(deadline);
                    transmit_logs_from(s_event_jobs, TX_EVENT, 4U, deadline);
                    queue_frame("{\"type\":\"status\",\"protocol\":\"%s\",\"cycle\":%" PRIu32
                                ",\"mono_us\":%" PRId64 ",\"time_synced\":%s,\"sample_n\":%d,"
                                "\"log_dropped\":%" PRIu32 ",\"tx_dropped\":%" PRIu32
                                ",\"release_dropped\":%" PRIu32 ",\"bad_frames\":%" PRIu32
                                ",\"rx_commands\":%" PRIu32 ",\"ack_dropped\":%" PRIu32
                                ",\"tx_pending\":%u,\"event_log_dropped\":%" PRIu32 "}\n",
                                PROTOCOL, cycles + 1U, clock.mono_us, clock.synced ? "true" : "false",
                                CONFIG_M2_MONITOR_SAMPLE_N,
                                __atomic_load_n(&s_log_dropped, __ATOMIC_RELAXED), s_tx_dropped,
                                __atomic_load_n(&s_release_dropped, __ATOMIC_RELAXED), s_bad_frames,
                                s_received, s_ack_dropped, (unsigned)s_tx_count,
                                __atomic_load_n(&s_event_log_dropped, __ATOMIC_RELAXED));
                    transmit_time(&clock);
                    transmit_logs_from(s_jobs, TX_BACKGROUND, MAX_LOGS, deadline);
                    flush_frames(deadline);
                }
            }
        }
        int64_t end_us = esp_timer_get_time();
        s_record(event_us, start_us, end_us);
        uint64_t response = (uint64_t)(end_us - event_us);
        misses += response > MONITOR_PERIOD_US;
        if (response > response_max) response_max = response;
        if (++cycles % 10U == 0)
            printf("MONITOR_METRIC;%s;jobs=%" PRIu32 ";misses=%" PRIu64
                   ";rmax_us=%" PRIu64 ";evento_us=%" PRId64 ";inicio_us=%" PRId64
                   ";fim_us=%" PRId64 ";log_dropped=%" PRIu32 ";tx_dropped=%" PRIu32
                   ";release_dropped=%" PRIu32 ";rx=%" PRIu32 ";tx=%" PRIu32 "\n",
                   PROTOCOL, cycles, misses, response_max, event_us, start_us, end_us,
                   __atomic_load_n(&s_log_dropped, __ATOMIC_RELAXED), s_tx_dropped,
                   __atomic_load_n(&s_release_dropped, __ATOMIC_RELAXED), s_received, s_sent);
    }
}

esp_err_t monitor_start(monitor_dispatch_t dispatch, monitor_cycle_t record,
                        TaskHandle_t *handle) {
    if (s_jobs != NULL || dispatch == NULL || record == NULL) return ESP_ERR_INVALID_STATE;
    s_dispatch = dispatch;
    s_record = record;
    s_jobs = xQueueCreate(LOG_CAPACITY, sizeof(monitor_job_t));
    s_event_jobs = xQueueCreate(EVENT_LOG_CAPACITY, sizeof(monitor_job_t));
    s_releases = xQueueCreate(4, sizeof(int64_t));
    if (!s_jobs || !s_event_jobs || !s_releases) return ESP_ERR_NO_MEM;
    return xTaskCreatePinnedToCore(task_monitor, "MONITOR_TASK", 10240, NULL, 1, handle, 0)
           == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
#else
bool monitor_network_state(uint32_t *epoch) { *epoch = 0; return false; }
void monitor_publish_job(const monitor_job_t *job) { (void)job; }
esp_err_t monitor_start(monitor_dispatch_t dispatch, monitor_cycle_t record,
                        TaskHandle_t *handle) {
    (void)dispatch; (void)record; (void)handle;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

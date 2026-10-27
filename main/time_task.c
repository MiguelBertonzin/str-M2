#include "time_task.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "experiment.h"

#if CONFIG_M2_TIME_ENABLE
#include <inttypes.h>
#include <stdio.h>
#include <time.h>
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "freertos/queue.h"
#include "monitor.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_releases;
static time_cycle_record_t s_record;
static uint64_t s_cycle, s_sync_cycle, s_release_number;
static uint32_t s_sync_count, s_release_dropped;
static int64_t s_sync_mono_us, s_sync_unix_us, s_timer_origin;

// Callback do lwIP: apenas captura a atualizacao, sem printf ou espera de rede.
static void synchronized(struct timeval *tv) {
    portENTER_CRITICAL(&s_lock);
    s_sync_mono_us = esp_timer_get_time();
    s_sync_unix_us = (int64_t)tv->tv_sec * 1000000LL + tv->tv_usec;
    s_sync_cycle = s_cycle;
    ++s_sync_count;
    portEXIT_CRITICAL(&s_lock);
}

void time_task_snapshot(time_snapshot_t *snapshot) {
    portENTER_CRITICAL(&s_lock);
    const int64_t now = esp_timer_get_time();
    const bool synced = s_sync_count != 0;
    *snapshot = (time_snapshot_t){
        .enabled = true, .synced = synced, .sync_count = s_sync_count,
        .cycle = s_cycle,
        .cycles_since_sync = synced ? (int64_t)(s_cycle - s_sync_cycle) : -1,
        .mono_us = now,
        // Hora estimada a partir da ultima amostra SNTP + tempo monotono.
        // Nao inventa uma data para eventos anteriores a primeira sincronizacao.
        .unix_ms = synced ? (s_sync_unix_us + now - s_sync_mono_us) / 1000 : -1,
        .sync_age_ms = synced ? (now - s_sync_mono_us) / 1000 : -1,
        .last_sync_mono_us = synced ? s_sync_mono_us : -1,
        .release_dropped = s_release_dropped,
    };
    portEXIT_CRITICAL(&s_lock);
}

static void release_cycle(void *context) {
    (void)context;
    const int64_t event_us = s_timer_origin + (int64_t)(++s_release_number) * TIME_TASK_PERIOD_US;
    if (xQueueSend(s_releases, &event_us, 0) != pdTRUE) {
        experiment_lost(5, event_us);
        portENTER_CRITICAL(&s_lock);
        ++s_release_dropped;
        portEXIT_CRITICAL(&s_lock);
    }
}

static void task_time(void *context) {
    (void)context;
    printf("TIME_CONFIG | periodo_us=%lld | deadline_us=%lld | prioridade=1 | "
           "servidor=%s | sntp_intervalo_s=%d | UTC\n",
           TIME_TASK_PERIOD_US, TIME_TASK_PERIOD_US,
           CONFIG_M2_SNTP_SERVER, CONFIG_M2_SNTP_INTERVAL_S);
    esp_timer_handle_t timer;
    const esp_timer_create_args_t timer_args = {.callback = release_cycle, .name = "time_release"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    s_timer_origin = esp_timer_get_time();
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, TIME_TASK_PERIOD_US));
    bool initialized = false;
    uint32_t network_epoch = 0, reported_sync = 0;
    int64_t last_attempt_us = -5000000LL;
    for (;;) {
        int64_t event_us;
        xQueueReceive(s_releases, &event_us, portMAX_DELAY);
        const int64_t start_us = esp_timer_get_time();
        uint32_t epoch;
        const bool online = monitor_network_state(&epoch);
        if (online && (!initialized || epoch != network_epoch) &&
            start_us - last_attempt_us >= 5000000LL) {
            last_attempt_us = start_us;
            esp_err_t result;
            if (!initialized) {
                esp_sntp_set_sync_interval(CONFIG_M2_SNTP_INTERVAL_S * 1000U);
                esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_M2_SNTP_SERVER);
                config.sync_cb = synchronized;
                config.wait_for_sync = false;
                result = esp_netif_sntp_init(&config);
                initialized = result == ESP_OK;
            } else {
                result = esp_netif_sntp_start(); // Nova conexao: retoma a consulta.
            }
            if (result == ESP_OK) network_epoch = epoch;
            else printf("TIME_ERROR | SNTP=%s | nova tentativa em >=5s\n", esp_err_to_name(result));
        }

        // Ciclos executados da TIME_TASK desde o callback SNTP, nao ciclos de CPU.
        portENTER_CRITICAL(&s_lock);
        ++s_cycle;
        portEXIT_CRITICAL(&s_lock);
        time_snapshot_t clock;
        time_task_snapshot(&clock);
        char utc[40] = "AGUARDANDO_SNTP";
        if (clock.synced) {
            time_t seconds = (time_t)(clock.unix_ms / 1000);
            struct tm tm;
            gmtime_r(&seconds, &tm);
            char date[24];
            strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &tm);
            snprintf(utc, sizeof(utc), "%s.%03dZ", date, (int)(clock.unix_ms % 1000));
        }
        if (clock.sync_count != reported_sync) {
            printf("TIME_SYNC | atualizacoes=%" PRIu32 " | mono_us=%" PRId64 "\n",
                   clock.sync_count, clock.last_sync_mono_us);
            reported_sync = clock.sync_count;
        }
        printf("TIME | ciclo=%" PRIu64 " | hora=%s | wifi=%s | sync=%" PRIu32
               " | ciclos_desde_sync=%" PRId64 " | idade_sync_ms=%" PRId64
               " | release_dropped=%" PRIu32 "\n",
               clock.cycle, utc, online ? "ON" : "OFF", clock.sync_count,
               clock.cycles_since_sync, clock.sync_age_ms, clock.release_dropped);
        // Inclui formatacao e impressao acima; exclui apenas o registro de metricas.
        const int64_t end_us = esp_timer_get_time();
        s_record(event_us, start_us, end_us);
    }
}

esp_err_t time_task_start(time_cycle_record_t record, TaskHandle_t *handle) {
    if (s_releases || !record || !handle) return ESP_ERR_INVALID_STATE;
    s_record = record;
    s_releases = xQueueCreate(4, sizeof(int64_t));
    if (!s_releases) return ESP_ERR_NO_MEM;
    return xTaskCreatePinnedToCore(task_time, "TIME_TASK", 4096, NULL, 1, handle, 0)
           == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
#else
void time_task_snapshot(time_snapshot_t *snapshot) {
    *snapshot = (time_snapshot_t){.mono_us = esp_timer_get_time(), .unix_ms = -1,
        .cycles_since_sync = -1, .sync_age_ms = -1, .last_sync_mono_us = -1};
}
esp_err_t time_task_start(time_cycle_record_t record, TaskHandle_t *handle) {
    (void)record; (void)handle;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

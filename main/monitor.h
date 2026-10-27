#pragma once

#include "sdkconfig.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "monitor_protocol.h"

#define MONITOR_PERIOD_US 500000LL
typedef struct {
    monitor_message_t message;
    int64_t received_us;
    const char *source; // Literal estatico: PC_UDP ou PC_TCP.
} monitor_request_t;

typedef struct {
    const char *task;   // Literais estaticos, nunca buffers locais.
    const char *source;
    uint64_t job;
    int64_t event_us;
    int64_t start_us;
    int64_t end_us;
    uint32_t session;
    uint32_t command_sequence;
    bool miss;
    int64_t end_unix_ms; // -1 se o fim preceder a ultima referencia SNTP.
    uint32_t time_sync_count;
    int64_t safe_us;
    uint32_t sample_sequence;
} monitor_job_t;

typedef esp_err_t (*monitor_dispatch_t)(const monitor_request_t *request);
typedef void (*monitor_cycle_t)(int64_t event_us, int64_t start_us, int64_t end_us);

esp_err_t monitor_start(monitor_dispatch_t dispatch, monitor_cycle_t record,
                        TaskHandle_t *handle);
// Apenas contexto de TASK, sem espera por espaco. Filas cheias sao contadas.
void monitor_publish_job(const monitor_job_t *job);
// Epoch muda em desconexao/reconexao, mesmo se entre duas consultas.
bool monitor_network_state(uint32_t *epoch);

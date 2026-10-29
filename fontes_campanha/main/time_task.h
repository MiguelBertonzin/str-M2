#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TIME_TASK_PERIOD_US 1000000LL

typedef struct {
    bool enabled;
    bool synced; // Ja recebeu ao menos uma atualizacao; nao garante frescor.
    uint32_t sync_count;
    uint64_t cycle;
    int64_t cycles_since_sync; // -1 antes da primeira atualizacao.
    int64_t mono_us;
    int64_t unix_ms;           // -1 enquanto nao sincronizado; UTC.
    int64_t sync_age_ms;       // Idade monotona, independente de ajustes UTC.
    int64_t last_sync_mono_us;
    uint32_t release_dropped;
} time_snapshot_t;

typedef void (*time_cycle_record_t)(int64_t event_us, int64_t start_us, int64_t end_us);
esp_err_t time_task_start(time_cycle_record_t record, TaskHandle_t *handle);
void time_task_snapshot(time_snapshot_t *snapshot);

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define EXP_TASKS 6
#define EXP_HIST_BINS 1024
#define EXP_BIN_US 100U
#define EXP_M 9U
#define EXP_K 10U
typedef struct {
    uint32_t jobs, misses, lost, invalid;
    uint64_t exec_sum_us;
    uint32_t exec_max_us, response_max_us, latency_max_us, safe_max_us;
    uint64_t jitter_sum_us;
    uint32_t jitter_max_us, isr_jobs, isr_latency_max_us, isr_safe_max_us;
    uint32_t windows, violations, min_hits;
    uint32_t p99_low_us, p99_high_us, histogram_overflow;
    int64_t last_event_us, last_start_us, last_end_us;
    int64_t run_start_us, run_end_us;
} experiment_snapshot_t;

// Uma janela por boot. start retorna false se ja tiver sido iniciada.
bool experiment_begin(int64_t now_us);
void experiment_bounds(int64_t *start_us, int64_t *end_us);
void experiment_record(unsigned task, int64_t event, int64_t start, int64_t end,
                       uint32_t deadline, int64_t safe_us, bool isr_event);
// APIs somente em contexto de task (inclusive callback esp_timer).
void experiment_lost(unsigned task, int64_t event);
void experiment_lost_isr(unsigned task, int64_t event);
void experiment_snapshot(unsigned task, experiment_snapshot_t *out);
const char *experiment_task_name(unsigned task);

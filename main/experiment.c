#include "experiment.h"
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    experiment_snapshot_t value;
    uint32_t histogram[EXP_HIST_BINS];
    uint16_t recent;
    uint8_t recent_count;
    uint32_t previous_latency;
} accumulator_t;
static accumulator_t s_stats[EXP_TASKS];
static int64_t s_begin, s_end;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

const char *experiment_task_name(unsigned task) {
    static const char *const names[] = {"FUS_IMU", "CTRL_ATT", "NAV_PLAN",
        "FAIL_SAFE", "MONITOR_TASK", "TIME_TASK"};
    return task < EXP_TASKS ? names[task] : "INVALID";
}
bool experiment_begin(int64_t now_us) {
    portENTER_CRITICAL(&s_lock);
    bool accepted = s_begin == 0;
    if (accepted) {
        s_begin = now_us;
        s_end = now_us + CONFIG_M2_RUN_SECONDS * 1000000LL;
    }
    portEXIT_CRITICAL(&s_lock);
    return accepted;
}
void experiment_bounds(int64_t *start_us, int64_t *end_us) {
    portENTER_CRITICAL(&s_lock);
    *start_us = s_begin;
    *end_us = s_end;
    portEXIT_CRITICAL(&s_lock);
}
static bool in_window(int64_t event) {
    return s_begin != 0 && event >= s_begin && event < s_end;
}
static void window_result(accumulator_t *a, bool hit) {
    a->recent = ((a->recent << 1) | hit) & ((1U << EXP_K) - 1);
    if (a->recent_count < EXP_K) ++a->recent_count;
    if (a->recent_count == EXP_K) {
        unsigned hits = __builtin_popcount((unsigned)a->recent);
        if (a->value.windows == 0 || hits < a->value.min_hits) a->value.min_hits = hits;
        ++a->value.windows;
        a->value.violations += hits < EXP_M;
    }
}
void experiment_record(unsigned task, int64_t event, int64_t start, int64_t end,
                       uint32_t deadline, int64_t safe_us, bool isr_event) {
    if (task >= EXP_TASKS) return;
    portENTER_CRITICAL(&s_lock);
    if (in_window(event)) {
        accumulator_t *a = &s_stats[task];
        if (start < event || end < start || end - event > UINT32_MAX) {
            ++a->value.invalid;
            window_result(a, false);
        } else {
            uint32_t execution = (uint32_t)(end - start);
            uint32_t response = (uint32_t)(end - event);
            uint32_t latency = (uint32_t)(start - event);
            uint32_t jitter = (task == 0 || task == 4 || task == 5) ? latency :
                !a->value.jobs ? 0 : latency >= a->previous_latency ?
                latency - a->previous_latency : a->previous_latency - latency;
            a->previous_latency = latency;
            a->value.jitter_sum_us += jitter;
            if (jitter > a->value.jitter_max_us) a->value.jitter_max_us = jitter;
            ++a->value.jobs;
            a->value.misses += response > deadline;
            a->value.exec_sum_us += execution;
            if (execution > a->value.exec_max_us) a->value.exec_max_us = execution;
            if (response > a->value.response_max_us) a->value.response_max_us = response;
            if (latency > a->value.latency_max_us) a->value.latency_max_us = latency;
            if (safe_us >= start && safe_us <= end && safe_us - event > a->value.safe_max_us)
                a->value.safe_max_us = (uint32_t)(safe_us - event);
            if (isr_event) {
                ++a->value.isr_jobs;
                if (latency > a->value.isr_latency_max_us) a->value.isr_latency_max_us = latency;
                if (safe_us >= start && safe_us <= end && safe_us - event > a->value.isr_safe_max_us)
                    a->value.isr_safe_max_us = (uint32_t)(safe_us - event);
            }
            uint32_t bin = response / EXP_BIN_US;
            if (bin < EXP_HIST_BINS) ++a->histogram[bin];
            else ++a->value.histogram_overflow;
            window_result(a, response <= deadline);
            a->value.last_event_us = event;
            a->value.last_start_us = start;
            a->value.last_end_us = end;
        }
    }
    portEXIT_CRITICAL(&s_lock);
}
void experiment_lost(unsigned task, int64_t event) {
    if (task >= EXP_TASKS) return;
    portENTER_CRITICAL(&s_lock);
    if (in_window(event)) ++s_stats[task].value.lost;
    // Nao insere em (m,k): o evento perdido pode ser posterior a um job em voo.
    // Qualquer perda torna (m,k) sobre ativacoes inconclusivo e deve ser reportada.
    portEXIT_CRITICAL(&s_lock);
}
void experiment_lost_isr(unsigned task, int64_t event) {
    if (task >= EXP_TASKS) return;
    portENTER_CRITICAL_ISR(&s_lock);
    if (in_window(event)) ++s_stats[task].value.lost;
    portEXIT_CRITICAL_ISR(&s_lock);
}
void experiment_snapshot(unsigned task, experiment_snapshot_t *out) {
    // Usada apenas pelo MONITOR (stack 10 KiB). Copia sob lock, scan fora dele.
    uint32_t histogram[EXP_HIST_BINS];
    portENTER_CRITICAL(&s_lock);
    *out = s_stats[task].value;
    memcpy(histogram, s_stats[task].histogram, sizeof(histogram));
    out->run_start_us = s_begin;
    out->run_end_us = s_end;
    portEXIT_CRITICAL(&s_lock);
    if (!out->jobs) return;
    uint32_t rank = (uint32_t)(((uint64_t)out->jobs * 99U + 99U) / 100U);
    uint32_t total = 0;
    for (unsigned b = 0; b < EXP_HIST_BINS; ++b) {
        total += histogram[b];
        if (total >= rank) {
            out->p99_low_us = b * EXP_BIN_US;
            out->p99_high_us = (b + 1U) * EXP_BIN_US - 1U;
            return;
        }
    }
    out->p99_low_us = EXP_HIST_BINS * EXP_BIN_US;
    out->p99_high_us = out->response_max_us;
}

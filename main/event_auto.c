#include "sdkconfig.h"

#if CONFIG_M2_INPUT_AUTO
#include <inttypes.h>
#include <stdio.h>
#include "driver/gptimer.h"
#include "esp_timer.h"
#include "event_auto.h"
#include "event_script.h"

// Buffer fixo: uma posicao por alarme. A ISR publica a quantidade somente
// depois de preencher os dados; a task usa acquire antes de le-los.
typedef struct {
    uint64_t counter_us;
    int64_t isr_us;
} event_trace_t;

static gptimer_handle_t s_timer;
static event_dispatch_isr_t s_dispatch;
static event_trace_t s_trace[EVENT_SCRIPT_COUNT];
static uint32_t s_ready;
static uint32_t s_printed;
static int s_error;
static bool s_started;

static bool on_alarm(gptimer_handle_t timer,
                     const gptimer_alarm_event_data_t *alarm, void *context) {
    (void)context;
    const uint32_t index = __atomic_load_n(&s_ready, __ATOMIC_RELAXED);
    if (index >= EVENT_SCRIPT_COUNT) {
        return false;
    }
    const int64_t now_us = esp_timer_get_time();
    s_trace[index] = (event_trace_t){.counter_us = alarm->count_value,
                                    .isr_us = now_us};
    const bool yield = s_dispatch(EVENT_SCRIPT[index].mask, now_us);
    esp_err_t result;
    if (index + 1U < EVENT_SCRIPT_COUNT) {
        // Proximo instante absoluto; nao acumula o atraso do callback anterior.
        const gptimer_alarm_config_t next = {
            .alarm_count = EVENT_SCRIPT[index + 1U].offset_us,
        };
        result = gptimer_set_alarm_action(timer, &next);
    } else {
        result = gptimer_stop(timer);
    }
    if (result != ESP_OK) {
        __atomic_store_n(&s_error, result, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&s_ready, index + 1U, __ATOMIC_RELEASE);
    return yield;
}

esp_err_t event_auto_start(event_dispatch_isr_t dispatch) {
    if (dispatch == NULL || s_timer != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_dispatch = dispatch;
    const gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
        .intr_priority = 1,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&config, &s_timer));
    const gptimer_event_callbacks_t callbacks = {.on_alarm = on_alarm};
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(s_timer, &callbacks, NULL));
    const gptimer_alarm_config_t first = {
        .alarm_count = EVENT_SCRIPT[0].offset_us,
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(s_timer, &first));
    ESP_ERROR_CHECK(gptimer_enable(s_timer));
    // Sem IRAM_ATTR: configuracao padrao nao habilita ISR cache-safe. Nao
    // executar operacoes de flash durante a coleta. Falhas nao sao ignoradas.
#if CONFIG_M2_AUTO_WAIT_PC
    printf("AUTO_READY | aguardando BEGIN do PC\n");
    return ESP_OK;
#else
    return event_auto_trigger();
#endif
}

esp_err_t event_auto_trigger(void) {
    if (!s_timer || s_started) return ESP_ERR_INVALID_STATE;
    esp_err_t result = gptimer_start(s_timer);
    if (result == ESP_OK) s_started = true;
    return result;
}

void event_auto_report(void) {
    const uint32_t ready = __atomic_load_n(&s_ready, __ATOMIC_ACQUIRE);
    while (s_printed < ready) {
        const uint32_t i = s_printed++;
        const int64_t late_us = (int64_t)s_trace[i].counter_us -
                                (int64_t)EVENT_SCRIPT[i].offset_us;
        printf("AUTO_EVENT;GPTIMER_ISR;%lu;0x%lx;%" PRIu64 ";%" PRIu64
               ";%" PRId64 ";%" PRId64 "\n",
               (unsigned long)(i + 1U), (unsigned long)EVENT_SCRIPT[i].mask,
               EVENT_SCRIPT[i].offset_us, s_trace[i].counter_us,
               s_trace[i].isr_us, late_us);
    }
    const int error = __atomic_load_n(&s_error, __ATOMIC_ACQUIRE);
    if (error != ESP_OK) {
        printf("AUTO_ERROR | %s | ensaio invalido\n", esp_err_to_name(error));
    }
}
#endif

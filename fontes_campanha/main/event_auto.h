#pragma once

#include "esp_err.h"
esp_err_t event_auto_trigger(void);

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Retornar true solicita yield ao sair da ISR, como no callback touch da M1.
typedef bool (*event_dispatch_isr_t)(uint32_t mask, int64_t event_time_us);

esp_err_t event_auto_start(event_dispatch_isr_t dispatch);
// Chamado pela tarefa auxiliar: imprime registros prontos, nunca dentro da ISR.
void event_auto_report(void);

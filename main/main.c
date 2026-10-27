// M2: base M1 com entrada fisica ou roteiro automatico em ISR.
// Autopiloto didatico de drone (ESP32 + FreeRTOS).
// As operacoes de sensores, controle, motores e navegacao sao simuladas.
//
// Fluxo principal do sistema:
//   FUS_IMU -> estado inercial -> fila de tamanho 1 -> CTRL_ATT -> motores
//   Touch A/B/C -> fila de eventos -> NAV_PLAN -> estresse/rota/telemetria
//   Touch D -> fila exclusiva -> FAIL_SAFE -> motores zerados
//
// As quatro tarefas acima sao instrumentadas com timestamps. METRICS e
// EXP_GUIDE sao tarefas auxiliares: apresentam os resultados e orientam a
// sequencia de contatos, mas nao representam funcoes do autopiloto.

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "event_script.h"
#include "event_auto.h"
#include "monitor.h"
#include "time_task.h"
#include "experiment.h"
#include "driver/touch_sens.h" // API atual de touch do ESP-IDF 6.1.
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ============================================================================
// CONFIGURACAO DAS POLITICAS DE PRIORIDADE
// ============================================================================
// A politica e selecionada em tempo de compilacao. Para comparar os cenarios,
// selecione a politica em menuconfig > M2 e recompile. Numeros maiores representam
// prioridades maiores no FreeRTOS.
//
// CUSTOM prioriza a seguranca: FAIL_SAFE > FUS_IMU > CTRL_ATT > NAV_PLAN.
// DM ordena pelos deadlines: 5 ms, 10 ms e 20 ms.
// RM ordena pelos periodos/intervalos minimos e e usado como experimento extra.
#define POLICY_CUSTOM 1
#define POLICY_DM 2
#define POLICY_RM 3
#ifndef PRIORITY_POLICY
#define PRIORITY_POLICY CONFIG_M2_PRIORITY_POLICY
#endif

#if PRIORITY_POLICY == POLICY_CUSTOM
#define POLICY_NAME "CUSTOM"
#define PRIO_FAIL_SAFE 5U
#define PRIO_FUS_IMU 4U
#define PRIO_CTRL_ATT 3U
#define PRIO_NAV_PLAN 2U
#elif PRIORITY_POLICY == POLICY_DM
// DM: D_FUS=D_CTRL=5 ms, D_FS=10 ms e D_NAV=20 ms.
#define POLICY_NAME "DM"
#define PRIO_FAIL_SAFE 4U
#define PRIO_FUS_IMU 5U
#define PRIO_CTRL_ATT 5U
#define PRIO_NAV_PLAN 3U
#elif PRIORITY_POLICY == POLICY_RM
// RM (extra): FUS e CTRL possuem T=5 ms. Para os eventos, o debounce de
// 1,2 s e usado como intervalo minimo entre ativacoes do mesmo touch.
#define POLICY_NAME "RM"
#define PRIO_FAIL_SAFE 4U
#define PRIO_FUS_IMU 5U
#define PRIO_CTRL_ATT 5U
#define PRIO_NAV_PLAN 3U
#else
#error "PRIORITY_POLICY invalida"
#endif

// ============================================================================
// TEMPORIZACAO, ESCALONADOR E AFINIDADE DE NUCLEO
// ============================================================================
// FUS_IMU e liberada a cada 5 ms. METRICS produz um snapshot a cada 5 s, e o
// guia verifica novos contatos a cada 50 ms. A sintese final so aparece depois
// de 30 s e da conclusao correta dos 14 eventos.
#define FUS_PERIOD_MS 5U
_Static_assert(CONFIG_FREERTOS_HZ == 1000, "Usar tick de 1 ms para a base M2");
#if CONFIG_M2_PREEMPTION
_Static_assert(configUSE_PREEMPTION == 1, "Override de preempcao nao aplicado");
#else
_Static_assert(configUSE_PREEMPTION == 0, "Override de preempcao nao aplicado");
#endif
#define TASK_STACK_SIZE 3072U
#define PRIO_METRICS 1U
#define METRICS_PERIOD_MS 5000U
#define GUIDE_POLL_PERIOD_MS 50U
#define EXPERIMENT_MIN_DURATION_US 30000000LL

// Em placas dual-core, relatorio e guia ficam fora do core das tarefas
// medidas. A configuracao unicore continua compilavel para comparacoes futuras.
#if CONFIG_FREERTOS_UNICORE
#define AUXILIARY_CORE 0
#define EXPERIMENT_CORE_COUNT 1U
#else
#define AUXILIARY_CORE 1
#define EXPERIMENT_CORE_COUNT 2U
#endif

#if configUSE_PREEMPTION == 1
#define SCHEDULER_NAME "PREEMPTIVO"
#elif configUSE_PREEMPTION == 0
#define SCHEDULER_NAME "COOPERATIVO"
#else
#error "configUSE_PREEMPTION deve ser 0 ou 1"
#endif

// Deadlines relativos usados para classificar cada job como atendido ou miss.
// A resposta e medida do evento de referencia ate o fim instrumentado.
#define DEADLINE_FUS_US 5000ULL
#define DEADLINE_CTRL_US 5000ULL
#define DEADLINE_NAV_US 20000ULL
#define DEADLINE_FAIL_SAFE_US 10000ULL

// ============================================================================
// CARGA COMPUTACIONAL CONTROLADA
// ============================================================================
// Cada tarefa chama simulate_fixed_work com uma quantidade fixa de operacoes.
// O laco nao espera por tempo: a mesma carga tende a demorar mais em 80 MHz e
// menos em 240 MHz. Isso permite comparar frequencia e escalonamento.
#define FUS_WORK_ITERATIONS 3000U
#define CTRL_WORK_ITERATIONS 2000U
#define NAV_WORK_ITERATIONS 24000U
#define FS_WORK_ITERATIONS 6000U
#define FUS_STRESS_WORK_ITERATIONS 500U
#define STRESS_WORK_EVERY_N_SAMPLES 4U

// ============================================================================
// ENTRADAS TOUCH E ROTEIRO FISICO
// ============================================================================
// Mapeamento fisico escolhido para manter os quatro contatos separados na
// ESP32 DevKit. Cada contato aceito gera no maximo um evento ate que o canal
// permaneca liberado pelo intervalo configurado.
// Canais definidos em event_script.h para ambas as fontes.
#define TOUCH_CHANNEL_COUNT 4U
#define TOUCH_DEBOUNCE_US 1200000LL
#define TOUCH_RELEASE_STABLE_MS 500U
#define TOUCH_INITIAL_SCAN_COUNT 5U
#define TOUCH_THRESHOLD_PERCENT 80U
#define TOUCH_SEQUENCE_CAPACITY 64U

// A medicao de cada canal dura aproximadamente 1 ms. Com quatro canais, uma
// varredura completa permanece abaixo do deadline de 10 ms do fail-safe.
#define TOUCH_MEASUREMENT_DURATION_MS 1.0f

// Dez segundos permitem executar B e C fisicamente durante a carga extra.
#define STRESS_DURATION_SAMPLES 2000U

// ============================================================================
// PARAMETROS DO MODELO SIMULADO DO DRONE
// ============================================================================
// dt acompanha o periodo de 5 ms. O filtro complementar usa principalmente o
// giroscopio integrado (98%) e corrige lentamente com o acelerometro (2%). Os
// motores sao percentuais logicos, sem acionamento de PWM fisico.
#define IMU_DT_SECONDS 0.005f
#define COMPLEMENTARY_ALPHA 0.98f
#define MOTOR_MIN_PERCENT 0.0f
#define MOTOR_MAX_PERCENT 100.0f
#define MOTOR_HOVER_PERCENT 50.0f

// ============================================================================
// TIPOS DE DADOS TROCADOS ENTRE TAREFAS
// ============================================================================
// Estado produzido pela fusao. O timestamp da amostra e usado como evento de
// referencia para medir a resposta ponta a ponta de CTRL_ATT.
typedef struct {
  float roll;
  float pitch;
  float yaw;
  int64_t sample_time_us;
  uint32_t sequence;
} imu_state_t;

// Referencia desejada pelo controle, atualizada quando NAV_PLAN troca waypoint.
typedef struct {
  float roll;
  float pitch;
  float yaw;
} attitude_reference_t;

// Saida logica do controlador: percentual dos quatro motores e amostra usada.
typedef struct {
  float motor[4];
  uint32_t source_sequence;
} actuator_state_t;

// Memoria de um PID: ganhos, integral acumulada e erro da iteracao anterior.
typedef struct {
  float kp;
  float ki;
  float kd;
  float integral;
  float previous_error;
} pid_axis_t;

// Tipos de mensagens que podem ser recebidas pela tarefa NAV_PLAN.
typedef enum { EV_NAV = 1, EV_TEL = 2, EV_STRESS = 3 } nav_event_type_t;

// Evento originado na ISR. O timestamp permite separar latencia de ativacao e
// tempo de resposta; stress_active_at_event preserva o estado no toque.
typedef struct {
  nav_event_type_t type;
  int64_t event_time_us;
  bool stress_active_at_event;
  const char *source;
  uint32_t session;
  uint32_t command_sequence;
} nav_event_t;

// O fail-safe usa uma fila exclusiva e precisa transportar apenas o timestamp.
typedef struct {
  int64_t event_time_us;
  const char *source;
  uint32_t session;
  uint32_t command_sequence;
} fail_safe_event_t;

// Descreve a associacao entre rotulo, canal touch e GPIO fisico.
typedef struct {
  char label;
  int channel_id;
  int gpio_num;
} touch_input_t;

// Atitude de referencia associada a um ponto da rota simulada.
typedef struct {
  float roll;
  float pitch;
  float yaw;
} waypoint_t;

// Indices comuns aos contadores e aos handles das quatro tarefas medidas.
typedef enum {
  METRIC_FUS = 0,
  METRIC_CTRL,
  METRIC_NAV,
  METRIC_FAIL_SAFE,
  METRIC_MONITOR,
  METRIC_TIME,
  METRIC_TASK_COUNT
} metric_task_id_t;
_Static_assert(METRIC_TASK_COUNT == EXP_TASKS && METRIC_MONITOR == 4 && METRIC_TIME == 5,
               "Indices das estatisticas devem acompanhar as tarefas");

// Acumuladores de instrumentacao. Eles permitem imprimir minimo, media e
// maximo sem armazenar uma amostra individual para cada job.
typedef struct {
  uint64_t jobs;
  uint64_t deadline_misses;
  uint64_t execution_sum_us;
  uint64_t response_sum_us;
  uint64_t jitter_sum_us;
  uint64_t execution_min_us;
  uint64_t execution_max_us;
  uint64_t response_min_us;
  uint64_t response_max_us;
  uint64_t start_latency_max_us;
  uint64_t jitter_max_us;
  uint64_t previous_start_latency_us;
  int64_t last_event_us;
  int64_t last_start_us;
  int64_t last_end_us;
} task_metrics_t;

// ============================================================================
// ROTA E SEQUENCIA GUIADA DO EXPERIMENTO
// ============================================================================
// Cada Touch B avanca circularmente entre estes tres pontos de referencia.
static const waypoint_t WAYPOINTS[] = {
    {0.0f, 0.0f, 0.0f}, {5.0f, 0.0f, 30.0f}, {0.0f, 5.0f, 60.0f}};

#if CONFIG_M2_INPUT_TOUCH
static const touch_input_t TOUCH_INPUTS[TOUCH_CHANNEL_COUNT] = {
    {.label = 'A', .channel_id = TOUCH_A_CHANNEL, .gpio_num = 4},
    {.label = 'B', .channel_id = TOUCH_B_CHANNEL, .gpio_num = 27},
    {.label = 'C', .channel_id = TOUCH_C_CHANNEL, .gpio_num = 13},
    {.label = 'D', .channel_id = TOUCH_D_CHANNEL, .gpio_num = 32},
};

#endif
#if !CONFIG_M2_INPUT_PC
static const char GUIDE_EXPECTED_SEQUENCE[] = EVENT_SCRIPT_SEQUENCE;
#endif
#if CONFIG_M2_INPUT_TOUCH
static const char *const GUIDE_PROMPTS[] = {
    "1/14: toque C (GPIO13) por cerca de 1 segundo",
    "2/14: toque B (GPIO27) por cerca de 1 segundo",
    "3/14: toque C (GPIO13) por cerca de 1 segundo",
    "4/14: toque A (GPIO4) para iniciar 10 segundos de estresse",
    "5/14: durante o estresse, toque B (GPIO27)",
    "6/14: durante o estresse, toque C (GPIO13)",
    "7/14: sequencia consecutiva, toque B (GPIO27)",
    "8/14: sequencia consecutiva, toque C (GPIO13)",
    "9/14: sequencia consecutiva, toque B (GPIO27)",
    "10/14: sequencia consecutiva, toque C (GPIO13)",
    "11-12/14: toque B (GPIO27) e C (GPIO13) juntos",
    "aguardando o C do toque simultaneo B+C",
    "13/14: toque D (GPIO32) para o fail-safe",
    "14/14: toque C (GPIO13) para confirmar o estado seguro",
};
#endif

// ============================================================================
// FILAS, HANDLES E ESTADO COMPARTILHADO
// ============================================================================
// g_imu_queue tem tamanho 1 e conserva somente a amostra mais recente.
// g_nav_queue guarda ate 8 eventos comuns em ordem de chegada.
// g_fail_safe_queue tem tamanho 1 e e exclusiva para a emergencia.
// As filas transportam dados; quem executa codigo sao as tarefas consumidoras.
static QueueHandle_t g_imu_queue = NULL;
static QueueHandle_t g_nav_queue = NULL;
static QueueHandle_t g_fail_safe_queue = NULL;
static QueueHandle_t g_fus_releases;
static int64_t g_fus_origin_us;
static uint64_t g_fus_release_number;
static uint32_t g_ctrl_overwritten, g_ctrl_sequence_gaps;

// Handles do driver touch e variaveis atomicas usadas entre a ISR e as tasks.
// O latch evita repeticoes enquanto o dedo permanece no pad; os timestamps de
// inatividade implementam o rearme somente apos uma liberacao estavel.
#if CONFIG_M2_INPUT_TOUCH
static touch_sensor_handle_t g_touch_sensor = NULL;
static touch_channel_handle_t g_touch_channels[TOUCH_CHANNEL_COUNT] = {0};
static int64_t g_last_touch_time_us[10] = {0};
static volatile uint32_t g_touch_latched_mask = 0;
static volatile uint32_t g_touch_last_inactive_ms[10] = {0};
#endif
static volatile uint32_t g_touch_event_count[10] = {0};
static volatile char g_touch_sequence[TOUCH_SEQUENCE_CAPACITY] = {0};
static volatile uint32_t g_touch_sequence_length = 0;
static volatile uint32_t g_touch_dropped_events = 0;
#if !CONFIG_M2_INPUT_PC
static volatile uint32_t g_guide_allowed_length = 1U;
#endif
static volatile bool g_guide_invalid = false;
#if !CONFIG_M2_INPUT_PC
static volatile char g_guide_invalid_label = '?';
static volatile uint32_t g_guide_invalid_position = 0;
#endif
static volatile bool g_guide_complete = false;
static volatile bool g_stress_request = false;
static volatile bool g_stress_active = false;
// Handles usados por METRICS para ler o tempo de CPU acumulado das tarefas.
static TaskHandle_t g_measured_task_handles[METRIC_TASK_COUNT] = {0};
static int64_t g_measurement_start_us = 0;

// g_state_lock protege o estado funcional do drone; g_metrics_lock protege os
// acumuladores. As secoes criticas ficam curtas para reduzir bloqueios.
static portMUX_TYPE g_state_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE g_metrics_lock = portMUX_INITIALIZER_UNLOCKED;
static task_metrics_t g_metrics[METRIC_TASK_COUNT] = {0};
static imu_state_t g_latest_imu = {0};
static attitude_reference_t g_reference = {0};
static actuator_state_t g_actuators = {0};
static bool g_fail_safe_active = false;
static uint32_t g_waypoint_index = 0;
static uint32_t g_stress_samples_remaining = 0;

// ============================================================================
// FUNCOES AUXILIARES DE CALCULO E INSTRUMENTACAO
// ============================================================================
// Mantem referencias e comandos de motor dentro dos limites permitidos.
static float clamp_float(float value, float minimum, float maximum) {
  if (value < minimum) {
    return minimum;
  }
  if (value > maximum) {
    return maximum;
  }
  return value;
}

// Diferenca temporal protegida contra referencias posteriores ao instante final.
static uint64_t nonnegative_delta_us(int64_t end_us, int64_t start_us) {
  return end_us > start_us ? (uint64_t)(end_us - start_us) : 0ULL;
}

// Distancia absoluta entre timestamps, usada no jitter periodico da FUS_IMU.
static uint64_t absolute_delta_us(int64_t first_us, int64_t second_us) {
  return first_us >= second_us ? (uint64_t)(first_us - second_us)
                               : (uint64_t)(second_us - first_us);
}

// Atualiza os acumuladores de um job:
//   execucao = fim - inicio
//   resposta = fim - evento
//   latencia = inicio - evento
// Um miss ocorre quando a resposta ultrapassa o deadline da tarefa.
static void metrics_record_detail(metric_task_id_t task_id, int64_t event_us,
                           int64_t start_us, int64_t end_us,
                           uint64_t deadline_us, const char *source,
                           uint32_t session, uint32_t command_sequence,
                           int64_t safe_us, uint32_t sample_sequence) {
  experiment_record(task_id, event_us, start_us, end_us, deadline_us, safe_us,
                    !strcmp(source, "TOUCH_ISR") || !strcmp(source, "GPTIMER_ISR"));
  const uint64_t execution_us = nonnegative_delta_us(end_us, start_us);
  const uint64_t response_us = nonnegative_delta_us(end_us, event_us);
  const uint64_t start_latency_us = nonnegative_delta_us(start_us, event_us);

  portENTER_CRITICAL(&g_metrics_lock);
  task_metrics_t *metric = &g_metrics[task_id];
  uint64_t jitter_us;

  if (task_id == METRIC_FUS || task_id == METRIC_TIME || task_id == METRIC_MONITOR) {
    // Para a tarefa periodica, jitter e o desvio do inicio real em relacao
    // ao instante de liberacao esperado.
    jitter_us = absolute_delta_us(start_us, event_us);
  } else if (metric->jobs == 0U) {
    jitter_us = 0U;
  } else {
    // Para tarefas encadeadas ou por evento, jitter e a variacao da latencia
    // de ativacao entre dois jobs consecutivos.
    jitter_us = start_latency_us >= metric->previous_start_latency_us
                    ? start_latency_us - metric->previous_start_latency_us
                    : metric->previous_start_latency_us - start_latency_us;
  }

  if (metric->jobs == 0U) {
    metric->execution_min_us = execution_us;
    metric->response_min_us = response_us;
  } else {
    if (execution_us < metric->execution_min_us) {
      metric->execution_min_us = execution_us;
    }
    if (response_us < metric->response_min_us) {
      metric->response_min_us = response_us;
    }
  }

  metric->jobs++;
  metric->deadline_misses += response_us > deadline_us ? 1U : 0U;
  metric->execution_sum_us += execution_us;
  metric->response_sum_us += response_us;
  metric->jitter_sum_us += jitter_us;
  if (execution_us > metric->execution_max_us) {
    metric->execution_max_us = execution_us;
  }
  if (response_us > metric->response_max_us) {
    metric->response_max_us = response_us;
  }
  if (start_latency_us > metric->start_latency_max_us) {
    metric->start_latency_max_us = start_latency_us;
  }
  if (jitter_us > metric->jitter_max_us) {
    metric->jitter_max_us = jitter_us;
  }
  metric->last_event_us = event_us;
  metric->last_start_us = start_us;
  metric->last_end_us = end_us;
  metric->previous_start_latency_us = start_latency_us;
  const uint64_t job_number = metric->jobs;
  portEXIT_CRITICAL(&g_metrics_lock);

  // Copia para a fila somente depois de soltar o lock. Nunca faz I/O aqui.
  static const char *const names[METRIC_TASK_COUNT] = {
      "FUS_IMU", "CTRL_ATT", "NAV_PLAN", "FAIL_SAFE", "MONITOR_TASK", "TIME_TASK"};
  const monitor_job_t job = {.task = names[task_id], .source = source,
      .job = job_number, .event_us = event_us, .start_us = start_us,
      .end_us = end_us, .session = session, .command_sequence = command_sequence,
      .miss = response_us > deadline_us, .safe_us = safe_us,
      .sample_sequence = sample_sequence};
  monitor_publish_job(&job);
}

static void metrics_record(metric_task_id_t task_id, int64_t event_us,
                           int64_t start_us, int64_t end_us,
                           uint64_t deadline_us, const char *source,
                           uint32_t session, uint32_t command_sequence) {
  metrics_record_detail(task_id, event_us, start_us, end_us, deadline_us,
                        source, session, command_sequence, -1, 0);
}

// Executa sempre a mesma quantidade de calculos de ponto flutuante. volatile e
// noinline impedem que o compilador elimine o trabalho. O resultado nao altera
// o estado do drone: esta funcao existe para gerar carga comparavel.
// O maior tempo obtido nos ensaios e um maximo observado, nao WCET garantido.
static __attribute__((noinline)) void simulate_fixed_work(uint32_t iterations,
                                                          float seed) {
  // A variavel local volatile mantem os calculos sem criar estado
  // compartilhado entre as tarefas.
  volatile float value = seed;

  for (uint32_t i = 0; i < iterations; ++i) {
    const float input = (float)(i & 0x0FU) * 0.0001f;
    value = (value * 0.9997f) + input;
    value = (value * 0.9999f) - (input * 0.0002f);
  }

  (void)value;
}

// Calcula uma iteracao do PID de um eixo. A integral e limitada para reduzir
// windup, e previous_error permite calcular a parcela derivativa.
static float pid_update(pid_axis_t *pid, float reference, float measured) {
  const float error = reference - measured;
  pid->integral += error * IMU_DT_SECONDS;
  pid->integral = clamp_float(pid->integral, -20.0f, 20.0f);

  const float derivative = (error - pid->previous_error) / IMU_DT_SECONDS;
  pid->previous_error = error;

  return (pid->kp * error) + (pid->ki * pid->integral) + (pid->kd * derivative);
}

// ============================================================================
// TRATAMENTO DOS EVENTOS TOUCH
// ============================================================================
// Rejeita ativacoes repetidas do mesmo canal dentro da janela de debounce.
// Esta verificacao ocorre no contexto da interrupcao e usa operacoes atomicas.
#if CONFIG_M2_INPUT_TOUCH
static bool touch_debounce_accept(int channel_id, int64_t now_us) {
  const int64_t previous_us = g_last_touch_time_us[channel_id];
  if (previous_us != 0 && (now_us - previous_us) < TOUCH_DEBOUNCE_US) {
    return false;
  }

  g_last_touch_time_us[channel_id] = now_us;
  return true;
}

// Aceita o primeiro contato imediatamente. Nos contatos seguintes, exige que
// o canal tenha permanecido inativo por TOUCH_RELEASE_STABLE_MS antes de rearmar.
static bool touch_contact_begin_accept(int channel_id, int64_t now_us) {
  const uint32_t channel_mask = 1UL << channel_id;
  const uint32_t previous_mask =
      __atomic_fetch_or(&g_touch_latched_mask, channel_mask, __ATOMIC_RELAXED);

  // O primeiro contato do canal nao precisa aguardar uma liberacao anterior.
  if ((previous_mask & channel_mask) == 0U) {
    return touch_debounce_accept(channel_id, now_us);
  }

  // Para os contatos seguintes, precisa ter ocorrido uma inatividade continua.
  // O exchange consome a liberacao candidata. Se o sinal voltar a ativo cedo
  // demais, a oscilacao e ignorada e uma nova inatividade sera necessaria.
  const uint32_t inactive_ms = __atomic_exchange_n(
      &g_touch_last_inactive_ms[channel_id], 0U, __ATOMIC_RELAXED);
  const uint32_t now_ms = (uint32_t)(now_us / 1000LL);
  if (inactive_ms == 0U ||
      (uint32_t)(now_ms - inactive_ms) < TOUCH_RELEASE_STABLE_MS) {
    return false;
  }

  return touch_debounce_accept(channel_id, now_us);
}

#endif

// AUTO injeta eventos logicos ja aceitos; nao simula capacitancia/debounce.
#if !CONFIG_M2_INPUT_PC
static bool input_accept_from_isr(int channel_id, int64_t now_us) {
#if CONFIG_M2_INPUT_TOUCH
  return touch_contact_begin_accept(channel_id, now_us);
#else
  (void)channel_id;
  (void)now_us;
  return true;
#endif
}

// Registra um contato aceito, valida sua posicao no roteiro e atualiza os
// contadores. O guia controla quantos eventos podem entrar antes do proximo
// passo, exceto no contato conjunto B+C.
static void touch_record_accepted_from_isr(int channel_id, char label) {
  __atomic_fetch_add(&g_touch_event_count[channel_id], 1U, __ATOMIC_RELAXED);

  const uint32_t length =
      __atomic_load_n(&g_touch_sequence_length, __ATOMIC_RELAXED);

  const uint32_t expected_length =
      (uint32_t)(sizeof(GUIDE_EXPECTED_SEQUENCE) - 1U);
#if CONFIG_M2_INPUT_TOUCH
  const uint32_t allowed_length =
      __atomic_load_n(&g_guide_allowed_length, __ATOMIC_ACQUIRE);
#endif
  const bool unexpected_label =
      length >= expected_length || label != GUIDE_EXPECTED_SEQUENCE[length];
#if CONFIG_M2_INPUT_TOUCH
  const bool before_prompt = length >= allowed_length;
#else
  // O guia observa o timer; nao controla nem atrasa sua agenda.
  const bool before_prompt = false;
#endif

  // Registra a falha, mas nao bloqueia o evento. Em particular, uma emergencia
  // D sempre deve acionar o fail-safe, mesmo que invalide o roteiro do ensaio.
  if ((unexpected_label || before_prompt) &&
      !__atomic_load_n(&g_guide_invalid, __ATOMIC_RELAXED)) {
    __atomic_store_n(&g_guide_invalid_label, label, __ATOMIC_RELAXED);
    __atomic_store_n(&g_guide_invalid_position, length + 1U,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&g_guide_invalid, true, __ATOMIC_RELEASE);
  }

  if (length < TOUCH_SEQUENCE_CAPACITY - 1U) {
    g_touch_sequence[length] = label;
    __atomic_store_n(&g_touch_sequence_length, length + 1U, __ATOMIC_RELEASE);
  }
}

// Empacota eventos A/B/C e os envia para g_nav_queue sem bloquear a ISR.
// Se a fila de oito itens estiver cheia, registra o descarte para a telemetria.
static void touch_send_nav_event_from_isr(nav_event_type_t type,
                                          int64_t event_time_us,
                                          BaseType_t *task_woken) {
  const nav_event_t event = {
#if CONFIG_M2_INPUT_AUTO
      .source = "GPTIMER_ISR",
#else
      .source = "TOUCH_ISR",
#endif
      .type = type,
      .event_time_us = event_time_us,
      .stress_active_at_event =
          __atomic_load_n(&g_stress_active, __ATOMIC_ACQUIRE),
  };

  if (xQueueSendFromISR(g_nav_queue, &event, task_woken) != pdTRUE) {
    experiment_lost_isr(METRIC_NAV, event_time_us);
    __atomic_fetch_add(&g_touch_dropped_events, 1U, __ATOMIC_RELAXED);
  }
}

// Despacho executado no contexto da interrupcao selecionada (touch ou GPTimer).
// Nenhum processamento pesado ou printf pode ser feito aqui.
// D e tratado primeiro porque representa emergencia. A apenas solicita o
// estresse; o trabalho adicional sera executado posteriormente por FUS_IMU.
// Usado pelas ISRs de touch OU GPTimer. Nunca chamar de uma task.
static bool dispatch_inputs_from_isr(uint32_t active_mask, int64_t now_us) {
  BaseType_t higher_priority_task_woken = pdFALSE;

  // Emergencia e tratada primeiro caso varios canais disparem juntos.
  if ((active_mask & (1UL << TOUCH_D_CHANNEL)) != 0U &&
      input_accept_from_isr(TOUCH_D_CHANNEL, now_us)) {
    touch_record_accepted_from_isr(TOUCH_D_CHANNEL, 'D');
    const fail_safe_event_t fail_safe_event = {.event_time_us = now_us,
#if CONFIG_M2_INPUT_AUTO
        .source = "GPTIMER_ISR"
#else
        .source = "TOUCH_ISR"
#endif
    };
    fail_safe_event_t replaced;
    if (xQueueReceiveFromISR(g_fail_safe_queue, &replaced, &higher_priority_task_woken) == pdTRUE)
      experiment_lost_isr(METRIC_FAIL_SAFE, replaced.event_time_us);
    xQueueOverwriteFromISR(g_fail_safe_queue, &fail_safe_event,
                           &higher_priority_task_woken);
  }

  if ((active_mask & (1UL << TOUCH_B_CHANNEL)) != 0U &&
      input_accept_from_isr(TOUCH_B_CHANNEL, now_us)) {
    touch_record_accepted_from_isr(TOUCH_B_CHANNEL, 'B');
    touch_send_nav_event_from_isr(EV_NAV, now_us, &higher_priority_task_woken);
  }

  if ((active_mask & (1UL << TOUCH_C_CHANNEL)) != 0U &&
      input_accept_from_isr(TOUCH_C_CHANNEL, now_us)) {
    touch_record_accepted_from_isr(TOUCH_C_CHANNEL, 'C');
    touch_send_nav_event_from_isr(EV_TEL, now_us, &higher_priority_task_woken);
  }

  if ((active_mask & (1UL << TOUCH_A_CHANNEL)) != 0U &&
      input_accept_from_isr(TOUCH_A_CHANNEL, now_us)) {
    touch_record_accepted_from_isr(TOUCH_A_CHANNEL, 'A');
    // A FUS inicia a carga no proximo periodo, sem depender da fila da NAV.
    __atomic_store_n(&g_stress_active, true, __ATOMIC_RELEASE);
    __atomic_store_n(&g_stress_request, true, __ATOMIC_RELEASE);
    touch_send_nav_event_from_isr(EV_STRESS, now_us,
                                  &higher_priority_task_woken);
  }

  // O driver executa portYIELD_FROM_ISR quando a callback retorna true.
  return higher_priority_task_woken == pdTRUE;
}

#endif

#if CONFIG_M2_INPUT_TOUCH
static bool touch_on_hardware_active(touch_sensor_handle_t sensor,
                                     const touch_hw_active_event_data_t *event,
                                     void *user_context) {
  (void)sensor;
  (void)user_context;
  return dispatch_inputs_from_isr(event->active_mask, esp_timer_get_time());
}

// Registra o inicio da liberacao. Um contato novo somente sera aceito se o
// canal permanecer inativo pelo intervalo TOUCH_RELEASE_STABLE_MS.
static bool touch_on_inactive(touch_sensor_handle_t sensor,
                              const touch_inactive_event_data_t *event,
                              void *user_context) {
  (void)sensor;
  (void)user_context;

  if (event->chan_id >= 0 && event->chan_id < 10) {
    const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
    __atomic_store_n(&g_touch_last_inactive_ms[event->chan_id], now_ms,
                     __ATOMIC_RELAXED);
  }
  return false;
}

// ============================================================================
// INICIALIZACAO DO PERIFERICO TOUCH
// ============================================================================
// Cria o controlador e os quatro canais, ativa o filtro do periferico, mede a
// linha de base sem contato e calcula cada limiar como 80% do valor inicial.
// Nesta fase os sensores devem ficar livres para nao deslocar a calibracao.
static void initialize_touch_sensor(void) {
  static touch_sensor_sample_config_t sample_config[TOUCH_SAMPLE_CFG_NUM] = {
      TOUCH_SENSOR_V1_DEFAULT_SAMPLE_CONFIG(TOUCH_MEASUREMENT_DURATION_MS,
                                            TOUCH_VOLT_LIM_L_0V5,
                                            TOUCH_VOLT_LIM_H_1V7)};

  touch_sensor_config_t sensor_config =
      TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(TOUCH_SAMPLE_CFG_NUM, sample_config);
  ESP_ERROR_CHECK(touch_sensor_new_controller(&sensor_config, &g_touch_sensor));

  touch_channel_config_t channel_config = {
      .abs_active_thresh = {1000U},
      .charge_speed = TOUCH_CHARGE_SPEED_7,
      .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
      .group = TOUCH_CHAN_TRIG_GROUP_BOTH,
  };

  for (uint32_t i = 0; i < TOUCH_CHANNEL_COUNT; ++i) {
    ESP_ERROR_CHECK(
        touch_sensor_new_channel(g_touch_sensor, TOUCH_INPUTS[i].channel_id,
                                 &channel_config, &g_touch_channels[i]));

    touch_chan_info_t channel_info = {0};
    ESP_ERROR_CHECK(
        touch_sensor_get_channel_info(g_touch_channels[i], &channel_info));
    configASSERT(channel_info.chan_gpio == TOUCH_INPUTS[i].gpio_num);
  }

  touch_sensor_filter_config_t filter_config =
      TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
  ESP_ERROR_CHECK(touch_sensor_config_filter(g_touch_sensor, &filter_config));

  // A primeira leitura inicializa os dados filtrados. Nao toque nos pinos
  // durante esta etapa de calibracao.
  printf("TOUCH | calibrando: mantenha os sensores livres\n");
  ESP_ERROR_CHECK(touch_sensor_enable(g_touch_sensor));
  for (uint32_t scan = 0; scan < TOUCH_INITIAL_SCAN_COUNT; ++scan) {
    ESP_ERROR_CHECK(
        touch_sensor_trigger_oneshot_scanning(g_touch_sensor, 2000));
  }
  ESP_ERROR_CHECK(touch_sensor_disable(g_touch_sensor));

  for (uint32_t i = 0; i < TOUCH_CHANNEL_COUNT; ++i) {
    uint32_t untouched_value[TOUCH_SAMPLE_CFG_NUM] = {0};
    ESP_ERROR_CHECK(touch_channel_read_data(
        g_touch_channels[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, untouched_value));
    configASSERT(untouched_value[0] > 0U);

    channel_config.abs_active_thresh[0] =
        (untouched_value[0] * TOUCH_THRESHOLD_PERCENT) / 100U;
    ESP_ERROR_CHECK(
        touch_sensor_reconfig_channel(g_touch_channels[i], &channel_config));

    printf("TOUCH %c | T%d | GPIO%d | base=%" PRIu32 " | limite=%" PRIu32 "\n",
           TOUCH_INPUTS[i].label, TOUCH_INPUTS[i].channel_id,
           TOUCH_INPUTS[i].gpio_num, untouched_value[0],
           channel_config.abs_active_thresh[0]);
  }

  const touch_event_callbacks_t callbacks = {
      .on_inactive = touch_on_inactive,
      .on_hw_active = touch_on_hardware_active,
  };
  ESP_ERROR_CHECK(
      touch_sensor_register_callbacks(g_touch_sensor, &callbacks, NULL));
}

// Registra as callbacks e inicia a varredura continua somente depois que filas
// e tarefas consumidoras ja foram criadas em app_main.
static void start_touch_sensor(void) {
  ESP_ERROR_CHECK(touch_sensor_enable(g_touch_sensor));
  ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(g_touch_sensor));
  printf("TOUCH | um evento por contato | debounce=%lld ms | "
         "liberacao_estavel=%u ms\n",
         TOUCH_DEBOUNCE_US / 1000LL, TOUCH_RELEASE_STABLE_MS);
}

#endif

// ============================================================================
// TAREFAS DO AUTOPILOTO SIMULADO
// ============================================================================
// FUS_IMU - produtora periodica a 200 Hz:
// 1. gera sinais deterministas de giroscopio e acelerometro;
// 2. aplica a perturbacao solicitada pelo Touch A;
// 3. estima roll e pitch pelo filtro complementar e integra yaw;
// 4. executa a carga fixa e publica a amostra mais recente na fila;
// 5. registra execucao, resposta, jitter e eventual miss de 5 ms.
static void release_fus(void *context) {
  (void)context;
  const int64_t event = g_fus_origin_us + (int64_t)(++g_fus_release_number) * FUS_PERIOD_MS * 1000LL;
  if (xQueueSend(g_fus_releases, &event, 0) != pdTRUE)
    experiment_lost(METRIC_FUS, event);
}

static void task_fus_imu(void *argument) {
  (void)argument;

  imu_state_t state = {0};
  g_fus_releases = xQueueCreate(8, sizeof(int64_t));
  configASSERT(g_fus_releases);
  esp_timer_handle_t timer;
  const esp_timer_create_args_t args = {.callback = release_fus, .name = "fus_release"};
  ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
  g_fus_origin_us = esp_timer_get_time();
  ESP_ERROR_CHECK(esp_timer_start_periodic(timer, FUS_PERIOD_MS * 1000LL));

  for (;;) {
    int64_t expected_release_us;
    xQueueReceive(g_fus_releases, &expected_release_us, portMAX_DELAY);
    const int64_t task_start_us = esp_timer_get_time();
    state.sample_time_us = task_start_us;
    state.sequence++;

    bool stress_active = false;
    taskENTER_CRITICAL(&g_state_lock);
    if (__atomic_exchange_n(&g_stress_request, false, __ATOMIC_ACQ_REL)) {
      g_stress_samples_remaining = STRESS_DURATION_SAMPLES;
    }
    if (g_stress_samples_remaining > 0U) {
      g_stress_samples_remaining--;
      stress_active = true;
      if (g_stress_samples_remaining == 0U) {
        __atomic_store_n(&g_stress_active, false, __ATOMIC_RELEASE);
      }
    }
    taskEXIT_CRITICAL(&g_state_lock);

    // Sinais deterministas que alternam o sentido periodicamente.
    float gyro_roll = ((state.sequence / 100U) % 2U == 0U) ? 18.0f : -18.0f;
    float gyro_pitch = ((state.sequence / 150U) % 2U == 0U) ? 10.0f : -10.0f;
    float gyro_yaw = 8.0f;
    float accel_roll = (gyro_roll > 0.0f) ? 5.0f : -5.0f;
    float accel_pitch = (gyro_pitch > 0.0f) ? 3.0f : -3.0f;

    if (stress_active) {
      // Perturbacao deterministica acionada pelo Touch A.
      gyro_roll += 45.0f;
      gyro_pitch -= 35.0f;
      gyro_yaw += 25.0f;
      accel_roll += 12.0f;
      accel_pitch -= 8.0f;
    }

    state.roll =
        COMPLEMENTARY_ALPHA * (state.roll + gyro_roll * IMU_DT_SECONDS) +
        (1.0f - COMPLEMENTARY_ALPHA) * accel_roll;
    state.pitch =
        COMPLEMENTARY_ALPHA * (state.pitch + gyro_pitch * IMU_DT_SECONDS) +
        (1.0f - COMPLEMENTARY_ALPHA) * accel_pitch;
    state.yaw += gyro_yaw * IMU_DT_SECONDS;
    if (state.yaw >= 180.0f) {
      state.yaw -= 360.0f;
    }

    simulate_fixed_work(FUS_WORK_ITERATIONS, state.roll + state.pitch);
    if (stress_active &&
        (state.sequence % STRESS_WORK_EVERY_N_SAMPLES) == 0U) {
      simulate_fixed_work(FUS_STRESS_WORK_ITERATIONS, state.yaw);
    }

    taskENTER_CRITICAL(&g_state_lock);
    g_latest_imu = state;
    taskEXIT_CRITICAL(&g_state_lock);

    // A fila de tamanho 1 sempre contem o estado mais recente.
    imu_state_t replaced;
    if (xQueueReceive(g_imu_queue, &replaced, 0) == pdTRUE) {
      __atomic_fetch_add(&g_ctrl_overwritten, 1U, __ATOMIC_RELAXED);
      experiment_lost(METRIC_CTRL, replaced.sample_time_us);
    }
    xQueueOverwrite(g_imu_queue, &state);
    const int64_t task_end_us = esp_timer_get_time();
    metrics_record_detail(METRIC_FUS, expected_release_us, task_start_us, task_end_us,
                   DEADLINE_FUS_US, "ESP_TIMER", 0, 0, -1, state.sequence);
  }
}

// CTRL_ATT - consumidora encadeada pela FUS_IMU:
// 1. permanece bloqueada, sem ocupar CPU, enquanto g_imu_queue esta vazia;
// 2. compara a atitude medida com a referencia do waypoint;
// 3. executa um PID independente para roll, pitch e yaw;
// 4. mistura as correcoes nos quatro motores logicos;
// 5. impede qualquer religamento dos motores depois do fail-safe.
static void task_ctrl_att(void *argument) {
  (void)argument;

  imu_state_t state;
  uint32_t previous_sequence = 0;
  pid_axis_t pid_roll = {.kp = 1.20f, .ki = 0.10f, .kd = 0.02f};
  pid_axis_t pid_pitch = {.kp = 1.20f, .ki = 0.10f, .kd = 0.02f};
  pid_axis_t pid_yaw = {.kp = 0.60f, .ki = 0.05f, .kd = 0.01f};

  for (;;) {
    if (xQueueReceive(g_imu_queue, &state, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    const int64_t task_start_us = esp_timer_get_time();

    const uint32_t sequence_delta = state.sequence - previous_sequence;
    if (sequence_delta > 1)
      __atomic_fetch_add(&g_ctrl_sequence_gaps, sequence_delta - 1, __ATOMIC_RELAXED);
    previous_sequence = state.sequence;

    attitude_reference_t reference;
    taskENTER_CRITICAL(&g_state_lock);
    reference = g_reference;
    taskEXIT_CRITICAL(&g_state_lock);

    const float roll_control =
        pid_update(&pid_roll, reference.roll, state.roll);
    const float pitch_control =
        pid_update(&pid_pitch, reference.pitch, state.pitch);
    const float yaw_control = pid_update(&pid_yaw, reference.yaw, state.yaw);

    actuator_state_t next_actuators = {
        .motor = {clamp_float(MOTOR_HOVER_PERCENT + roll_control +
                                  pitch_control - yaw_control,
                              MOTOR_MIN_PERCENT, MOTOR_MAX_PERCENT),
                  clamp_float(MOTOR_HOVER_PERCENT - roll_control +
                                  pitch_control + yaw_control,
                              MOTOR_MIN_PERCENT, MOTOR_MAX_PERCENT),
                  clamp_float(MOTOR_HOVER_PERCENT - roll_control -
                                  pitch_control - yaw_control,
                              MOTOR_MIN_PERCENT, MOTOR_MAX_PERCENT),
                  clamp_float(MOTOR_HOVER_PERCENT + roll_control -
                                  pitch_control + yaw_control,
                              MOTOR_MIN_PERCENT, MOTOR_MAX_PERCENT)},
        .source_sequence = state.sequence};

    simulate_fixed_work(CTRL_WORK_ITERATIONS,
                        roll_control + pitch_control + yaw_control);

    taskENTER_CRITICAL(&g_state_lock);
    // Impede que o controle religue os motores depois de um fail-safe.
    const bool fail_safe = g_fail_safe_active;
    if (fail_safe) {
      for (uint32_t i = 0; i < 4U; ++i) {
        g_actuators.motor[i] = 0.0f;
      }
      g_actuators.source_sequence = state.sequence;
    } else {
      g_actuators = next_actuators;
    }
    taskEXIT_CRITICAL(&g_state_lock);

    const int64_t task_end_us = esp_timer_get_time();
    metrics_record_detail(METRIC_CTRL, state.sample_time_us, task_start_us,
                   task_end_us, DEADLINE_CTRL_US, "FUS_QUEUE", 0, 0, -1, state.sequence);
  }
}

// NAV_PLAN - consumidora dos eventos A/B/C:
// EV_NAV avanca a rota e altera a referencia usada pelos PIDs.
// EV_TEL copia uma fotografia coerente e imprime a telemetria.
// EV_STRESS informa que a perturbacao/carga solicitada por A foi iniciada.
// A tarefa e soft real-time: atrasos ocasionais degradam a navegacao, mas nao
// representam a mesma falha imediata de estabilidade/seguranca das hard RT.
static void task_nav_plan(void *argument) {
  (void)argument;

  nav_event_t event;

  for (;;) {
    if (xQueueReceive(g_nav_queue, &event, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    const int64_t task_start_us = esp_timer_get_time();
    const int64_t activation_latency_us = task_start_us - event.event_time_us;

    if (event.type == EV_NAV) {
      uint32_t selected_waypoint;
      bool fail_safe;

      simulate_fixed_work(NAV_WORK_ITERATIONS, (float)g_waypoint_index);

      taskENTER_CRITICAL(&g_state_lock);
      fail_safe = g_fail_safe_active;
      if (!fail_safe) {
        g_waypoint_index = (g_waypoint_index + 1U) %
                           (sizeof(WAYPOINTS) / sizeof(WAYPOINTS[0]));
        g_reference.roll = WAYPOINTS[g_waypoint_index].roll;
        g_reference.pitch = WAYPOINTS[g_waypoint_index].pitch;
        g_reference.yaw = WAYPOINTS[g_waypoint_index].yaw;
      }
      selected_waypoint = g_waypoint_index;
      taskEXIT_CRITICAL(&g_state_lock);

      const int64_t task_end_us = esp_timer_get_time();
      metrics_record(METRIC_NAV, event.event_time_us, task_start_us,
                     task_end_us, DEADLINE_NAV_US, event.source,
                     event.session, event.command_sequence);

      if (fail_safe) {
        printf("NAV | comando ignorado: fail-safe ativo | stress_evento=%s "
               "| latencia=%" PRId64 " us\n",
               event.stress_active_at_event ? "ON" : "OFF",
               activation_latency_us);
      } else {
        printf("NAV | waypoint=%lu | ref=[%.1f, %.1f, %.1f] "
               "| stress_evento=%s | latencia=%" PRId64 " us\n",
               (unsigned long)selected_waypoint,
               WAYPOINTS[selected_waypoint].roll,
               WAYPOINTS[selected_waypoint].pitch,
               WAYPOINTS[selected_waypoint].yaw,
               event.stress_active_at_event ? "ON" : "OFF",
               activation_latency_us);
      }
    } else if (event.type == EV_TEL) {
      imu_state_t imu;
      actuator_state_t actuators;
      uint32_t waypoint;
      uint32_t stress_samples;
      bool fail_safe;

      taskENTER_CRITICAL(&g_state_lock);
      imu = g_latest_imu;
      actuators = g_actuators;
      waypoint = g_waypoint_index;
      stress_samples = g_stress_samples_remaining;
      fail_safe = g_fail_safe_active;
      taskEXIT_CRITICAL(&g_state_lock);

      // A transferencia UART do log fica fora do trecho temporizado. A
      // resposta medida termina quando a fotografia de telemetria esta pronta.
      const int64_t task_end_us = esp_timer_get_time();
      metrics_record(METRIC_NAV, event.event_time_us, task_start_us,
                     task_end_us, DEADLINE_NAV_US, event.source,
                     event.session, event.command_sequence);

      printf("TEL | seq=%lu | waypoint=%lu | rpy=[%.2f, %.2f, %.2f] "
             "| motores=[%.1f, %.1f, %.1f, %.1f] | fail-safe=%s "
             "| stress_evento=%s | stress_restante=%lu "
             "| latencia=%" PRId64 " us | descartados=%lu\n",
             (unsigned long)imu.sequence, (unsigned long)waypoint, imu.roll,
             imu.pitch, imu.yaw, actuators.motor[0], actuators.motor[1],
             actuators.motor[2], actuators.motor[3], fail_safe ? "ON" : "OFF",
             event.stress_active_at_event ? "ON" : "OFF",
             (unsigned long)stress_samples, activation_latency_us,
             (unsigned long)__atomic_load_n(&g_touch_dropped_events,
                                            __ATOMIC_RELAXED));
    } else if (event.type == EV_STRESS) {
      uint32_t stress_samples;
      taskENTER_CRITICAL(&g_state_lock);
      stress_samples = g_stress_samples_remaining;
      taskEXIT_CRITICAL(&g_state_lock);

      metrics_record(METRIC_NAV, event.event_time_us, task_start_us,
                     esp_timer_get_time(), DEADLINE_NAV_US, event.source,
                     event.session, event.command_sequence);

      printf("STRESS | perturbacao e carga extras iniciadas por %u ms "
             "| restantes=%lu amostras | "
             "latencia=%" PRId64 " us\n",
             STRESS_DURATION_SAMPLES * FUS_PERIOD_MS,
             (unsigned long)stress_samples, activation_latency_us);
    }
  }
}

// FAIL_SAFE - consumidora exclusiva do Touch D:
// 1. acorda quando a ISR coloca o timestamp em g_fail_safe_queue;
// 2. trava o estado seguro e zera os quatro motores dentro da secao critica;
// 3. executa a carga fixa e registra a resposta completa contra 10 ms;
// 4. permanece ativo ate a placa ser reiniciada.
// Os motores sao zerados antes da carga e antes do printf.
static void task_fail_safe(void *argument) {
  (void)argument;

  fail_safe_event_t event;

  for (;;) {
    if (xQueueReceive(g_fail_safe_queue, &event, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    const int64_t task_start_us = esp_timer_get_time();

    taskENTER_CRITICAL(&g_state_lock);
    g_fail_safe_active = true;
    for (uint32_t i = 0; i < 4U; ++i) {
      g_actuators.motor[i] = 0.0f;
    }
    const int64_t safe_us = esp_timer_get_time();
    taskEXIT_CRITICAL(&g_state_lock);

    simulate_fixed_work(FS_WORK_ITERATIONS, 0.5f);

    const int64_t task_end_us = esp_timer_get_time();
    metrics_record_detail(METRIC_FAIL_SAFE, event.event_time_us, task_start_us,
                   task_end_us, DEADLINE_FAIL_SAFE_US, event.source,
                   event.session, event.command_sequence, safe_us, 0);

    // O printf ocorre depois da acao critica de seguranca.
    printf("FAIL-SAFE | motores zerados | modo seguro ativado "
           "| latencia_inicio=%" PRId64 " us | resposta=%" PRId64
           " us | acao_segura_us=%" PRId64 " | acao_desde_evento_us=%" PRId64 "\n",
           task_start_us - event.event_time_us,
           task_end_us - event.event_time_us, safe_us, safe_us - event.event_time_us);
  }
}

// ============================================================================
// APRESENTACAO E CONDUCAO DO EXPERIMENTO
// ============================================================================
// Exibe uma leitura humana dos mesmos contadores impressos nas linhas METRIC.
// A sintese roda uma unica vez, fora dos trechos instrumentados das tarefas.
// EMAX e RMAX sao convertidos de microssegundos para milissegundos apenas para
// facilitar a leitura; os valores originais permanecem nas linhas METRIC.
static void print_experiment_summary(
    const task_metrics_t snapshot[METRIC_TASK_COUNT], int64_t report_time_us) {
  static const char *const task_names[METRIC_TASK_COUNT] = {
      "FUS_IMU", "CTRL_ATT", "NAV_PLAN", "FAIL_SAFE", "MONITOR_TASK", "TIME_TASK"};
  static const char *const task_classes[METRIC_TASK_COUNT] = {
      "HARD", "HARD", "SOFT", "HARD", "SOFT", "SOFT"};

  uint64_t total_jobs = 0U;
  uint64_t total_misses = 0U;
  for (uint32_t i = 0; i < METRIC_TASK_COUNT; ++i) {
    total_jobs += snapshot[i].jobs;
    total_misses += snapshot[i].deadline_misses;
  }

  const uint64_t hard_misses =
      snapshot[METRIC_FUS].deadline_misses +
      snapshot[METRIC_CTRL].deadline_misses +
      snapshot[METRIC_FAIL_SAFE].deadline_misses;
  const uint64_t soft_misses = snapshot[METRIC_NAV].deadline_misses +
                               snapshot[METRIC_MONITOR].deadline_misses +
                               snapshot[METRIC_TIME].deadline_misses;
  const uint32_t dropped_events =
      __atomic_load_n(&g_touch_dropped_events, __ATOMIC_RELAXED);

  bool fail_safe;
  uint32_t waypoint;
  taskENTER_CRITICAL(&g_state_lock);
  fail_safe = g_fail_safe_active;
  waypoint = g_waypoint_index;
  taskEXIT_CRITICAL(&g_state_lock);

  printf("\n");
  printf("+=============================================================+\n");
  printf("|                     SINTESE DO ENSAIO                       |\n");
  printf("+=============================================================+\n");
  printf("| CONFIG | politica=%s | %s | cpu=%d MHz | nucleos=%u\n",
         POLICY_NAME, SCHEDULER_NAME, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
         EXPERIMENT_CORE_COUNT);
  printf("| ROTEIRO | CONCLUIDO | tempo=%.1f s | eventos=14/14 | "
         "descartados=%lu\n",
         (double)(report_time_us - g_measurement_start_us) / 1000000.0,
         (unsigned long)dropped_events);
  printf("| ESTADO | fail-safe=%s | waypoint=%lu\n",
         fail_safe ? "ATIVO" : "INATIVO", (unsigned long)waypoint);
  printf("+-------------+------+----------+----------+----------+----------+\n");
  printf("| TAREFA      | TIPO |     JOBS |   MISSES | EMAX(ms) | RMAX(ms) |\n");
  printf("+-------------+------+----------+----------+----------+----------+\n");

  for (uint32_t i = 0; i < METRIC_TASK_COUNT; ++i) {
    printf("| %-11s | %-4s | %8" PRIu64 " | %8" PRIu64
           " | %8.3f | %8.3f |\n",
           task_names[i], task_classes[i], snapshot[i].jobs,
           snapshot[i].deadline_misses,
           (double)snapshot[i].execution_max_us / 1000.0,
           (double)snapshot[i].response_max_us / 1000.0);
  }

  printf("+-------------+------+----------+----------+----------+----------+\n");
  printf("| TOTAL | jobs=%" PRIu64 " | misses=%" PRIu64
         " | hard=%" PRIu64 " | soft=%" PRIu64 "\n",
         total_jobs, total_misses, hard_misses, soft_misses);
  printf("| DEADLINES HARD | %s\n",
         hard_misses == 0U ? "ATENDIDOS NESTE ENSAIO" : "HOUVE PERDAS");
  printf("| DEADLINE SOFT  | %s\n",
         soft_misses == 0U ? "ATENDIDO NESTE ENSAIO" : "HOUVE PERDAS");
  printf("| NOTA | EMAX e RMAX sao maximos observados, nao WCET garantido.\n");
  printf("+=============================================================+\n\n");
}

// A cada 5 s, copia atomicamente os acumuladores e imprime as linhas CSV.
// O snapshot curto evita manter g_metrics_lock durante vTaskGetInfo e printf.
// cpu_pct representa o tempo de CPU da tarefa na ultima janela, enquanto os
// demais campos sao acumulados desde o inicio da medicao.
static void task_metrics(void *argument) {
  (void)argument;

  static const char *const task_names[METRIC_TASK_COUNT] = {
      "FUS_IMU", "CTRL_ATT", "NAV_PLAN", "FAIL_SAFE", "MONITOR_TASK", "TIME_TASK"};
  uint64_t previous_runtime_us[METRIC_TASK_COUNT] = {0};
  uint32_t previous_touch_sequence_length = 0;
  int64_t previous_report_us = g_measurement_start_us;
  bool summary_printed = false;
  TickType_t next_report = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&next_report, pdMS_TO_TICKS(METRICS_PERIOD_MS));

    const int64_t report_time_us = esp_timer_get_time();
    printf("QUEUE_LOSS | ctrl_overwritten=%" PRIu32 " | ctrl_sequence_gaps=%" PRIu32 "\n",
           __atomic_load_n(&g_ctrl_overwritten, __ATOMIC_RELAXED),
           __atomic_load_n(&g_ctrl_sequence_gaps, __ATOMIC_RELAXED));
    const uint64_t window_us =
        nonnegative_delta_us(report_time_us, previous_report_us);
    task_metrics_t snapshot[METRIC_TASK_COUNT];

    portENTER_CRITICAL(&g_metrics_lock);
    for (uint32_t i = 0; i < METRIC_TASK_COUNT; ++i) {
      snapshot[i] = g_metrics[i];
    }
    portEXIT_CRITICAL(&g_metrics_lock);

    for (uint32_t i = 0; i < METRIC_TASK_COUNT; ++i) {
      TaskStatus_t status = {0};
      uint64_t runtime_us = previous_runtime_us[i];
      if (g_measured_task_handles[i] != NULL) {
        vTaskGetInfo(g_measured_task_handles[i], &status, pdFALSE, eInvalid);
        runtime_us = (uint64_t)status.ulRunTimeCounter;
      }

      const uint64_t runtime_delta_us =
          runtime_us >= previous_runtime_us[i]
              ? runtime_us - previous_runtime_us[i]
              : runtime_us;
      previous_runtime_us[i] = runtime_us;

      const task_metrics_t *metric = &snapshot[i];
      const double jobs = (double)metric->jobs;
      const double miss_percent =
          metric->jobs > 0U
              ? 100.0 * (double)metric->deadline_misses / jobs
              : 0.0;
      const double execution_average_us =
          metric->jobs > 0U ? (double)metric->execution_sum_us / jobs : 0.0;
      const double response_average_us =
          metric->jobs > 0U ? (double)metric->response_sum_us / jobs : 0.0;
      const double jitter_average_us =
          metric->jobs > 0U ? (double)metric->jitter_sum_us / jobs : 0.0;
      const double cpu_percent =
          window_us > 0U ? 100.0 * (double)runtime_delta_us / (double)window_us
                         : 0.0;

      printf("METRIC;%.3f;%s;%s;%" PRIu64 ";%" PRIu64
             ";%.3f;%" PRIu64 ";%.1f;%" PRIu64 ";%" PRIu64
             ";%.1f;%" PRIu64 ";%" PRIu64 ";%.1f;%" PRIu64
             ";%.3f;%" PRId64 ";%" PRId64 ";%" PRId64 "\n",
             (double)(report_time_us - g_measurement_start_us) / 1000000.0,
             POLICY_NAME, task_names[i], metric->jobs,
             metric->deadline_misses, miss_percent,
             metric->jobs > 0U ? metric->execution_min_us : 0U,
             execution_average_us, metric->execution_max_us,
             metric->jobs > 0U ? metric->response_min_us : 0U,
             response_average_us, metric->response_max_us,
             metric->start_latency_max_us, jitter_average_us,
             metric->jitter_max_us, cpu_percent, metric->last_event_us,
             metric->last_start_us, metric->last_end_us);
    }

#if CONFIG_M2_INPUT_AUTO
    event_auto_report();
#endif

    const uint32_t touch_sequence_length =
        __atomic_load_n(&g_touch_sequence_length, __ATOMIC_ACQUIRE);
    if (touch_sequence_length != previous_touch_sequence_length) {
      char touch_sequence[TOUCH_SEQUENCE_CAPACITY] = {0};
      const uint32_t copy_length =
          touch_sequence_length < TOUCH_SEQUENCE_CAPACITY - 1U
              ? touch_sequence_length
              : TOUCH_SEQUENCE_CAPACITY - 1U;
      for (uint32_t i = 0; i < copy_length; ++i) {
        touch_sequence[i] = g_touch_sequence[i];
      }

      printf("TOUCH_SEQ;%.3f;%s;A=%lu;B=%lu;C=%lu;D=%lu;descartados=%lu\n",
             (double)(report_time_us - g_measurement_start_us) / 1000000.0,
             touch_sequence,
             (unsigned long)__atomic_load_n(
                 &g_touch_event_count[TOUCH_A_CHANNEL], __ATOMIC_RELAXED),
             (unsigned long)__atomic_load_n(
                 &g_touch_event_count[TOUCH_B_CHANNEL], __ATOMIC_RELAXED),
             (unsigned long)__atomic_load_n(
                 &g_touch_event_count[TOUCH_C_CHANNEL], __ATOMIC_RELAXED),
             (unsigned long)__atomic_load_n(
                 &g_touch_event_count[TOUCH_D_CHANNEL], __ATOMIC_RELAXED),
             (unsigned long)__atomic_load_n(&g_touch_dropped_events,
                                            __ATOMIC_RELAXED));
      previous_touch_sequence_length = touch_sequence_length;
    }

    const bool guide_complete =
        __atomic_load_n(&g_guide_complete, __ATOMIC_ACQUIRE);
    const bool guide_invalid =
        __atomic_load_n(&g_guide_invalid, __ATOMIC_ACQUIRE);
    const int64_t elapsed_us = report_time_us - g_measurement_start_us;
    if (!summary_printed && guide_complete && !guide_invalid &&
        elapsed_us >= EXPERIMENT_MIN_DURATION_US) {
      print_experiment_summary(snapshot, report_time_us);
      summary_printed = true;
    }

    previous_report_us = report_time_us;
  }
}

// Orienta o operador pelo roteiro CBCABCBCBCBCDC e valida cada prefixo.
// A consulta ocorre a cada 50 ms, mas so ha printf quando o estado muda. Um D
// antecipado sempre ativa a seguranca, embora torne o ensaio invalido.
#if !CONFIG_M2_INPUT_PC
static void task_experiment_guide(void *argument) {
  (void)argument;

  uint32_t previous_length = UINT32_MAX;
  bool invalid_reported = false;
  const uint32_t expected_length =
      (uint32_t)(sizeof(GUIDE_EXPECTED_SEQUENCE) - 1U);

  for (;;) {
    const uint32_t length =
        __atomic_load_n(&g_touch_sequence_length, __ATOMIC_ACQUIRE);
    const bool guide_invalid =
        __atomic_load_n(&g_guide_invalid, __ATOMIC_ACQUIRE);

    if (guide_invalid) {
      if (!invalid_reported) {
        const uint32_t position = __atomic_load_n(
            &g_guide_invalid_position, __ATOMIC_RELAXED);
        const char label =
            __atomic_load_n(&g_guide_invalid_label, __ATOMIC_RELAXED);
        printf("GUIA | evento %c inesperado ou antes da instrucao no passo "
               "%lu; o fail-safe continua ativo se o evento foi D; "
               "reinicie a placa para repetir\n",
               label, (unsigned long)position);
        invalid_reported = true;
      }
    } else if (length != previous_length) {
      bool valid_prefix = length <= expected_length;
      const uint32_t compared_length =
          length < expected_length ? length : expected_length;

      for (uint32_t i = 0; i < compared_length; ++i) {
        if (g_touch_sequence[i] != GUIDE_EXPECTED_SEQUENCE[i]) {
          valid_prefix = false;
          break;
        }
      }

      if (!valid_prefix) {
        printf("GUIA | sequencia incorreta; reinicie a placa e repita o "
               "ensaio\n");
      } else if (length == expected_length) {
        const double elapsed_seconds =
            (double)(esp_timer_get_time() - g_measurement_start_us) /
            1000000.0;
        printf("GUIA | sequencia concluida em %.1f s | aguarde uma linha "
               "METRIC com tempo_s >= 30 e a sintese final\n",
               elapsed_seconds);
        __atomic_store_n(&g_guide_complete, true, __ATOMIC_RELEASE);
      } else {
        // Um passo comum libera um evento. No passo conjunto, B e C podem ser
        // detectados na mesma varredura antes que o guia volte a executar.
        const uint32_t next_allowed_length =
            (length == 10U || length == 11U) ? 12U : length + 1U;
        __atomic_store_n(&g_guide_allowed_length, next_allowed_length,
                         __ATOMIC_RELEASE);
#if CONFIG_M2_INPUT_TOUCH
        printf("GUIA | %s\n", GUIDE_PROMPTS[length]);
#else
        printf("GUIA_AUTO | %lu/14 eventos | aguardando roteiro do timer\n",
               (unsigned long)length);
#endif
      }

      previous_length = length;
    }

    vTaskDelay(pdMS_TO_TICKS(GUIDE_POLL_PERIOD_MS));
  }
}

#endif

#if CONFIG_M2_MONITOR_ENABLE
// Comandos de rede chegam em TASK context: APIs normais de fila, nunca FromISR.
static esp_err_t dispatch_pc_command(const monitor_request_t *request) {
  if (request->message.command == MON_BEGIN) {
    if (!experiment_begin(request->received_us)) return ESP_ERR_INVALID_STATE;
#if CONFIG_M2_INPUT_AUTO && CONFIG_M2_AUTO_WAIT_PC
    ESP_ERROR_CHECK(event_auto_trigger());
#endif
    printf("RUN_BEGIN | mono_us=%" PRId64 " | duration_s=%d\n",
           request->received_us, CONFIG_M2_RUN_SECONDS);
    return ESP_OK;
  }
  if (request->message.command == MON_NAV) {
    taskENTER_CRITICAL(&g_state_lock);
    const bool safe = g_fail_safe_active;
    taskEXIT_CRITICAL(&g_state_lock);
    if (safe) return ESP_ERR_INVALID_STATE;
    const nav_event_t event = {.type = EV_NAV,
        .event_time_us = request->received_us,
        .stress_active_at_event = __atomic_load_n(&g_stress_active, __ATOMIC_ACQUIRE),
        .source = request->source, .session = request->message.session,
        .command_sequence = request->message.sequence};
    if (xQueueSend(g_nav_queue, &event, 0) == pdTRUE) return ESP_OK;
    experiment_lost(METRIC_NAV, event.event_time_us);
    return ESP_ERR_TIMEOUT;
  }
  if (request->message.command == MON_FS) {
    const fail_safe_event_t event = {.event_time_us = request->received_us,
        .source = request->source, .session = request->message.session,
        .command_sequence = request->message.sequence};
    if (xQueueSend(g_fail_safe_queue, &event, 0) == pdTRUE) return ESP_OK;
    experiment_lost(METRIC_FAIL_SAFE, event.event_time_us);
    return ESP_ERR_TIMEOUT;
  }
  return ESP_ERR_INVALID_ARG;
}

static void record_monitor_cycle(int64_t event_us, int64_t start_us, int64_t end_us) {
  metrics_record(METRIC_MONITOR, event_us, start_us, end_us,
                 MONITOR_PERIOD_US, "MON_RELEASE", 0, 0);
}
#endif

// ============================================================================
// PONTO DE ENTRADA
#if CONFIG_M2_TIME_ENABLE
static void record_time_cycle(int64_t event_us, int64_t start_us, int64_t end_us) {
  metrics_record(METRIC_TIME, event_us, start_us, end_us,
                 TIME_TASK_PERIOD_US, "TIME_RELEASE", 0, 0);
}
#endif
// ============================================================================
// Ordem de inicializacao:
// 1. cria as tres filas;
// 2. no modo fisico, configura e calibra touch ainda sem interrupcoes;
// 3. cria primeiro todas as tarefas consumidoras e auxiliares;
// 4. cria FUS_IMU, a produtora periodica;
// 5. inicia a fonte selecionada e imprime a configuracao efetivamente compilada.
// Essa ordem impede eventos enviados para filas/tasks ainda inexistentes.
void app_main(void) {
  g_imu_queue = xQueueCreate(1U, sizeof(imu_state_t));
  g_nav_queue = xQueueCreate(8U, sizeof(nav_event_t));
  g_fail_safe_queue = xQueueCreate(1U, sizeof(fail_safe_event_t));

  configASSERT(g_imu_queue != NULL);
  configASSERT(g_nav_queue != NULL);
  configASSERT(g_fail_safe_queue != NULL);

  // Calibra os canais antes de iniciar a varredura continua.
#if CONFIG_M2_INPUT_TOUCH
  initialize_touch_sensor();
#endif
  g_measurement_start_us = esp_timer_get_time();
  printf("MEASUREMENT_ORIGIN_US=%" PRId64 "\n", g_measurement_start_us);

  // Primeiro sao criados os consumidores, que bloqueiam aguardando eventos.
  BaseType_t created =
      xTaskCreatePinnedToCore(task_fail_safe, "FS_TASK", TASK_STACK_SIZE, NULL,
                              PRIO_FAIL_SAFE,
                              &g_measured_task_handles[METRIC_FAIL_SAFE], 0);
  configASSERT(created == pdPASS);

  created = xTaskCreatePinnedToCore(task_ctrl_att, "CTRL_ATT", TASK_STACK_SIZE,
                                    NULL, PRIO_CTRL_ATT,
                                    &g_measured_task_handles[METRIC_CTRL], 0);
  configASSERT(created == pdPASS);

  created = xTaskCreatePinnedToCore(task_nav_plan, "NAV_PLAN", TASK_STACK_SIZE,
                                    NULL, PRIO_NAV_PLAN,
                                    &g_measured_task_handles[METRIC_NAV], 0);
  configASSERT(created == pdPASS);

  created = xTaskCreatePinnedToCore(task_metrics, "METRICS", TASK_STACK_SIZE,
                                    NULL, PRIO_METRICS, NULL, AUXILIARY_CORE);
  configASSERT(created == pdPASS);

#if !CONFIG_M2_INPUT_PC
  created = xTaskCreatePinnedToCore(task_experiment_guide, "EXP_GUIDE",
                                    TASK_STACK_SIZE, NULL, PRIO_METRICS, NULL,
                                    AUXILIARY_CORE);
  configASSERT(created == pdPASS);

#endif
#if CONFIG_M2_MONITOR_ENABLE
  ESP_ERROR_CHECK(monitor_start(dispatch_pc_command, record_monitor_cycle,
                                &g_measured_task_handles[METRIC_MONITOR]));
#endif
#if CONFIG_M2_TIME_ENABLE
  ESP_ERROR_CHECK(time_task_start(record_time_cycle,
                                 &g_measured_task_handles[METRIC_TIME]));
#endif

  // A produtora e criada por ultimo para nao perder a primeira amostra.
  created = xTaskCreatePinnedToCore(task_fus_imu, "FUS_IMU", TASK_STACK_SIZE,
                                    NULL, PRIO_FUS_IMU,
                                    &g_measured_task_handles[METRIC_FUS], 0);
  configASSERT(created == pdPASS);

  // As interrupcoes so comecam depois que todas as consumidoras existem.
#if CONFIG_M2_INPUT_TOUCH
  start_touch_sensor();
#elif CONFIG_M2_INPUT_AUTO
  ESP_ERROR_CHECK(event_auto_start(dispatch_inputs_from_isr));
#endif

  printf("POLITICA | %s | FS=%u | FUS=%u | CTRL=%u | NAV=%u\n", POLICY_NAME,
         PRIO_FAIL_SAFE, PRIO_FUS_IMU, PRIO_CTRL_ATT, PRIO_NAV_PLAN);
  printf("CONFIG | escalonamento=%s | nucleos=%u | cpu=%d MHz | "
         "preemption=%d | slicing=%d | tick_hz=%d\n",
         SCHEDULER_NAME, EXPERIMENT_CORE_COUNT,
         CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, configUSE_PREEMPTION,
         configUSE_TIME_SLICING, CONFIG_FREERTOS_HZ);
#if CONFIG_M2_INPUT_AUTO
  printf("INPUT | fonte=GPTIMER_ISR | eventos sinteticos A/B/C/D | "
         "nao mede contato fisico\n");
  printf("AUTO_EVENT;fonte;passo;mascara;planejado_timer_us;"
         "observado_timer_us;evento_isr_us;atraso_alarme_us\n");
#elif CONFIG_M2_INPUT_TOUCH
  printf("INPUT | fonte=TOUCH_ISR | pads fisicos | "
         "timestamp na entrada do callback\n");
#else
  printf("INPUT | fonte=PC_NETWORK | comandos NAV/FS | sem roteiro automatico\n");
#endif
  printf("METRIC;tempo_s;politica;tarefa;jobs;misses;miss_pct;exec_min_us;"
         "exec_avg_us;exec_max_us;resp_min_us;resp_avg_us;resp_max_us;"
         "lat_inicio_max_us;jitter_avg_us;jitter_max_us;cpu_pct;evento_us;"
         "inicio_us;fim_us\n");
  printf("DRONE | simulacao iniciada | FUS_IMU=%u ms | metricas=%u ms\n",
         FUS_PERIOD_MS, METRICS_PERIOD_MS);
}

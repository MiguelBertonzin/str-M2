# Estímulos automáticos mantendo a lógica A/B/C/D

Atualização M2-METRICS-2: por padrão o GPTimer aguarda BEGIN do PC, após rede e
SNTP. A agenda abaixo continua igual em relação ao início do timer. O tratamento
de A/STRESS também passou a gerar métrica NAV (13 jobs do roteiro, antes 12).
Referência operacional: [ensaios_M2](../ensaios_M2/README.md). As limitações
históricas no final deste documento motivaram as correções desta revisão.

## O que é preservado

O modo `GPTIMER_ISR` chama o mesmo despachante lógico que o modo `TOUCH_ISR`:

```text
Touch físico -> callback ISR -> debounce -----+
                                             +-> eventos A/B/C/D -> filas -> tarefas
GPTimer -> callback ISR -> agenda automática -+
```

A continua solicitando perturbação/carga na próxima FUS; B altera waypoint;
C prepara telemetria; D acorda a tarefa de segurança por fila exclusiva.
As APIs `xQueueSendFromISR`/`xQueueOverwriteFromISR` são chamadas em contexto de
interrupção real, e o retorno solicita yield ao driver quando necessário.
O guia não controla o relógio do roteiro automático.

Fontes são mutuamente exclusivas por menuconfig, para evitar misturar eventos
físicos e sintéticos inadvertidamente. No modo físico permanecem calibração,
limiar, filtro, debounce e rearme originais. No automático o sensor não é iniciado;
os eventos são injetados como contatos lógicos já aceitos, sem simular capacitância.

## Agenda por boot

Os tempos são relativos ao início do GPTimer de 1 MHz, não ao instante de boot,
nem à origem de `tempo_s` das métricas. Os dois relógios são identificados no log.

| Tempo do timer (s) | Evento |
|---|---|
| 2 | C |
| 4 | B |
| 6 | C |
| 8 | A: inicia 2000 amostras de carga, nominalmente 10 s |
| 10 | B |
| 12 | C |
| 14 | B |
| 16 | C |
| 18 | B |
| 20 | C |
| 22 | B+C no mesmo callback, enfileirados nessa ordem |
| 24 | D: emergência |
| 26 | C: observar estado seguro |

São 13 alarmes e 14 eventos: sequência `CBCABCBCBCBCDC`, A=1, B=5, C=7, D=1.
Na revisão atual, há 13 jobs NAV instrumentados (5 B + 7 C + 1 A/STRESS).
Na base anterior, A não era contabilizado, resultando em 12 jobs. Mesmo B+C no mesmo alarme passa por operações
sequenciais de software; não significa duas tarefas executando simultaneamente.

O próximo alarme usa instante absoluto desde o início do timer, evitando acumular
o atraso de cada callback. A ISR pode atrasar por seções críticas/interrupções:
o código registra esse atraso, não promete eventos sem jitter. Ao final o timer
é parado; não ocorre reinício automático do roteiro nem desbloqueio do fail-safe.

## Registros e interpretação

- `INPUT`: `GPTIMER_ISR` ou `TOUCH_ISR` para identificar a fonte.
- `AUTO_EVENT`: passo, máscara dos canais, instante planejado no contador,
  contador observado, timestamp monotônico da ISR e atraso do alarme.
- `TOUCH_SEQ`: nome histórico mantido; sequência e contagens dos eventos lógicos
  de ambas as fontes. Interprete sempre junto de `INPUT`.
- `METRIC`: acumuladores M1 e último evento/início/fim da tarefa.
- `AUTO_ERROR`: falha do timer, invalida a coleta.

As linhas AUTO são gravadas em buffer fixo na ISR e impressas pela tarefa auxiliar,
sem `printf` na interrupção. No modo automático, `inicio - evento` mede a latência
desde o callback GPTimer; `fim - evento` mede a resposta desde esse mesmo ponto.
O atraso até entrar no callback aparece separadamente em `atraso_alarme_us`.

No modo físico, o timestamp também começa no callback, portanto nem a M1 media
o tempo completo desde o contato do dedo, anterior à detecção por hardware.
Fases de chegada, filtro, debounce, calibração e custo do driver são diferentes
entre os dois modos. **Equivalência funcional não implica equivalência temporal.**

O PDF M2, página 5, exige sensores touch para gerar interrupções. A automação
acelera desenvolvimento e ensaios controlados; não comprova sozinha esse item.
Preserve uma demonstração física ou confirme com o professor se ele aceita a
substituição na avaliação. Não misture os dois conjuntos de resultados.

## Instrumentação atual e limites

A revisão M2-METRICS-2 usa liberações nominais da FUS por esp_timer, registra a ação
segura separadamente do término de FS e contabiliza perdas na fila FUS→CTRL.
Também calcula HWM(99%) por histograma e (9,10)-firm localmente. A auditoria
importada descreve as limitações históricas que motivaram essas mudanças.
O modo cooperativo conserva yields explícitos na saída da ISR, como na M1.
Não execute gravações de flash durante ensaios: o modo GPTimer atual usa o padrão
sem callback cache-safe. Repetições e fases distintas são necessárias para ampliar
a cobertura temporal.

Referências consultadas: [GPTimer da Espressif](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/gptimer.html)
e [touch capacitivo](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/cap_touch_sens.html),
além dos headers e exemplo GPTimer do SDK 6.1 instalado.

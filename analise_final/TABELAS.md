# Tabelas consolidadas da campanha AGENDA_60S_R2

Cinco repetições de 60 s por cenário. Valores derivados dos acumuladores locais e dos logs do PC.

## Comparação por cenário

| Cenário | Hard: violações | Hard: perdas | Hard total | Soft: violações | PING médio (ms) | PING p99 (ms) | PING máximo (ms) |
|---|---|---|---|---|---|---|---|
| M_UDP | 0 | 0 | 0 | 0 | 513,888 | 995,775 | 1013,405 |
| M_TCP | 13 | 7 | 20 | 0 | 529,745 | 1030,689 | 2326,991 |
| P_UDP | 1856 | 55 | 1911 | 75 | 647,473 | 1021,700 | 1026,376 |
| P_TCP | 2802 | 279 | 3081 | 75 | 684,204 | 1995,784 | 1998,684 |

PING: 145 amostras por cenário. Percentis de RTT usam nearest-rank sobre as amostras dos cinco ensaios; essas amostras não são 145 repetições independentes. Dispersão entre as cinco execuções em cenarios.csv.

## Resposta, execução e cumprimento de prazos por tarefa

| Cenário | Tarefa | D (ms) | WCRT (ms) | C decorrido máx. (ms) | Misses | Perdas | Misses / jobs (%) |
|---|---|---|---|---|---|---|---|
| M_UDP | FUS_IMU | 5 | 4,482 | 3,619 | 0 | 0 | 0,000 |
| M_UDP | CTRL_ATT | 5 | 4,415 | 2,993 | 0 | 0 | 0,000 |
| M_UDP | NAV_PLAN | 20 | 9,924 | 9,847 | 0 | 0 | 0,000 |
| M_UDP | FAIL_SAFE | 10 | 1,608 | 1,423 | 0 | 0 | 0,000 |
| M_UDP | MONITOR_TASK | 500 | 27,614 | 26,281 | 0 | 0 | 0,000 |
| M_UDP | TIME_TASK | 1000 | 179,253 | 179,188 | 0 | 0 | 0,000 |
| M_TCP | FUS_IMU | 5 | 5,623 | 5,331 | 3 | 0 | 0,005 |
| M_TCP | CTRL_ATT | 5 | 7,012 | 6,309 | 10 | 7 | 0,017 |
| M_TCP | NAV_PLAN | 20 | 10,428 | 9,790 | 0 | 0 | 0,000 |
| M_TCP | FAIL_SAFE | 10 | 2,983 | 2,902 | 0 | 0 | 0,000 |
| M_TCP | MONITOR_TASK | 500 | 25,084 | 25,026 | 0 | 0 | 0,000 |
| M_TCP | TIME_TASK | 1000 | 179,929 | 179,871 | 0 | 0 | 0,000 |
| P_UDP | FUS_IMU | 5 | 6,739 | 6,655 | 71 | 0 | 0,118 |
| P_UDP | CTRL_ATT | 5 | 14,461 | 10,213 | 1765 | 55 | 2,944 |
| P_UDP | NAV_PLAN | 20 | 75,431 | 73,742 | 75 | 0 | 68,182 |
| P_UDP | FAIL_SAFE | 10 | 25,712 | 20,655 | 20 | 0 | 100,000 |
| P_UDP | MONITOR_TASK | 500 | 109,556 | 100,188 | 0 | 0 | 0,000 |
| P_UDP | TIME_TASK | 1000 | 135,901 | 135,788 | 0 | 0 | 0,000 |
| P_TCP | FUS_IMU | 5 | 8,167 | 8,096 | 350 | 0 | 0,583 |
| P_TCP | CTRL_ATT | 5 | 17,134 | 15,023 | 2432 | 279 | 4,072 |
| P_TCP | NAV_PLAN | 20 | 83,608 | 75,250 | 75 | 0 | 68,182 |
| P_TCP | FAIL_SAFE | 10 | 25,259 | 20,687 | 20 | 0 | 100,000 |
| P_TCP | MONITOR_TASK | 500 | 105,096 | 104,993 | 0 | 0 | 0,000 |
| P_TCP | TIME_TASK | 1000 | 204,695 | 204,586 | 0 | 0 | 0,000 |

WCRT/HWM100 é o maior valor observado. C decorrido inclui eventuais preempções e esperas; não é WCET formal.

## HWM99 por execução

Não foi calculado um HWM99 global a partir dos percentis individuais. Os limites exatos reportados por cada histograma estão em tarefas_por_repeticao.csv. Os campos menor/maior limite de tarefas_por_cenario.csv descrevem apenas o envelope dos cinco intervalos, não um percentil agregado.

## Evidência de integridade

integridade_dados.csv registra SHA256 dos arquivos usados na geração. Caso a publicação remova dados pessoais de logs, regenerar este inventário sobre as cópias publicadas; os arquivos locais originais permanecem preservados.

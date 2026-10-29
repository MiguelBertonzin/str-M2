# P_TCP_R01

Estado da coleta: **COLETA_CONFERIDA**.

Campanha: AGENDA_60S_R2; coletor: M2-AGENDA-60S-2.

| Tarefa | Jobs | Misses | Perdidos | WCRT (ms) | HWM99 intervalo (ms) |
|---|---:|---:|---:|---:|---|
| FUS_IMU | 12000 | 73 | 0 | 7.218 | 4.3 .. 4.399 |
| CTRL_ATT | 11945 | 496 | 55 | 13.574 | 7.6 .. 7.699 |
| NAV_PLAN | 22 | 15 | 0 | 70.613 | 70.6 .. 70.613 |
| FAIL_SAFE | 4 | 4 | 0 | 25.259 | 25.2 .. 25.259 |
| MONITOR_TASK | 120 | 0 | 0 | 99.49 | 99.1 .. 99.199 |
| TIME_TASK | 60 | 0 | 0 | 189.731 | 102.4 .. 189.731 |

HWM100 = WCRT observado. HWM99 usa histograma de 100 us; o intervalo e um limite, nao um percentil exato.
Os acumuladores incluem todos os jobs validos da janela; os logs individuais FUS/CTRL sao amostrados.

Falhas hard (misses + perdidos + invalidos): 628. Soft: 15.
RTT: 42 respostas; media=697.7408523809523 ms; max=1921.8116 ms.
RTT inclui monitor e transporte; sem retry de aplicacao nos comandos da agenda.

## Conferencias

- Agenda oferecida conforme horarios: True; envios 42/42; ACKs ausentes 0; tardios 0.
- Respostas por comando: {'PING:OK': 29, 'NAV:QUEUED': 9, 'NAV:SAFE_ACTIVE': 1, 'FS:QUEUED': 3}.
- Nenhuma inconsistencia automatica detectada; revisar logs e contexto antes do relatorio.

Detalhes de CPU e (9,10)-firm por tarefa em tarefas.csv; transmissao e descartes em resumo.json.
Perdas tornam (m,k) sobre ativacoes inconclusivo. Uma coleta conferida pode ter deadlines descumpridos.

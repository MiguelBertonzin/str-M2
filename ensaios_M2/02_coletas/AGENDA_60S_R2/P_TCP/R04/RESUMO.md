# P_TCP_R04

Estado da coleta: **COLETA_CONFERIDA**.

Campanha: AGENDA_60S_R2; coletor: M2-AGENDA-60S-2.

| Tarefa | Jobs | Misses | Perdidos | WCRT (ms) | HWM99 intervalo (ms) |
|---|---:|---:|---:|---:|---|
| FUS_IMU | 12000 | 87 | 0 | 8.095 | 4.5 .. 4.599 |
| CTRL_ATT | 11934 | 492 | 66 | 17.134 | 7.6 .. 7.699 |
| NAV_PLAN | 22 | 15 | 0 | 83.608 | 83.6 .. 83.608 |
| FAIL_SAFE | 4 | 4 | 0 | 24.951 | 24.9 .. 24.951 |
| MONITOR_TASK | 120 | 0 | 0 | 104.148 | 89.3 .. 89.399 |
| TIME_TASK | 60 | 0 | 0 | 184.656 | 102.4 .. 184.656 |

HWM100 = WCRT observado. HWM99 usa histograma de 100 us; o intervalo e um limite, nao um percentil exato.
Os acumuladores incluem todos os jobs validos da janela; os logs individuais FUS/CTRL sao amostrados.

Falhas hard (misses + perdidos + invalidos): 649. Soft: 15.
RTT: 42 respostas; media=701.6567833333334 ms; max=1998.684 ms.
RTT inclui monitor e transporte; sem retry de aplicacao nos comandos da agenda.

## Conferencias

- Agenda oferecida conforme horarios: True; envios 42/42; ACKs ausentes 0; tardios 0.
- Respostas por comando: {'PING:OK': 29, 'NAV:QUEUED': 9, 'NAV:SAFE_ACTIVE': 1, 'FS:QUEUED': 3}.
- Nenhuma inconsistencia automatica detectada; revisar logs e contexto antes do relatorio.

Detalhes de CPU e (9,10)-firm por tarefa em tarefas.csv; transmissao e descartes em resumo.json.
Perdas tornam (m,k) sobre ativacoes inconclusivo. Uma coleta conferida pode ter deadlines descumpridos.

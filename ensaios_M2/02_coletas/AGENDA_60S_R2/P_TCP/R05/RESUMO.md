# P_TCP_R05

Estado da coleta: **COLETA_CONFERIDA**.

Campanha: AGENDA_60S_R2; coletor: M2-AGENDA-60S-2.

| Tarefa | Jobs | Misses | Perdidos | WCRT (ms) | HWM99 intervalo (ms) |
|---|---:|---:|---:|---:|---|
| FUS_IMU | 12000 | 63 | 0 | 8.167 | 4.5 .. 4.599 |
| CTRL_ATT | 11945 | 482 | 55 | 14.017 | 7.6 .. 7.699 |
| NAV_PLAN | 22 | 15 | 0 | 80.489 | 80.4 .. 80.489 |
| FAIL_SAFE | 4 | 4 | 0 | 15.756 | 15.7 .. 15.756 |
| MONITOR_TASK | 120 | 0 | 0 | 105.096 | 90.1 .. 90.199 |
| TIME_TASK | 60 | 0 | 0 | 185.916 | 102.4 .. 185.916 |

HWM100 = WCRT observado. HWM99 usa histograma de 100 us; o intervalo e um limite, nao um percentil exato.
Os acumuladores incluem todos os jobs validos da janela; os logs individuais FUS/CTRL sao amostrados.

Falhas hard (misses + perdidos + invalidos): 604. Soft: 15.
RTT: 42 respostas; media=869.5757809523809 ms; max=2001.7117 ms.
RTT inclui monitor e transporte; sem retry de aplicacao nos comandos da agenda.

## Conferencias

- Agenda oferecida conforme horarios: True; envios 42/42; ACKs ausentes 0; tardios 0.
- Respostas por comando: {'PING:OK': 29, 'NAV:QUEUED': 9, 'NAV:SAFE_ACTIVE': 1, 'FS:QUEUED': 3}.
- Nenhuma inconsistencia automatica detectada; revisar logs e contexto antes do relatorio.

Detalhes de CPU e (9,10)-firm por tarefa em tarefas.csv; transmissao e descartes em resumo.json.
Perdas tornam (m,k) sobre ativacoes inconclusivo. Uma coleta conferida pode ter deadlines descumpridos.

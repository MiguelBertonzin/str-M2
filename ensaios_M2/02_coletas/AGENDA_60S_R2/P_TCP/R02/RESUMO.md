# P_TCP_R02

Estado da coleta: **COLETA_CONFERIDA**.

Campanha: AGENDA_60S_R2; coletor: M2-AGENDA-60S-2.

| Tarefa | Jobs | Misses | Perdidos | WCRT (ms) | HWM99 intervalo (ms) |
|---|---:|---:|---:|---:|---|
| FUS_IMU | 12000 | 50 | 0 | 7.65 | 4.2 .. 4.299 |
| CTRL_ATT | 11960 | 470 | 40 | 14.423 | 7.6 .. 7.699 |
| NAV_PLAN | 22 | 15 | 0 | 74.9 | 74.9 .. 74.9 |
| FAIL_SAFE | 4 | 4 | 0 | 24.774 | 24.7 .. 24.774 |
| MONITOR_TASK | 120 | 0 | 0 | 94.011 | 84.4 .. 84.499 |
| TIME_TASK | 60 | 0 | 0 | 204.695 | 102.4 .. 204.695 |

HWM100 = WCRT observado. HWM99 usa histograma de 100 us; o intervalo e um limite, nao um percentil exato.
Os acumuladores incluem todos os jobs validos da janela; os logs individuais FUS/CTRL sao amostrados.

Falhas hard (misses + perdidos + invalidos): 564. Soft: 15.
RTT: 42 respostas; media=782.6971166666667 ms; max=1509.3477 ms.
RTT inclui monitor e transporte; sem retry de aplicacao nos comandos da agenda.

## Conferencias

- Agenda oferecida conforme horarios: True; envios 42/42; ACKs ausentes 0; tardios 0.
- Respostas por comando: {'PING:OK': 29, 'NAV:QUEUED': 9, 'NAV:SAFE_ACTIVE': 1, 'FS:QUEUED': 3}.
- Nenhuma inconsistencia automatica detectada; revisar logs e contexto antes do relatorio.

Detalhes de CPU e (9,10)-firm por tarefa em tarefas.csv; transmissao e descartes em resumo.json.
Perdas tornam (m,k) sobre ativacoes inconclusivo. Uma coleta conferida pode ter deadlines descumpridos.

# P_UDP_R01

Estado da coleta: **REVISAR**.

Campanha: AGENDA_60S_R2; coletor: M2-AGENDA-60S-2.

| Tarefa | Jobs | Misses | Perdidos | WCRT (ms) | HWM99 intervalo (ms) |
|---|---:|---:|---:|---:|---|
| FUS_IMU | 11999 | 18 | 0 | 7.148 | 4.1 .. 4.199 |
| CTRL_ATT | 11985 | 387 | 14 | 11.645 | 7.5 .. 7.599 |
| NAV_PLAN | 22 | 15 | 0 | 78.747 | 78.7 .. 78.747 |
| FAIL_SAFE | 4 | 4 | 0 | 24.17 | 24.1 .. 24.17 |
| MONITOR_TASK | 120 | 0 | 0 | 105.288 | 95.1 .. 95.199 |
| TIME_TASK | 60 | 0 | 0 | 140.005 | 102.4 .. 140.005 |

HWM100 = WCRT observado. HWM99 usa histograma de 100 us; o intervalo e um limite, nao um percentil exato.
Os acumuladores incluem todos os jobs validos da janela; os logs individuais FUS/CTRL sao amostrados.

Falhas hard (misses + perdidos + invalidos): 423. Soft: 15.
RTT: 42 respostas; media=671.0319761904761 ms; max=1027.0615 ms.
RTT inclui monitor e transporte; sem retry de aplicacao nos comandos da agenda.

## Conferencias

- Agenda oferecida conforme horarios: True; envios 42/42; ACKs ausentes 0; tardios 0.
- Respostas por comando: {'PING:OK': 29, 'NAV:QUEUED': 9, 'NAV:SAFE_ACTIVE': 1, 'FS:QUEUED': 3}.
- NAV_PLAN: logs individuais de 21/22 jobs executados; verificar perda de registros.

Detalhes de CPU e (9,10)-firm por tarefa em tarefas.csv; transmissao e descartes em resumo.json.
Perdas tornam (m,k) sobre ativacoes inconclusivo. Uma coleta conferida pode ter deadlines descumpridos.

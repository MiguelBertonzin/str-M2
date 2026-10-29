# P_UDP_R03

Estado da coleta: **COLETA_CONFERIDA**.

Campanha: AGENDA_60S_R2; coletor: M2-AGENDA-60S-2.

| Tarefa | Jobs | Misses | Perdidos | WCRT (ms) | HWM99 intervalo (ms) |
|---|---:|---:|---:|---:|---|
| FUS_IMU | 12000 | 20 | 0 | 6.349 | 3.8 .. 3.899 |
| CTRL_ATT | 11984 | 344 | 16 | 14.461 | 7.4 .. 7.499 |
| NAV_PLAN | 22 | 15 | 0 | 75.431 | 75.4 .. 75.431 |
| FAIL_SAFE | 4 | 4 | 0 | 25.712 | 25.7 .. 25.712 |
| MONITOR_TASK | 120 | 0 | 0 | 94.929 | 94.9 .. 94.929 |
| TIME_TASK | 60 | 0 | 0 | 14.836 | 14.8 .. 14.836 |

HWM100 = WCRT observado. HWM99 usa histograma de 100 us; o intervalo e um limite, nao um percentil exato.
Os acumuladores incluem todos os jobs validos da janela; os logs individuais FUS/CTRL sao amostrados.

Falhas hard (misses + perdidos + invalidos): 384. Soft: 15.
RTT: 42 respostas; media=657.4350142857143 ms; max=1026.3764 ms.
RTT inclui monitor e transporte; sem retry de aplicacao nos comandos da agenda.

## Conferencias

- Agenda oferecida conforme horarios: True; envios 42/42; ACKs ausentes 0; tardios 0.
- Respostas por comando: {'PING:OK': 29, 'NAV:QUEUED': 9, 'NAV:SAFE_ACTIVE': 1, 'FS:QUEUED': 3}.
- Nenhuma inconsistencia automatica detectada; revisar logs e contexto antes do relatorio.

Detalhes de CPU e (9,10)-firm por tarefa em tarefas.csv; transmissao e descartes em resumo.json.
Perdas tornam (m,k) sobre ativacoes inconclusivo. Uma coleta conferida pode ter deadlines descumpridos.

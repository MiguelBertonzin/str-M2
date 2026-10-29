# P_TCP_R01

Estado da coleta: **COLETA_CONFERIDA**.

Campanha: AGENDA_60S_R2; coletor: M2-AGENDA-60S-2.

| Tarefa | Jobs | Misses | Perdidos | WCRT (ms) | HWM99 intervalo (ms) |
|---|---:|---:|---:|---:|---|
| FUS_IMU | 12000 | 99 | 0 | 8.246 | 4.8 .. 4.899 |
| CTRL_ATT | 11914 | 445 | 86 | 14.178 | 7.6 .. 7.699 |
| NAV_PLAN | 22 | 15 | 0 | 75.055 | 75.0 .. 75.055 |
| FAIL_SAFE | 4 | 4 | 0 | 24.837 | 24.8 .. 24.837 |
| MONITOR_TASK | 120 | 0 | 0 | 100.663 | 100.3 .. 100.399 |
| TIME_TASK | 60 | 0 | 0 | 199.692 | 102.4 .. 199.692 |

HWM100 = WCRT observado. HWM99 usa histograma de 100 us; o intervalo e um limite, nao um percentil exato.
Os acumuladores incluem todos os jobs validos da janela; os logs individuais FUS/CTRL sao amostrados.

Falhas hard (misses + perdidos + invalidos): 634. Soft: 15.
RTT: 42 respostas; media=789.2222142857142 ms; max=2002.412 ms.
RTT inclui monitor e transporte; sem retry de aplicacao nos comandos da agenda.

## Conferencias

- Agenda oferecida conforme horarios: True; envios 42/42; ACKs ausentes 0; tardios 0.
- Respostas por comando: {'PING:OK': 29, 'NAV:QUEUED': 9, 'NAV:SAFE_ACTIVE': 1, 'FS:QUEUED': 3}.
- Nenhuma inconsistencia automatica detectada; revisar logs e contexto antes do relatorio.

Detalhes de CPU e (9,10)-firm por tarefa em tarefas.csv; transmissao e descartes em resumo.json.
Perdas tornam (m,k) sobre ativacoes inconclusivo. Uma coleta conferida pode ter deadlines descumpridos.

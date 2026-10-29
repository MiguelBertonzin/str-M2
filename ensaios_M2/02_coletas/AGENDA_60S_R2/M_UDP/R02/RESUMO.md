# M_UDP_R02

Estado da coleta: **COLETA_CONFERIDA**.

Campanha: AGENDA_60S_R2; coletor: M2-AGENDA-60S-2.

| Tarefa | Jobs | Misses | Perdidos | WCRT (ms) | HWM99 intervalo (ms) |
|---|---:|---:|---:|---:|---|
| FUS_IMU | 12000 | 0 | 0 | 3.271 | 1.6 .. 1.699 |
| CTRL_ATT | 12000 | 0 | 0 | 4.317 | 2.1 .. 2.199 |
| NAV_PLAN | 22 | 0 | 0 | 9.811 | 9.8 .. 9.811 |
| FAIL_SAFE | 4 | 0 | 0 | 1.48 | 1.4 .. 1.48 |
| MONITOR_TASK | 120 | 0 | 0 | 27.614 | 24.3 .. 24.399 |
| TIME_TASK | 60 | 0 | 0 | 60.367 | 60.3 .. 60.367 |

HWM100 = WCRT observado. HWM99 usa histograma de 100 us; o intervalo e um limite, nao um percentil exato.
Os acumuladores incluem todos os jobs validos da janela; os logs individuais FUS/CTRL sao amostrados.

Falhas hard (misses + perdidos + invalidos): 0. Soft: 0.
RTT: 42 respostas; media=494.11638095238095 ms; max=988.2639 ms.
RTT inclui monitor e transporte; sem retry de aplicacao nos comandos da agenda.

## Conferencias

- Agenda oferecida conforme horarios: True; envios 42/42; ACKs ausentes 0; tardios 0.
- Respostas por comando: {'PING:OK': 29, 'NAV:QUEUED': 9, 'NAV:SAFE_ACTIVE': 1, 'FS:QUEUED': 3}.
- Nenhuma inconsistencia automatica detectada; revisar logs e contexto antes do relatorio.

Detalhes de CPU e (9,10)-firm por tarefa em tarefas.csv; transmissao e descartes em resumo.json.
Perdas tornam (m,k) sobre ativacoes inconclusivo. Uma coleta conferida pode ter deadlines descumpridos.

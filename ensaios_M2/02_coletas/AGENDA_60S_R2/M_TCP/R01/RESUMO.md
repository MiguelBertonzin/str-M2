# M_TCP_R01

Estado da coleta: **COLETA_CONFERIDA**.

Campanha: AGENDA_60S_R2; coletor: M2-AGENDA-60S-2.

| Tarefa | Jobs | Misses | Perdidos | WCRT (ms) | HWM99 intervalo (ms) |
|---|---:|---:|---:|---:|---|
| FUS_IMU | 12000 | 0 | 0 | 4.682 | 2.4 .. 2.499 |
| CTRL_ATT | 12000 | 4 | 0 | 7.012 | 2.4 .. 2.499 |
| NAV_PLAN | 22 | 0 | 0 | 9.433 | 9.4 .. 9.433 |
| FAIL_SAFE | 4 | 0 | 0 | 1.608 | 1.6 .. 1.608 |
| MONITOR_TASK | 120 | 0 | 0 | 25.084 | 21.3 .. 21.399 |
| TIME_TASK | 60 | 0 | 0 | 169.218 | 102.4 .. 169.218 |

HWM100 = WCRT observado. HWM99 usa histograma de 100 us; o intervalo e um limite, nao um percentil exato.
Os acumuladores incluem todos os jobs validos da janela; os logs individuais FUS/CTRL sao amostrados.

Falhas hard (misses + perdidos + invalidos): 4. Soft: 0.
RTT: 42 respostas; media=480.34721904761903 ms; max=973.8035 ms.
RTT inclui monitor e transporte; sem retry de aplicacao nos comandos da agenda.

## Conferencias

- Agenda oferecida conforme horarios: True; envios 42/42; ACKs ausentes 0; tardios 0.
- Respostas por comando: {'PING:OK': 29, 'NAV:QUEUED': 9, 'NAV:SAFE_ACTIVE': 1, 'FS:QUEUED': 3}.
- Nenhuma inconsistencia automatica detectada; revisar logs e contexto antes do relatorio.

Detalhes de CPU e (9,10)-firm por tarefa em tarefas.csv; transmissao e descartes em resumo.json.
Perdas tornam (m,k) sobre ativacoes inconclusivo. Uma coleta conferida pode ter deadlines descumpridos.

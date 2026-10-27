# M_UDP_R01_T02

Estado da coleta: **REVISAR**.

| Tarefa | Jobs | Misses | Perdidos | WCRT (ms) | HWM99 intervalo (ms) |
|---|---:|---:|---:|---:|---|
| FUS_IMU | 36000 | 0 | 0 | 4.667 | 2.1 .. 2.199 |
| CTRL_ATT | 36000 | 0 | 0 | 4.164 | 2.1 .. 2.199 |
| NAV_PLAN | 24 | 0 | 0 | 14.074 | 14.0 .. 14.074 |
| FAIL_SAFE | 16 | 0 | 0 | 2.104 | 2.1 .. 2.104 |
| MONITOR_TASK | 360 | 0 | 0 | 37.933 | 29.9 .. 29.999 |
| TIME_TASK | 180 | 0 | 0 | 128.321 | 60.7 .. 60.799 |

HWM100 = WCRT observado. HWM99 usa histograma de 100 us; o intervalo e um limite, nao um percentil exato.
Os acumuladores incluem todos os jobs validos da janela; os logs individuais FUS/CTRL sao amostrados.

Falhas hard (misses + perdidos + invalidos): 0. Soft: 0.
RTT: 116 respostas; media=700.6810344827586 ms; max=2484.0 ms.
RTT inclui espera pelo monitor e retransmissoes; nao e apenas tempo de radio.

## Conferencias

- 34 comandos atrasaram mais de 500 ms em relacao ao roteiro do PC.

Detalhes de CPU e (9,10)-firm por tarefa em tarefas.csv; transmissao e descartes em resumo.json.
Perdas tornam (m,k) sobre ativacoes inconclusivo. Uma coleta conferida pode ter deadlines descumpridos.

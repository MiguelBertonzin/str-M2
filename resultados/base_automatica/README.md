# Validação da base automática na ESP32

`CUSTOM_PRE_240MHz_sem_monitor.txt` preserva o registro do monitor serial do teste
que realizei em 26/10/2026, antes da implementação de MONITOR_TASK/Wi-Fi.

- ESP32, CUSTOM, UNICORE, 240 MHz, preemptivo, slicing ON, GPTimer ISR;
- roteiro `CBCABCBCBCBCDC`, 14 eventos, nenhum descarte;
- sintese aos 30 s: FUS=6000, CTRL=6000, NAV=12, FS=1; nenhum miss reportado;
- emergencia ativou o estado seguro; telemetria posterior mostrou motores zerados.

Esse registro valida o fluxo funcional observado, com as limitacoes de medicao
da FUS ja descritas na auditoria importada. Ele nao inclui a nova carga de Wi-Fi
e nao representa um teste da MONITOR_TASK.

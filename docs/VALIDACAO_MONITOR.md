# Validacao da implementacao MONITOR_TASK — 26/10/2026

> Registro histórico da etapa indicada no título. O estado atual e as coletas
> posteriores estão no [README do projeto](../README.md). As pendências abaixo
> descrevem o momento dessa validação, não necessariamente a versão atual.

Registro historico anterior a TIME_TASK. O estado atual e os novos builds estao
em [VALIDACAO_TIME](VALIDACAO_TIME.md); as pastas de build foram reutilizadas.

Esta etapa foi compilada com ESP-IDF 6.1.0 para ESP32/UNICORE, CPU 240 MHz,
CUSTOM, preemptivo, time slicing ligado e tick de 1 ms.

| Build | Configuracao de entrada/rede | Resultado |
|---|---|---|
| `build` | Roteiro automatico, UDP, SSID vazio | Compilacao concluida |
| `build_monitor_tcp` | Somente PC, TCP, credenciais ficticias de teste | Compilacao concluida, incluindo caminho completo de inicializacao Wi-Fi |

O build TCP usa somente `M2_BUILD_TEST_ONLY` / `test-only-password` para validar
compilacao e tamanho, sem executar conexao ou gravar a placa. Os dois firmwares
cabem na particao de 1 MiB. Os logs locais sao `build_monitor_udp.log` e
`build_monitor_tcp.log`, ignorados no Git. O projeto padrao continua com SSID
vazio; as credenciais da rede devem ser configuradas pelo menuconfig.

Tambem foi conferida a sintaxe de `main.c` e `monitor.c` com MONITOR desabilitada,
usando o compilador Xtensa e as flags/include paths gerados pelo build.

Os testes de `tests/test_monitor_pc.py` passaram. Cobrem enquadramento JSON TCP
fragmentado e agrupado, limite por mensagem, comunicacao TCP local com um par
simulado e retransmissao UDP preservando o identificador. Essa simulacao exercita
o cliente PC; nao executa as filas ou os sockets do firmware ESP32.

Ainda precisam de validacao fisica: conexao Wi-Fi, comunicacao real UDP/TCP,
ACK de NAV/FS, correlacao com jobs, estado seguro dos motores, reconexao,
ocupacao de CPU, memoria/pilha e comportamento temporal sob a nova carga.
O roteiro de teste esta em [MONITOR_TASK](MONITOR_TASK.md).

Nenhum resultado de Wi-Fi foi inventado ou atribuido ao log da base anterior.
TIME_TASK/SNTP, percentis completos, (m,k)-firm e a comparacao experimental entre
UDP e TCP permanecem para as proximas etapas.

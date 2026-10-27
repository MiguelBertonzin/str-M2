# Validacao TIME_TASK - 26/10/2026

Implementacao compilada com ESP-IDF 6.1.0, Xtensa e alvo ESP32. Nao foi gravada
na placa nesta etapa; testes fisicos anteriores eram do firmware sem TIME_TASK.

| Build | Configuracao | Resultado |
|---|---|---|
| `build` | Configuracao local do usuario, UDP, PC, CUSTOM, 240 MHz, cooperativo, slicing OFF, TIME ON | Compilacao e link concluidos; app 0xc5690 bytes |
| `build_monitor_tcp` | Configuracao isolada de teste, TCP, PC, TIME ON | Compilacao e link concluidos; app 0xc61e0 bytes |

Ambos cabem na particao de 1 MiB, com 23% livre. Nenhum warning/error do compilador
nos logs `build_time_udp.log` e `build_time_tcp.log`. O SDK emite notas de Kconfig
e descoberta de versao Git, sem impedir a compilacao. Credenciais locais foram
preservadas; o build TCP isolado usa as credenciais ficticias da validacao anterior.

Verificacoes adicionais:

- Sete testes Python passaram: casos anteriores de TCP/UDP e exibicao de hora
  antes da sincronizacao, UTC com milissegundos, idade e descartes.
- `main.c`, `monitor.c` e `time_task.c` passaram pela verificacao de sintaxe com
  o compilador Xtensa, headers do build e TIME OFF, tanto com MONITOR ON quanto
  OFF. Isso verifica as alternativas condicionais, nao a execucao dessas variantes.
- Inicializacao SNTP, callback e API de reinicio conferidas no SDK instalado.
- As duracoes continuam usando esp_timer; timestamps UTC sao metadados separados.
- Hashes das fontes e dos binarios em `validacao_time_builds.json`, sem credenciais.

Ainda dependem da placa/rede: primeira resposta SNTP, segunda atualizacao e
reinicio do contador, queda/retorno da conexao, custos e interferencia medidos.
Os testes Python exercitam o cliente, nao executam a callback SNTP ou o firmware.
Nao se afirma que deadlines serao atendidos nem que a hora tem exatidao de 1 ms.

O roteiro de observacao e comparacao esta em [TIME_TASK](TIME_TASK.md).

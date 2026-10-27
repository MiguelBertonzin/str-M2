# Validação da base M2 — 26/10/2026

Este registro e `validacao_builds.json` são da base anterior à MONITOR_TASK.
O teste físico dessa base foi posteriormente enviado pelo usuário e preservado em
`resultados/base_automatica/`. A etapa de rede tem documentação própria em
[MONITOR_TASK](MONITOR_TASK.md).

Compilação realizada no Windows com ESP-IDF v6.1.0, toolchain Xtensa e alvo ESP32.

| Build | Fonte | Configuração | Resultado |
|---|---|---|---|
| `build` | GPTimer ISR | CUSTOM, 240 MHz, preemptivo, slicing ON | Firmware gerado |
| `build_pior` | GPTimer ISR | DM, 80 MHz, cooperativo, slicing ON | Firmware gerado |
| `build_touch` | Touch físico | CUSTOM, 240 MHz, preemptivo, slicing ON | Firmware gerado |

Todos usam UNICORE, tick de 1 ms e estatísticas de CPU via ESP Timer. Os três
binários cabem na partição padrão de 1 MiB. Os logs locais de compilação estão em
`build_inicial.log`, `build_pior.log` e `build_touch.log` (ignorados no Git).

Conferências realizadas:

- hashes SHA-256 das 30 referências importadas, comparados também com os originais;
- presença dos 18 logs históricos da M1;
- roteiro `CBCABCBCBCBCDC`, 13 alarmes, 14 eventos, B+C no mesmo alarme;
- contagens A=1, B=5, C=7, D=1, intervalos do mesmo canal acima de 1,2 s;
- configurações efetivas de CPU, política e fonte em cada build;
- inclusão da configuração local do FreeRTOS antes do header original, tanto na
  aplicação quanto no kernel e nas filas;
- pré-processamento real com o compilador Xtensa: `configUSE_PREEMPTION` igual
  a 1, 0 e 1, respectivamente, na aplicação e em `tasks.c`.

O SDK em disco não foi editado. Somente a cópia de configuração dentro de cada
build recebe os valores experimentais. A execução das conferências locais pode
ser repetida com `python tools/verificar_base.py`.

**Não houve flash nem ensaio físico nesta etapa.** Compilar e conferir a agenda
não comprova a execução do roteiro na placa, ausência de estouro de pilha, jitter,
perdas de fila ou atendimento de deadlines. A próxima validação deve gravar a
ESP32, observar `INPUT`, `CONFIG`, `AUTO_EVENT`, `TOUCH_SEQ` e a síntese, e salvar
o log integral de cada repetição. A sequência deve ter 14 eventos, 12 jobs NAV,
1 job FS e nenhum descarte/AUTO_ERROR. Repetir também no modo touch físico.

Não há resultados de Wi-Fi/SNTP, HWM(99%) ou (m,k)-firm: esses itens permanecem
no plano de desenvolvimento. Os resultados da pasta de referências pertencem
à M1 e não foram recalculados como se fossem novas medições.

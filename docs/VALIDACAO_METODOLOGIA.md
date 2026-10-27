# Validação M2-METRICS-2 — 27/10/2026

Implementação preparada para os pilotos da metodologia. **Não foi gravada na
placa nem validada fisicamente nesta etapa.** Os logs antigos foram preservados
em `ensaios_M2/03_anexos_chat/H01...H05` e pertencem à instrumentação anterior.

## Compilação final

ESP-IDF 6.1.0 / Xtensa / ESP32 / UNICORE / tick 1000 Hz. Cinco builds recompilados
após as últimas alterações C, sem warnings/errors do compilador nos logs finais:

| Pasta | Cenário | App (hex bytes) | Partição livre |
|---|---|---:|---:|
| build | M_UDP, AUTO, aguarda BEGIN | 0xc9a10 | 21% |
| build_monitor_tcp | M_TCP, AUTO | 0xca4f0 | 21% |
| build_pior | P_UDP, AUTO | 0xc9940 | 21% |
| build_ensaio_p_tcp | P_TCP, AUTO | 0xca420 | 21% |
| build_touch | M_UDP, TOUCH | 0xcb8d0 | 20% |

Os diretórios isolados usam credenciais fictícias apenas para compilar o caminho
Wi-Fi completo. Para os testes, usar os comandos do guia que configuram e gravam
**build**, preservando as credenciais reais do sdkconfig local. Não gravar os
binários isolados de validação esperando que conectem à rede do usuário.

## Conferências

- 16 testes Python passaram: enquadramento TCP, retries UDP, coleta REPORT das
  seis tarefas, proteção contra cenário errado, preservação de credenciais,
  roteiro, timestamp/offset SNTP, rejeição de resposta inválida, rollover NTP,
  percentil por rank, (m,k) inconclusivo quando faltam eventos e geração dos
  arquivos de análise com sinalização de coleta incompleta.
- Verificação de sintaxe de main/monitor/time com TIME OFF, e com MONITOR OFF,
  pelo compilador Xtensa e headers da configuração gerada.
- `tools/verificar_base.py`: 30 referências M1 íntegras, 18 logs históricos,
  sequência de eventos preservada e overrides de preempção/slicing corretos
  na aplicação e kernel dos cenários base/melhor, pior e touch.
- Seis anexos enviados pelo usuário arquivados com origem, bytes e SHA256.
- Matriz/índice com 27 testes planejados, todos inicialmente AGUARDANDO.

Os testes de rede Python usam pares locais simulados e não executam o firmware.
Compilar não prova o comportamento do rádio, reset USB, disponibilidade do SNTP,
consumo de heap/stack em execução nem cumprimento de deadlines. Isso é função
dos pilotos. Não foi possível executar testes nativos do acumulador C neste
ambiente com compiladores exclusivamente embarcados; houve revisão e compilação.

## Mudanças que afetam a comparação

- FUS usa liberação nominal via esp_timer (fila de 8), e conta liberações perdidas.
- CTRL conserva a amostra mais recente, agora contando sobrescritas e saltos de
  sequência. FS mede ação segura dentro da seção crítica e fim do corpo à parte.
- Estatísticas da janela BEGIN→BEGIN+180s incluem todos os jobs válidos localmente;
  logs individuais de FUS/CTRL continuam amostrados. Histograma ocupa cerca de
  24 KiB mais metadados e cópia temporária na stack do monitor (10 KiB de stack).
- (9,10)-firm cobre janelas de execuções consecutivas; perdas/invalidos impedem
  concluir atendimento sobre todas as ativações. HWM99 é um intervalo, não valor
  exato; HWM100/WCRT observado é o máximo exato dos jobs executados na janela.
- Latência ISR e ação segura por ISR são separadas das ativações pelo PC.
- A/STRESS passou a ter seu job NAV instrumentado: 13 jobs NAV do roteiro.
- Relógio PC tem referência SNTP estimada; timestamps de envio/recepção são da
  aplicação, não do rádio. RTT continua monotônico no PC.
- A instrumentação e a nova fonte periódica têm custo; manter a mesma revisão
  nos quatro cenários. Não reaproveitar números antigos como se fossem desta revisão.

Hashes das fontes/binários: `validacao_metodologia_builds.json`.
Roteiro de execução: [ensaios_M2](../ensaios_M2/README.md).

# M2 — Drone ESP32: código, testes e evidências

Continuação da M1 em C/FreeRTOS, com MONITOR_TASK (Wi-Fi UDP/TCP) e TIME_TASK (SNTP). Cliente/coletor do PC em Python. **Versão em validação: não é uma bateria concluída nem garantia de todos os requisitos atendidos.**

[Enunciado M2 em PDF](Trabalho%20M2%20-%20STR%20.pdf). Estado publicado em 27/10/2026, instrumentação `M2-METRICS-2`.

## O que já funciona e foi observado

- Seis tarefas implementadas: FUS_IMU, CTRL_ATT, NAV_PLAN, FAIL_SAFE, MONITOR_TASK e TIME_TASK.
- Eventos A/B/C/D automáticos por ISR de GPTimer; BEGIN coordena o início; estatísticas locais consideram todos os jobs válidos.
- UDP, NAV/FS, bloqueio de NAV após fail-safe e sincronização SNTP observados na placa.
- Compilação registrada dos quatro cenários e variante touch; 16 testes Python registrados. Compilar não comprova funcionamento na placa.
- **Uma coleta completa:** [M_UDP/R01_T02](ensaios_M2/02_coletas/M_UDP/R01_T02/RESUMO.md), 180 s, zero misses, perdas de ativações ou timestamps inválidos nas seis tarefas; três novas sincronizações SNTP.
- **Ressalva REVISAR:** 34 comandos atrasaram mais de 500 ms em relação à agenda. O cliente aguarda ACK antes do próximo envio. Isso afeta a carga oferecida e precisa de revisão antes da comparação final; não significa deadline perdido na ESP32.
- A [tentativa R01](ensaios_M2/02_coletas/M_UDP/R01/manifesto.json) foi interrompida por desconexão USB e está preservada como falha.

Veja a [revisão da coleta](ensaios_M2/03_anexos_chat/M_UDP_R01_T02/REVISAO.md), o [índice](ensaios_M2/INDICE.csv) e a [validação técnica histórica](docs/VALIDACAO_METODOLOGIA.md). Documentos antigos podem mencionar planos substituídos: **o plano atual tem 20 coletas, não 27**. Hashes históricos identificam a versão validada naquele momento.

## Preparar outra máquina

Necessário: ESP32 clássico com touch, cabo USB de dados, Git, **ESP-IDF 6.1** e Python do ambiente ESP-IDF. Rede Wi-Fi **2,4 GHz** com comunicação entre PC e placa, DNS e SNTP (UDP 123). Aplicação usa porta 5000. Projeto configurado UNICORE.

Abra um **terminal ESP-IDF** na extensão do VS Code ou ative o ambiente instalado. PowerShell comum sem ativação não reconhece idf.py. Confira `idf.py --version`.

```powershell
git clone https://github.com/MiguelBertonzin/str-M2.git
cd str-M2
python -m pip install -r pc/requirements.txt
idf.py set-target esp32
idf.py menuconfig
```

Em **M2 - Drone e experimentos**, preencha SSID e senha da sua rede. Os padrões são vazios; [sdkconfig.wifi.example](sdkconfig.wifi.example) contém apenas placeholders. O arquivo sdkconfig é local, ignorado pelo Git. Não publique configurações/binários com suas credenciais. sdkconfig.defaults só inicializa configurações novas.

O CMake gera um FreeRTOSConfig.h local ao build para preempção/slicing no kernel e aplicação. **Não altere o SDK global.** Se outro SDK não for compatível, use a versão validada ou revise explicitamente o adaptador.

## Rodar uma coleta automática

Troque COM7 pela porta da sua placa e o IP abaixo pelo **MONITOR_IP** exibido no serial (não o IP do notebook).

```powershell
python tools/configurar_teste.py --cenario M_UDP
idf.py -p COM7 flash monitor
```

Anote o IP, feche o monitor com **Ctrl+]** e execute:

```powershell
python pc/executar_ensaio.py --cenario M_UDP --repeticao R02 --host 192.168.15.23 --serial COM7
```

R02 está livre; R01 e R01_T02 são evidências já incluídas. Para começar uma campanha independente dos colegas em R01, mova `ensaios_M2/02_coletas` para uma pasta de arquivo fora da execução, preservando as evidências originais. O coletor recria as pastas necessárias. Identifique a origem de cada campanha; placas/redes diferentes não devem ser misturadas como condições equivalentes.

O coletor reinicia a placa, aguarda rede/SNTP, valida configuração e executa BEGIN/agenda/coleta. **Sem tocar nos sensores nem digitar NAV/FS.** Mantenha cabo e rede até **Arquivos salvos em:** e retorno ao prompt. São 180 s medidos mais preparação e finalização; normalmente 3–4 minutos, podendo demorar mais na conexão.

Para refazer uma tentativa falha, adicione `--tentativa 2` (ou próximo número livre), sem apagar a anterior. Entre repetições do mesmo cenário basta mudar `--repeticao`. Para mudar cenário, execute o configurador e grave novamente. Ele preserva Wi-Fi, mas redefine os parâmetros experimentais.

## Matriz e coletas pendentes

| Cenário | Política | Preempção | CPU | Slicing | Rede | Repetições |
|---|---|---|---|---|---|---|
| M_UDP | CUSTOM | ON | 240 MHz | ON | UDP | R01–R05 |
| M_TCP | CUSTOM | ON | 240 MHz | ON | TCP | R01–R05 |
| P_UDP | DM | OFF/cooperativo | 80 MHz | ON | UDP | R01–R05 |
| P_TCP | DM | OFF/cooperativo | 80 MHz | ON | TCP | R01–R05 |

Melhor/pior vêm dos cenários 02/07 da M1, não são extremos garantidos. Cinco repetições e 180 s são escolhas nossas; o PDF pede vários testes sem fixar esses números. Slicing ON no cooperativo não implica preempção por tick; há yields explícitos.

**Faltam 19 coletas completas no plano**, com aceitação da R01 atual ainda sujeita à revisão da agenda. Se mudar a carga/metodologia, pode ser necessário repetir R01. Primeiro validar R01 dos outros três cenários; depois multiplicar repetições. [Matriz](ensaios_M2/01_planejamento/MATRIZ.csv) e [guia operacional](ensaios_M2/README.md).

## Cobertura do PDF e o que falta

| Item | Situação |
|---|---|
| 1 — Melhor/pior M1 | Configurações prontas, referências históricas preservadas |
| 2 — UDP/TCP | Implementados; validar cenários restantes e revisar agenda antes da comparação |
| 3 — SNTP/RTT | Referência no PC/ESP, timestamps, sequência e bytes; RTT monotônico no PC |
| 4 — WCRT/HWM | Máximo observado, HWM100 e HWM99 por histograma |
| 5 — (m,k)-firm | (9,10) nas soft; perdas tornam a conclusão sobre todas as ativações inconclusiva |
| 6 — Interferência/bloqueio | Dados/código disponíveis; análise fundamentada ainda pendente |
| 7 — Deadlines em vários testes | Misses contados; atendimento nos quatro cenários ainda não demonstrado |
| 8 — Logs/latência/CPU | Instrumentados; logs individuais FUS/CTRL amostrados, acumuladores completos |
| 9 — Tabela comparativa | CSV por ensaio pronto; consolidar, gerar gráficos e escrever conclusões |
| Extra — FS remoto acender LED | **Não implementado** |

**Touch físico:** por escolha desta campanha, eventos são automáticos por GPTimer. O PDF exige sensores touch; a automação não comprova esse requisito. Existe modo TOUCH, mas validação separada/aceite da substituição permanecem pendentes. Não chamar latência GPTimer de latência touch.

Outras limitações: SNTP não garante precisão de 1 ms. Timestamps são da aplicação, não do rádio. RTT inclui monitor de 500 ms, escalonamento e retries. WCRT observado não é garantia formal de WCET. HWM99 tem intervalos de 100 us e overflow acima de 102,4 ms. FS fica travado até reiniciar; FS posteriores reafirmam o estado seguro. Tempo decorrido da tarefa pode incluir preempção; CPU é medida separadamente. Instrumentação também custa CPU/memória.

## Apresentação sem Codex

**Sim: pode mudar os parâmetros pelo menuconfig, recompilar e gravar.** Veja [APRESENTACAO.md](docs/APRESENTACAO.md), com parâmetros, comandos e diagnóstico.

O coletor automático aceita apenas os quatro cenários padronizados. Para RM, 160 MHz, slicing OFF ou outra combinação pedida na hora, use o cliente interativo e identifique como demonstração fora da matriz. Não rode o configurador de cenário depois das mudanças manuais: ele as substituiria.

## Organização e análises

- `main/`: firmware, tarefas, ISR, rede, SNTP e métricas.
- `cmake/`: configuração de escalonador local ao build.
- `pc/`: cliente interativo, coleta automática e referência SNTP.
- `tools/`: configuração, análise, índice e arquivamento.
- `tests/`: testes do cliente e ferramentas.
- `ensaios_M2/`: planejamento, dados brutos, estatísticas, revisões e relatório futuro.
- `referencias/m1/`: código, PDFs e resultados usados como referência histórica.
- `referencias/professor/`: exemplos originais e procedência.
- `docs/`: arquitetura, metodologia, validações e guia de apresentação.

Cada coleta guarda manifesto, serial, rede JSONL, agenda/comandos, estatísticas, CSV e resumo quando a finalização foi possível. COLETADO significa que o processo terminou; REVISAR indica ressalvas. Nenhum deles significa automaticamente atendimento ao PDF. Falhas são preservadas.

```powershell
python -m unittest discover -s tests -p "test_*.py"
python tools/analisar_ensaio.py --help
python tools/atualizar_indice.py
```

[Plano do relatório](ensaios_M2/04_relatorio/README.md). Enviem a pasta completa do ensaio, identificando placa, rede, revisão do código e alterações. A publicação usa cópias sanitizadas; veja [PUBLICACAO.md](docs/PUBLICACAO.md). Nunca compartilhem credenciais nos resultados.

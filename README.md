# M2 — Análise temporal de um drone didático em ESP32

Implementação em C/FreeRTOS de fusão sensorial, controle de atitude, navegação,
fail-safe, comunicação Wi-Fi UDP/TCP e sincronização SNTP. O projeto dá
continuidade à M1 e investiga a resposta temporal das seis tarefas em quatro
configurações. As ferramentas de coleta e análise no PC são escritas em Python.

[Enunciado](Trabalho%20M2%20-%20STR%20.pdf) ·
[Análise dos resultados](analise_final/README.md) ·
[Arquitetura](docs/ARQUITETURA_FINAL.md) ·
[Metodologia](docs/METODOLOGIA_FINAL.md) ·
[Modos de entrada](docs/MODOS_ENTRADA.md)

## Resultados da campanha

Campanha `AGENDA_60S_R2`: cinco repetições de 60 segundos por cenário,
firmware `M2-METRICS-4` e coletor de medição `M2-AGENDA-60S-2`.
As 20 coletas têm comandos confirmados, registros NAV/FS completos e
estatísticas das seis tarefas. Os acumuladores incluem os jobs válidos da
janela, mesmo quando os logs periódicos individuais são amostrados ou descartados.

| Cenário | Política | Escalonamento | CPU | Rede | Repetições | Falhas hard¹ | Falhas soft |
|---|---|---|---:|---|---:|---:|---:|
| M_UDP | CUSTOM | Preemptivo | 240 MHz | UDP | 5 | 0 | 0 |
| M_TCP | CUSTOM | Preemptivo | 240 MHz | TCP | 5 | 20 | 0 |
| P_UDP | DM | Cooperativo | 80 MHz | UDP | 5 | 1911 | 75 |
| P_TCP | DM | Cooperativo | 80 MHz | TCP | 5 | 3081 | 75 |

¹ Violações de prazo e ativações perdidas, discriminadas na análise.
Todos usam unicore e slicing configurado como ON. No modo cooperativo esse
valor não implica preempção pelo tick.

M_UDP atendeu todos os prazos nas cinco janelas observadas. Os demais
cenários apresentaram descumprimentos que integram os resultados. Esses
ensaios não demonstram garantia formal para todas as cargas. A comparação
M/P altera simultaneamente prioridade, frequência e preempção.

## Reproduzir o projeto

Ambiente de referência: ESP32 clássico, ESP-IDF 6.1, Python 3.11 e terminal
ESP-IDF. A ESP32 e o PC devem ter comunicação na mesma rede Wi-Fi 2,4 GHz;
SNTP requer acesso à rede externa. Porta da aplicação: 5000.

```powershell
git clone https://github.com/MiguelBertonzin/str-M2.git
cd str-M2
python -m pip install -r pc/requirements.txt
idf.py set-target esp32
idf.py menuconfig
```

Em **M2 - Drone e experimentos**, definir SSID e senha. As credenciais são
locais em `sdkconfig`, ignorado pelo Git. `sdkconfig.wifi.example` contém
placeholders. O adaptador em `cmake/` configura preempção e slicing no build
do projeto sem modificar o SDK global.

### Modo automatizado — padrão

```powershell
python tools/configurar_teste.py --cenario M_UDP --automatizado
idf.py -p COM7 flash
python pc/executar_ensaio.py --cenario M_UDP --repeticao P01 --automatizado --host auto --serial COM7
```

Substituir COM7 pela porta local. `P01` identifica uma demonstração e evita
colisão com R01–R05 já publicadas. Omitir `--automatizado` mantém o padrão.
O coletor ocupa a serial, reinicia a placa, aguarda rede/SNTP, envia BEGIN,
executa a agenda e recebe as estatísticas. Aguardar **Arquivos salvos em:**
e retorno ao terminal. Não abrir outro monitor na mesma porta.

Para repetir o mesmo cenário, alterar apenas `--repeticao P02`, P03 etc.
Para mudar cenário, executar o configurador e gravar novamente. Os quatro
nomes válidos são M_UDP, M_TCP, P_UDP e P_TCP. Resultados publicados não
devem ser sobrescritos para uma apresentação.

### Modo físico — demonstração com touch

```powershell
python tools/configurar_teste.py --cenario M_UDP --fisico
idf.py -p COM7 flash
python pc/executar_ensaio.py --cenario M_UDP --repeticao T01 --fisico --host auto --serial COM7
```

O modo de entrada é selecionado na compilação: trocar a opção do coletor
sem gravar o firmware correspondente produz diagnóstico de incompatibilidade.
O guia físico, pinos e limites dessa demonstração estão em
[MODOS_ENTRADA.md](docs/MODOS_ENTRADA.md). As coletas analisadas neste repositório
são automáticas; a opção física não transforma essas evidências em medições
do sensor capacitivo.

Para RM, 160 MHz e configurações fora da matriz, usar `idf.py menuconfig`
e o cliente interativo conforme [APRESENTACAO.md](docs/APRESENTACAO.md).

## Dados, análise e rastreabilidade

- `ensaios_M2/02_coletas/AGENDA_60S_R2/`: 20 conjuntos de dados brutos,
  manifestos, comandos, estatísticas e resumos.
- `analise_final/`: discussão, tabelas CSV e gráficos exportáveis.
- `evidencias_complementares/`: primeiras R01 de P_UDP e P_TCP, identificadas
  na análise de seleção e sensibilidade; não são cinco novas repetições.
- `fontes_campanha/`: snapshot das fontes registradas nos manifestos da
  campanha, anterior aos aliases de linha de comando para apresentação.
- `main/`, `cmake/`, `pc/`, `tools/`, `tests/`: implementação atual e ferramentas.
- `referencias/`: evidências M1 da escolha de configurações e exemplos do
  professor com atribuição de origem.

As opções de apresentação foram acrescentadas aos scripts depois da coleta.
O firmware medido permanece preservado. O manifesto de publicação identifica
os hashes das cópias públicas e eventuais remoções de credenciais dos logs;
os originais de medição permanecem preservados localmente.

```powershell
python -m unittest discover -s tests -v
python -m pip install -r analise_final/requirements.txt
python tools/gerar_analise_final.py
```

Os detalhes de reprodução, agregação e dependências estão no README da análise.

## Limitações e atendimento ao enunciado

A matriz corresponde às configurações de melhor/pior atendimento selecionadas
na M1, cruzadas com UDP/TCP. WCRT é o máximo observado; HWM99 usa intervalos
de histograma, sem equivaler a WCET formal. O modelo soft adotado é (9,10),
sem formar janelas entre reinicializações.

RTT inclui o atendimento periódico do monitor de 500 ms, filas e transporte.
UTC via SNTP não garante precisão de 1 ms. Os cenários P descartam logs
secundários; isso deve ser distinguido de perdas de comandos e de pacotes
no rádio. As execuções em blocos e a variação de RSSI limitam a atribuição
causal das diferenças somente ao protocolo.

O PDF exige sensores touch. Nesta campanha, os estímulos foram gerados por
GPTimer; a latência apresentada é ISR → tarefa, não contato físico → tarefa.
A conformidade física depende de demonstração específica ou aceite da
substituição. O extra de acender LED por FS remoto não está implementado.
O fail-safe permanece ativo até reiniciar; os FS remotos da agenda reafirmam
um estado já seguro.

Este material reúne implementação, evidências e análise técnica para a
elaboração do relatório final em PDF e para a defesa presencial.

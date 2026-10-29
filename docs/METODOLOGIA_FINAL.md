# Metodologia experimental — Drone M2

## Objetivo e desenho do experimento

O experimento avalia o comportamento temporal do autopiloto didático com
MONITOR_TASK e TIME_TASK, comparando UDP e TCP em duas configurações
selecionadas a partir da M1. A seleção atende ao item 1 da metodologia do
enunciado: uma referência com melhor atendimento de prazos e outra com pior
atendimento no conjunto previamente observado.

Foram usadas as configurações 02 e 07 de
`referencias/m1/tabelas_graficos/dados/resumo_cenarios.csv`. A configuração
02 apresentou zero misses hard e soft; a 07 apresentou 73 misses hard e
cinco soft na execução registrada. A M1 contém uma execução por combinação,
portanto essa seleção identifica referências experimentais, sem provar
extremos universais de desempenho.

| Cenário | Referência M1 | Prioridade | Escalonamento | CPU | Time slicing | Transporte |
|---|---|---|---|---:|---|---|
| M_UDP | 02 | CUSTOM | Preemptivo | 240 MHz | ON | UDP |
| M_TCP | 02 | CUSTOM | Preemptivo | 240 MHz | ON | TCP |
| P_UDP | 07 | DM | Cooperativo | 80 MHz | ON | UDP |
| P_TCP | 07 | DM | Cooperativo | 80 MHz | ON | TCP |

Foram realizadas cinco repetições de 60 s por cenário: 20 janelas e 1200 s
de medição planejada. Quantidade e duração são escolhas experimentais;
o PDF solicita vários testes, sem fixar esses números. A matriz não inclui
RM, 160 MHz ou todas as combinações possíveis de preempção e slicing.
Slicing ON no cenário cooperativo representa o parâmetro configurado, sem
implicar preempção periódica por tick.

## Plataforma e controles

Utilizaram-se ESP32 clássica, ESP-IDF 6.1, FreeRTOS em unicore, tick de 1 ms,
Wi-Fi de 2,4 GHz, porta de aplicação 5000 e notebook com cliente/coletor Python.
A revisão medida é `M2-METRICS-4`, com coletor `M2-AGENDA-60S-2` e campanha
`AGENDA_60S_R2`. MONITOR e TIME permaneceram habilitadas, com períodos de
500 ms e 1 s; o intervalo de sincronização SNTP foi de 20 s.

As cargas computacionais são laços com quantidades fixas de operações, em
vez de esperas por uma duração fixa. Assim, variar a frequência altera o
tempo necessário à execução. O mesmo roteiro e a mesma configuração de
amostragem de logs foram empregados nos quatro cenários.

Cada manifesto registra configuração reportada pela placa, revisão,
timestamp da coleta, hashes das fontes, RSSI e canal disponíveis no início.
As repetições ocorreram principalmente em blocos por cenário. Não houve
randomização ou alternância completa dos protocolos, e variações de rede
ao longo do tempo permanecem como fator de confusão. Mudanças de endereço
IP entre sessões, por si só, não demonstram troca de ponto de acesso.

## Preparação e janela

A placa é reiniciada antes de cada repetição, eliminando o estado seguro
travado da execução anterior. O coletor acompanha a serial, identifica IP
e protocolo, valida a configuração, consulta SNTP no notebook e aguarda a
sincronização inicial da ESP32. Em seguida envia `BEGIN`.

A janela da ESP32 é `[run_start_us, run_end_us)`, com 60 s no relógio
monotônico. Um job integra a janela por seu timestamp de evento, mesmo se
terminar após o limite final. Inicialização, conexão e aquecimento ficam
registrados, mas fora dos acumuladores dessa janela. O coletor aguarda uma
margem de 7 s e solicita as estatísticas finais das seis tarefas.

O relógio da agenda do notebook começa no primeiro envio de BEGIN. O início
efetivo na placa ocorre depois da recepção e não é presumido simultâneo.
Os timestamps dos dois lados são preservados para verificar a carga e a
fase dos eventos. A finalização exige estatísticas da mesma solicitação de
relatório, evitando combinar respostas de relatórios distintos.

## Estímulos reproduzíveis

O roteiro automático usa ISR de GPTimer e gera 14 eventos em 13 alarmes:

| Instante desde o início do roteiro (s) | Evento |
|---:|---|
| 2 | C |
| 4 | B |
| 6 | C |
| 8 | A |
| 10 | B |
| 12 | C |
| 14 | B |
| 16 | C |
| 18 | B |
| 20 | C |
| 22 | B e C no mesmo alarme |
| 24 | D |
| 26 | C |

As contagens são A=1, B=5, C=7 e D=1. Os 13 eventos A/B/C são contabilizados
como jobs de NAV, incluindo o tratamento de estresse A. A solicita carga
adicional durante 2000 amostras da FUS, aproximadamente 10 s quando as
liberações são atendidas regularmente. D trava o estado seguro.

O PC oferece 42 comandos por janela: 29 PING em 2, 4, ..., 58 s; nove NAV
em 5, 7, ..., 21 s; uma NAV em 27 s, esperada como `SAFE_ACTIVE`; e três FS
em 30,75, 40,75 e 50,75 s. As respostas esperadas são PING/OK, NAV inicial
e FS/QUEUED e NAV posterior/SAFE_ACTIVE. Combinando PC e ISR, esperam-se
22 jobs NAV e quatro FS.

Os comandos são enviados em horários absolutos, sem aguardar o ACK anterior.
Há um envio de aplicação por comando em ambos os protocolos; TCP mantém
seus mecanismos próprios de transporte. A tolerância de emissão é de
100 ms e a observação de ACK é de 5 s. Um horário perdido é registrado como
tal, sem enviar posteriormente uma rajada de compensação.

## Dados e critérios de conferência

Cada pasta contém manifesto, serial, mensagens de rede, agenda, comandos,
estatísticas locais, resumo e tabela por tarefa. A conferência automática
verifica configuração, janela, agenda, estados das respostas, correlação
por sessão/sequência, carga executada, registros NAV/FS, número de tarefas,
timestamps e sincronizações dentro do período.

`COLETA_CONFERIDA` significa que os critérios de consistência foram
atendidos. Uma coleta pode receber esse estado e apresentar misses de
deadline. Falha de prazo, ativação perdida, comando não confirmado e perda
de log são fenômenos distintos e permanecem separados na análise.

FUS/CTRL têm logs individuais amostrados a cada 100 jobs. Os acumuladores
locais incluem todos os jobs válidos da janela, permitindo calcular
contagens e máximos sem depender da recepção de cada log. Nos cenários P
houve descartes de logs secundários e de transmissão. Isso limita a
reconstrução individual de todos os jobs, embora os registros NAV/FS,
ACKs e estatísticas das pastas finais estejam completos.

Os deltas de descartes são observados entre snapshots do coletor e podem
abranger a etapa de finalização. Não equivalem a uma taxa de perda no rádio
durante exatamente 60 s nem devem ser atribuídos exclusivamente ao UDP.

## Definição das métricas

Para evento `e`, início `s`, fim `f` e deadline relativo `D`:

| Métrica | Definição e limite de interpretação |
|---|---|
| Resposta | `f − e`, incluindo atraso de ativação |
| Miss | Job válido cujo tempo de resposta é maior que D |
| Ativação perdida | Evento/amostra contabilizado sem o correspondente job concluído |
| C observado | `f − s`; tempo decorrido, não CPU exclusiva nem WCET formal |
| WCRT/HWM100 | Maior resposta observada na janela |
| HWM99 | Intervalo do histograma de 100 µs; overflow acima de 102,4 ms |
| Latência de ISR | Início da tarefa menos timestamp registrado no callback GPTimer |
| Resposta segura FS | Instante em que os motores são zerados menos timestamp de origem |
| CPU | Contadores de execução em janelas completas de 5 s dentro do ensaio |
| RTT de PING | Recepção do ACK menos envio do PING no relógio monotônico do notebook |

O timestamp de FUS é a liberação nominal. O de CTRL é a amostra registrada
no início da FUS, de modo que a resposta de CTRL inclui fusão e espera na
fila. Para NAV/FS, o timestamp é o da ISR ou recepção registrada na placa.
O RTT inclui atendimento periódico do monitor, escalonamento, filas e
transporte. Não se trata apenas do atraso do enlace Wi-Fi.

Para diferenças de timestamps UTC entre PC e ESP32, a análise deve
considerar offset estimado, deriva e assimetria. A presença de três casas
decimais não demonstra exatidão de 1 ms. Bytes registrados são da mensagem
de aplicação, não o tamanho total transmitido no enlace.

## Modelo (9,10)-firm e agregação

NAV_PLAN, MONITOR_TASK e TIME_TASK foram avaliadas por janelas deslizantes
de dez execuções consecutivas. Uma janela satisfaz (9,10) quando pelo menos
nove respostas cumprem o prazo. As janelas não atravessam reinicializações.
Perdas ou timestamps inválidos impedem afirmar o atendimento sobre todas
as ativações, mesmo quando a subsequência executada parece satisfazer o modelo.

A análise apresenta contagens de janelas e violações por repetição e tarefa.
WCRT de um cenário é o maior WCRT observado entre suas repetições. HWM99
continua identificado por execução e por intervalo; não se calcula um
percentil global a partir da média dos cinco percentis. Para RTT, são
preservados valores por repetição e amostras individuais do mesmo tipo de
comando, sem confundir PING com o conjunto PING/NAV/FS.

Percentuais de misses usam jobs executados como denominador. Ativações
perdidas são apresentadas em coluna própria. A soma de misses e perdas,
quando usada como indicador agregado hard, é explicitamente identificada.

## Conjunto principal e execuções adicionais

O conjunto principal corresponde às pastas ativas R01 a R05 de cada cenário.
As primeiras execuções R01 de P_UDP e P_TCP foram repetidas e preservadas
como evidência adicional. A primeira P_UDP tinha 21/22 registros NAV, com
423 falhas hard e 15 soft; a R01 principal recebeu todos os registros e
apresentou 403 hard e 15 soft.

A primeira P_TCP já era uma coleta conferida, com 634 hard e 15 soft; sua
nova R01 apresentou 628 hard e 15 soft. A repetição não é justificada como
correção de uma coleta inválida. As comparações devem explicitar a seleção
e apresentar sensibilidade ao uso da primeira execução. Nenhuma execução
é excluída por conter mais violações de prazo.

## Correspondência com os itens 1 a 9 do enunciado

| Item | Tratamento na análise |
|---|---|
| 1 — Melhor/pior M1 | Seleção documentada de 02/07 e matriz de quatro cenários |
| 2 — UDP/TCP | Comparação de resposta da aplicação, perdas/descartes e efeitos nos prazos |
| 3 — SNTP e comunicação | Sequências, bytes, UTC e RTT monotônico, com limites de sincronização |
| 4 — WCRT/HWM | Máximos por tarefa e cenário, intervalos HWM99 identificados |
| 5 — (m,k)-firm | Janelas (9,10) por tarefa soft e repetição |
| 6 — Interferência/bloqueio | Discussão das prioridades, cooperação, SDK, filas e seções críticas |
| 7 — Todos os deadlines | Critério avaliado pelos misses e perdas, sem equiparar coleta completa a sucesso |
| 8 — Logs/latência/CPU | Timestamps, ISR GPTimer e contadores de execução, explicitando amostragem |
| 9 — Tabela comparativa | Tabelas adaptadas ao drone, com seis tarefas, unidades e classificação |

## Limitações de validade e cobertura

M_UDP cumpriu os prazos nas cinco janelas; os demais cenários apresentaram
falhas. Isso é evidência observada e não garantia para todo caso possível.
As condições M/P alteram três fatores simultaneamente; a comparação não
isola o efeito de cada um. Cinco repetições e um roteiro finito também não
caracterizam todas as condições de carga e rede.

O enunciado exige sensores touch para gerar as interrupções. A campanha
automática mantém o caminho ISR → tarefa, mas não demonstra esse requisito
físico. O modo físico disponibiliza a alternativa de execução; sua existência
no código não transforma as coletas GPTimer em medições de touch. O aceite
da substituição ou a demonstração física deve ser tratado explicitamente.

Os FS remotos ocorreram após o evento D, com segurança já travada. Eles
comprovam comando aceito e execução, sem representar três transições novas
para emergência. O LED acionado por FS remoto, proposto como ponto extra,
não foi implementado nem é alegado como resultado.

As condições de RSSI variaram, e a ordem em blocos permite efeitos temporais
da rede. As conclusões sobre UDP/TCP são específicas desta aplicação e
destas condições, especialmente do monitor periódico de 500 ms. O protocolo
com menos falhas neste conjunto não é universalmente melhor para qualquer
sistema de tempo real.

Referências: [arquitetura](ARQUITETURA_FINAL.md) e
[enunciado M2](../Trabalho%20M2%20-%20STR%20.pdf).

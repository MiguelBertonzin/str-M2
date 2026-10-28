# Metodologia de testes M2

Baseei a metodologia nos itens 1 a 9 da página 4 e nos requisitos da página 5 do
[enunciado](../Trabalho%20M2%20-%20STR%20.pdf), considerando a temática Drone.
O roteiro de comandos e a organização dos resultados estão em
[ensaios_M2](../ensaios_M2/README.md).

## Seleção das configurações

Usei as perdas de deadlines hard como primeiro critério, seguidas das perdas
soft. Entre configurações sem perdas, adotei CUSTOM a 240 MHz como referência.
Os cenários 02 e 07 estão registrados em
`referencias/m1/tabelas_graficos/dados/resumo_cenarios.csv`.
A M1 contém uma execução por combinação: são referências experimentais, não
extremos garantidos para qualquer carga ou condição de rede.

| Cenário | Referência M1 | Política | Escalonador | CPU | Slicing | Rede |
|---|---|---|---|---|---|---|
| M_UDP | 02 | CUSTOM | Preemptivo | 240 MHz | ON | UDP |
| M_TCP | 02 | CUSTOM | Preemptivo | 240 MHz | ON | TCP |
| P_UDP | 07 | DM | Cooperativo | 80 MHz | ON | UDP |
| P_TCP | 07 | DM | Cooperativo | 80 MHz | ON | TCP |

Todos usam UNICORE, tick de 1 ms, MONITOR e TIME habilitadas. As duas tarefas novas
têm prioridade 1. No cooperativo, slicing ON é o valor configurado na M1; não
significa alternância preemptiva por tick. Os yields explícitos permanecem.

## Quantidade e condições dos ensaios

Planejei **20 coletas: cinco repetições de 180 s para cada um dos quatro cenários**.
R01 serve como piloto do próprio cenário e pode contar na bateria após a revisão.
O PDF pede vários testes, mas não fixa duração nem número de repetições.
Não incluí TIME OFF, RM, 160 MHz ou pilotos extras nesta matriz.

Antes de multiplicar as repetições, é necessário conferir R01 dos quatro cenários.
A ordem planejada alterna UDP/TCP entre rodadas para reduzir efeitos de variação
da rede. Entre repetições, a placa deve ser reiniciada, pois o fail-safe fica
travado e a janela BEGIN só pode começar uma vez por boot.

Mantenho constantes rede/AP, posição da placa e notebook, UNICORE, tick, duração,
amostragem de logs, intervalo SNTP e roteiro. Cada ensaio registra a configuração
efetiva, protocolo, versão das fontes e condições disponíveis da conexão.
Resultados de outras placas/redes precisam ser identificados separadamente.

## Estímulos e janela de medição

Optei por automatizar A/B/C/D por GPTimer. O firmware mantém o caminho ISR → filas
→ tarefas, mas não reproduz o sensor capacitivo. O PDF exige touch físico;
esta campanha não demonstra esse requisito nem mede latência de toque.
A substituição depende do aceite do professor.

O coletor espera conexão e primeira sincronização SNTP antes de enviar BEGIN.
A janela de 180 s é definida pelo relógio monotônico da ESP32. Aquecimento fica
registrado no serial, fora dos acumuladores. Jobs são incluídos pelo instante
do evento; a coleta final aguarda uma margem para conclusão dos jobs.

O roteiro GPTimer dura 26 s, com 13 alarmes e 14 eventos: A=1, B=5, C=7, D=1.
B+C ocorrem no mesmo alarme aos 22 s; D aciona fail-safe aos 24 s. A instrumentação
atual conta 13 jobs NAV do roteiro, incluindo A/STRESS. O PC também envia NAV,
PING e FS conforme a agenda registrada. FS remotos após D reafirmam o estado
seguro; não representam novos pousos independentes.

O instante de envio de BEGIN no PC e o início do GPTimer na ESP32 são diferentes.
Essa diferença e os atrasos de envio dos comandos são registrados, não presumidos nulos.

## Métricas e interpretação

- **Evento/início/fim:** a FUS usa liberações nominais por esp_timer; tarefas por
  evento usam a origem registrada. O instante da ação segura é separado do fim de FS.
- **Perdas:** liberações descartadas, sobrescritas e saltos de sequência são
  contabilizados. Ativação perdida não é ocultada por não produzir um job concluído.
- **WCRT/HWM:** maior resposta observada por tarefa e HWM100 exato dos jobs
  registrados. HWM99 é um intervalo do histograma de 100 us, com overflow acima
  de 102,4 ms. Não representam uma prova formal de WCET.
- **(m,k)-firm:** adotei (9,10) nas tarefas soft, por janelas de execuções
  consecutivas. Perdas ou timestamps inválidos impedem concluir atendimento
  sobre todas as ativações. Janelas não atravessam reinicializações.
- **Latência ISR:** separada das ativações do PC. No modo AUTO, começa no callback
  GPTimer; no modo TOUCH, no callback do sensor, não no primeiro contato do dedo.
- **CPU e execução:** ocupação de CPU vem das janelas completas do serial dentro
  do ensaio. Tempo decorrido do corpo da tarefa pode incluir preempções e esperas.
- **Comunicação:** sequência, protocolo, bytes de aplicação e timestamps de envio/
  recepção no PC e na ESP32. Não são timestamps do rádio nem tamanho total no enlace.
- **RTT e SNTP:** RTT usa o relógio monotônico do PC e inclui espera pelo monitor
  de 500 ms, escalonamento e retries. SNTP fornece referência UTC estimada; para
  diferenças entre dispositivos, considerar offset, deriva e assimetria da rede.

Os acumuladores locais consideram todos os jobs válidos, independentemente da
amostragem de logs individuais FUS/CTRL. A instrumentação tem custo de CPU,
memória e seções críticas; a revisão usada deve ser a mesma nos quatro cenários.

## Estado das coletas

M_UDP/R01_T02 concluiu a janela de 180 s, com zero misses, perdas de ativações ou
timestamps inválidos nas seis tarefas. Houve três novas sincronizações SNTP.
A tentativa anterior R01 foi interrompida pela retirada do cabo USB e não conta
como repetição completa.

A coleta completa permanece **REVISAR**, pois 34 comandos saíram mais de 500 ms
após a agenda prevista. O cliente espera o ACK antes de enviar o próximo comando;
RTT maior que o espaçamento previsto pode acumular atrasos. Preciso revisar esse
efeito na carga oferecida e na comparação UDP/TCP antes de fechar a metodologia.
Se a agenda ou o comportamento do coletor mudar, pode ser necessário repetir R01.

Faltam 19 coletas completas no plano. Isso não significa que a primeira já tenha
sido aprovada em todos os critérios. TCP e os cenários lentos compilam, mas ainda
precisam de validação na placa com a coleta atual.

## Análise e relatório

Vou consolidar os resultados por tarefa e cenário, incluindo WCRT/HWM, misses,
perdas, CPU, latência ISR e (9,10)-firm das soft. A comparação dos protocolos deve
considerar RTT, tentativas, perdas e efeitos nos deadlines, não apenas a média.

A análise de interferência e bloqueio depende das prioridades, das tarefas
Wi-Fi/lwIP/temporizadores e das seções críticas. Espera por mensagem não é, por si
só, inversão de prioridade. A aplicação não implementa mutex com PIP/PCP.

O critério de sucesso do PDF pede atendimento de todos os deadlines em vários
testes. Havendo perdas hard, elas devem constar como descumprimento nas condições
medidas. A tabela ilustrativa do enunciado não justifica concluir “OK hard” com
misses. COLETADO indica finalização do processo; não é um certificado de sucesso.

O relatório ainda precisa justificar hard/soft, consolidar tabelas/gráficos e
explicar as limitações. O ponto extra de acender o LED por FS remoto não foi
implementado. A documentação de reprodução está no README da raiz e o roteiro
presencial está em [APRESENTACAO.md](APRESENTACAO.md).

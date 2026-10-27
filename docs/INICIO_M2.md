# Análise inicial do enunciado M2

Atualização: a implementação inicial de MONITOR_TASK, Wi-Fi, UDP/TCP e cliente
PC foi acrescentada depois desta preparação. Veja [MONITOR_TASK](MONITOR_TASK.md)
para essa etapa, cujo teste funcional UDP foi realizado pelo usuário. A etapa
seguinte implementa [TIME_TASK/SNTP](TIME_TASK.md), cuja sincronização e contagem
foram confirmadas nos logs da placa. O próximo roteiro é a
[Metodologia de testes](METODOLOGIA_TESTES.md).

Fonte: `Trabalho M2 - STR .pdf`, cinco páginas, temática Drone. O enunciado
informa entrega em **29/10/2026 às 08h00**, código C/C++, GitHub e relatório PDF,
com demonstração presencial e compilação obrigatória.

## O que foi aproveitado da M1

Foi escolhido `C:/Users/Migue/Trab-STR-M1.2/main/main.c`, a implementação principal
com comentários e instrumentação. O manifesto em `referencias/m1/IMPORTACAO.json`
permite identificar exatamente os arquivos consultados e copiados.

Foram importados como referência: firmware, CMake, configurações históricas,
enunciado M1, roteiro de requisitos, relatório compilado, resumo de cenários,
métricas finais por tarefa, metodologia, auditoria e 18 logs aceitos. Os dados
continuam sendo **resultados históricos da M1**, não resultados do novo firmware.

Não foram importados build, ambientes Python, caches, ferramentas instaladas,
anotações provisórias nem todas as cópias de gráficos/LaTeX. A pasta `entrega_github`
não foi usada como projeto-base: seu CMake inclui `main.c` e `main1.c`, ambos com
`app_main`. Nenhum arquivo da pasta M1 ou do SDK foi alterado.

## Continuidade funcional

| Tarefa | Implementação preservada | Prazo |
|---|---|---|
| FUS_IMU | Período nominal de 5 ms, sinais simulados e filtro complementar | 5 ms |
| CTRL_ATT | Fila de estado de uma posição, PID e motores simulados | 5 ms após amostra |
| NAV_PLAN | Eventos B/C, rota e telemetria serial | 20 ms |
| FS_TASK | Evento D, estado seguro travado e motores zerados | 10 ms |
| METRICS / EXP_GUIDE | Auxiliares da M1, não substituem as novas tarefas M2 | Instrumentação |

Mantêm-se as prioridades CUSTOM, DM e RM. FUS/CTRL/FS são classificadas como
hard e NAV como soft na base M1; a justificativa e a revisão dessa classificação
fazem parte do relatório M2. Sensores inerciais, motores e trajetória são simulados.

## Cenários de partida apoiados nos dados

Critério inicial: quantidade/taxa de perdas hard, depois soft; entre cenários sem
perdas, escolher CUSTOM a 240 MHz para priorizar segurança e dar margem à rede.
Isso é uma escolha experimental documentada, não uma prova de melhor pior caso.

| Papel | Log M1 | Configuração | Perdas registradas |
|---|---|---|---|
| Melhor de referência (um dos empatados) | 02 | CUSTOM, preemptivo, 240 MHz, UNICORE | hard 0/14001; soft 0/12 |
| Pior por perdas hard entre os 18 | 07 | DM, cooperativo, 80 MHz, UNICORE | hard 73/14003; soft 5/12 |

Fonte: `referencias/m1/tabelas_graficos/dados/resumo_cenarios.csv` e logs
correspondentes em `referencias/m1/Validos`. A M1 possui apenas um ensaio por
combinação. A auditoria registra limitações de timestamps, filas e fases dos toques.
Não escolher o pior apenas pelo tempo de FS: outro critério pode selecionar outro cenário.

No SDK instalado, `configUSE_PREEMPTION` estava fixado em 0. O M2 gera uma cópia
local de `FreeRTOSConfig.h` com os valores escolhidos em menuconfig e a fornece ao
kernel e aos consumidores. O original permanece intacto. Esse mecanismo é específico
à estrutura validada do IDF 6.1 e falha explicitamente se as definições não forem encontradas.

## Etapas seguintes do M2

Roteiro original de planejamento: as etapas MONITOR_TASK e TIME_TASK abaixo ja
foram implementadas. UDP e duas sincronizacoes SNTP foram exercitados na placa
pelo usuario. O cliente atual e Python, seguindo os exemplos de PC do professor.
Atualizacao de 27/10: a instrumentacao e o coletor foram revisados; as baterias
fisicas seguem pendentes. Veja [ensaios_M2](../ensaios_M2/README.md).

1. **Validar esta base na ESP32:** roteiro automático, modo touch físico, 14 eventos,
   nenhuma perda de fila, 12 jobs NAV e 1 job FS por execução do roteiro. Repetir os ensaios.
2. **Revisar a instrumentação por job antes das coletas finais:** alinhar a referência
   de liberação da FUS aos ticks; registrar evento/início/fim de cada job; timestamp
   separado para a ação segura; contar saltos de sequência no CTRL. A base ainda
   usa acumuladores M1 e imprime apenas os últimos timestamps por tarefa a cada 5 s.
3. **MONITOR_TASK:** implementar Wi-Fi e comunicação PC por UDP e TCP, envio de logs
   e recebimento de comandos NAV/FS. O enunciado informa periodicidade e D=500 ms;
   adotar inicialmente T=500 ms como hipótese explícita a confirmar no modelo.
   Evitar I/O sem timeout e tratar mensagens parciais do TCP.
4. **TIME_TASK:** integração SNTP, estado de sincronização e contagem de ciclos desde
   a última sincronização. O enunciado informa periodicidade e D=1 s; adotar T=1 s
   inicialmente. Não confundir uma consulta ao relógio local com uma nova resposta SNTP.
5. **PC em C/C++ e protocolo:** definir sequência, tamanho, origem e timestamps;
   implementar echo/ACK para RTT. Usar tempo monotônico para deadlines e RTT local;
   usar hora SNTP com ms para correlação entre máquinas. A diferença de relógios
   afeta latência unidirecional e deve ser explicitada.
6. **Novas análises:** máximo observado de resposta por tarefa (WCRT experimental),
   HWM/percentis com definição explícita, janelas deslizantes (m,k)-firm, interferência,
   bloqueio, misses, CPU e latência ISR→task. Resumos agregados M1 não permitem
   reconstruir percentis nem a ordem de acertos/misses necessária ao (m,k)-firm.
7. **Matriz mínima:** melhor×UDP, melhor×TCP, pior×UDP, pior×TCP; múltiplas repetições
   com origem dos estímulos identificada, duração/pacotes/fases registrados.
   Variar a fase inicial em ensaios posteriores: repetir um roteiro fixo não cobre
   todas as fases possíveis de interferência.
8. **Extra:** acender o LED da placa por comando FS vindo do PC, confirmando o GPIO
   do modelo de placa. Produzir relatório e demonstração com resultados reais.

Não há mutex de aplicação implementando PIP/PCP nesta base: há filas e seções
críticas. Bloqueio esperando uma mensagem não deve ser automaticamente classificado
como inversão de prioridade. Revisar os novos locks e a rede ao ampliar o sistema.

O exemplo de tabela do PDF contém perdas hard e a conclusão “OK hard”; use os
resultados medidos e o critério escrito de atendimento dos prazos, não essa conclusão
ilustrativa. Não há alegação de cumprimento total do M2 nesta etapa.

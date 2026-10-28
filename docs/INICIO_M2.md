# Análise inicial do enunciado M2

Atualização: a implementação inicial de MONITOR_TASK, Wi-Fi, UDP/TCP e cliente
PC foi acrescentada depois desta preparação. Veja [MONITOR_TASK](MONITOR_TASK.md)
para essa etapa. Validei o fluxo funcional UDP na placa. A implementação de
[TIME_TASK/SNTP](TIME_TASK.md) também está concluída, com sincronização e contagem
confirmadas nos logs. A campanha atual está descrita na
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

## Estado atual e próximas etapas

Implementei MONITOR_TASK, TIME_TASK e o coletor Python, seguindo os exemplos de
comunicação com o PC disponibilizados pelo professor. A instrumentação atual
registra liberações nominais da FUS, perdas de ativações/amostras, ação segura,
WCRT, HWM e janelas (9,10)-firm. O roteiro passou a contabilizar A/STRESS como job
NAV: são 13 jobs NAV automáticos, além das ativações aceitas do PC.

A primeira coleta completa, M_UDP/R01_T02, terminou com zero misses nas seis
tarefas, mas permanece REVISAR por 34 comandos enviados com atraso na agenda.
O detalhamento está em [ensaios_M2](../ensaios_M2/README.md).

1. Revisar o efeito da espera pelo ACK na agenda e na comparação dos protocolos.
2. Validar na placa M_TCP, P_UDP e P_TCP antes de repetir a bateria em lote.
3. Concluir a matriz de quatro cenários, com cinco repetições de 180 s cada.
   Uma alteração de carga/metodologia pode exigir repetir a coleta já concluída.
4. Consolidar resultados por tarefa e cenário e analisar interferência, bloqueio,
   deadlines, CPU e latência. Justificar a classificação hard/soft no relatório.
5. Registrar a diferença entre os estímulos automáticos e o touch físico exigido
   no PDF. A campanha automática não comprova a latência do sensor capacitivo.
6. Implementar e validar o LED acionado por FS remoto, caso seja realizado o extra.
7. Finalizar relatório e demonstração usando resultados efetivamente coletados.

Não há mutex de aplicação implementando PIP/PCP nesta base: há filas e seções
críticas. Bloqueio esperando uma mensagem não deve ser automaticamente classificado
como inversão de prioridade. Revisar os novos locks e a rede ao ampliar o sistema.

O exemplo de tabela do PDF contém perdas hard e a conclusão “OK hard”; use os
resultados medidos e o critério escrito de atendimento dos prazos, não essa conclusão
ilustrativa. Não há alegação de cumprimento total do M2 nesta etapa.

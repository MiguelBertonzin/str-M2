# Auditoria e limites das medições

Os 18 registros permitem comparações experimentais úteis. Esta auditoria explica os limites da instrumentação para evitar que as tabelas sejam apresentadas como uma prova formal de escalonabilidade. **Nenhum resultado ou firmware foi corrigido nesta etapa.**

As referências de linha abaixo correspondem à [cópia de main.c consultada](contexto/main.c.txt). O comportamento foi confrontado com os campos dos logs. O código presente não substitui o arquivamento do binário usado em cada coleta.

## 1. `exec_max_us` não é um WCET garantido de CPU

`metrics_record`, por volta da linha 271, calcula `fim − início` com `esp_timer_get_time()`. Esse intervalo inclui o tempo em que a tarefa pode ter sido interrompida, preemptada ou bloqueada dentro do trecho instrumentado. Portanto, representa duração decorrida, frequentemente chamada wall-clock.

WCET, na análise clássica, é um limite superior do tempo de computação da tarefa sob condições definidas. Um máximo encontrado em uma bateria finita é um **máximo observado**, não um limite provado. Neste projeto existe ainda a interferência de outras tarefas dentro da medida.

No relatório, use “duração máxima observada do trecho instrumentado” ou “estimativa experimental de pior duração, incluindo interferência”. Para estimar custo de CPU de cada job, seria necessário instrumentar sua execução efetiva entre trocas de contexto ou separar trechos sem interferência com metodologia explícita. Nenhuma subtração retrospectiva foi feita.

O `for` com número fixo de operações torna a carga de cálculo controlável, mas não fixa a duração em microssegundos: frequência, interrupções, escalonamento e caminhos condicionais continuam interferindo. Os mínimos observados ajudam a visualizar a escala do custo base; também não são WCET.

## 2. FUS: referência em ticks e referência em microssegundos desalinhadas

Em `task_fus_imu`, linhas próximas de 581–584, `next_release` é obtido por `xTaskGetTickCount()` e `expected_release_us` por outra leitura, `esp_timer_get_time()`. A espera periódica usa `vTaskDelayUntil` sobre a grade de ticks, enquanto o evento esperado em microssegundos é incrementado a partir daquela leitura independente.

Assim, a tarefa pode iniciar antes do timestamp designado como seu evento esperado. Essa situação aparece nos snapshots dos logs e está mostrada na [figura 11](graficos/11_auditoria_fase_fus.png).

Exemplo do último snapshot FUS do cenário 01:

| Campo | Valor (µs) |
| --- | ---: |
| Evento esperado | 35.042.128 |
| Início | 35.042.024 |
| Fim | 35.044.052 |
| Início − evento | −104 |
| Duração deste job: fim − início | 2.028 |
| Resposta deste job: fim − evento | 1.924 |

O início ocorre 104 µs antes da referência armazenada. Como a latência é truncada em zero, a relação usual `R = L + E` deixa de valer nesse caso. O jitter absoluto conta esse deslocamento de fase como parte do desvio.

**Impacto:** latência, jitter, resposta e classificação de misses da FUS dependem de uma referência que precisa de revisão. A diferença pode alterar o resultado de jobs próximos do deadline. Não é correto simplesmente subtrair um offset dos máximos ou alterar os contadores: não há timestamp de todos os jobs nos logs.

As tabelas preservam os misses reportados e usam essa expressão explicitamente. Uma futura instrumentação deveria definir a grade de liberação em uma referência comum e coerente com o mecanismo de espera. A consistência `evento ≤ início ≤ fim`, quando essa for a semântica escolhida, precisa ser conferida por job.

## 3. Existem duas definições de jitter

Na FUS, o código usa `|início − liberação_esperada|`. Nas tarefas encadeadas ou por evento, usa a variação absoluta da latência de início entre jobs consecutivos. O primeiro job dessas tarefas recebe jitter zero.

Consequências:

- Os valores de FUS e CTRL têm definições diferentes e aparecem em painéis separados no gráfico.
- Jitter zero de FAIL_SAFE é consequência de existir apenas um job. Não demonstra repetibilidade da emergência.
- A média das tarefas por evento divide a soma por todos os jobs, incluindo o primeiro zero. Isso difere de dividir por `jobs − 1` para uma média das diferenças entre pares.
- Não há base para obter jitter pico a pico do período real da FUS em todos os jobs a partir desses resumos.

Para uma futura análise, explicitar a definição desejada e armazenar os intervalos reais de início, ou estatísticas específicas deles, resolveria a ambiguidade. Os gráficos atuais usam estritamente as definições existentes.

## 4. O instante do evento touch é o instante observado pela ISR

O callback touch lê `esp_timer_get_time()` e encaminha o evento à tarefa, próximo das linhas 441–488. A medida `início − evento` corresponde à ativação desde a detecção em software na ISR.

O tempo entre o contato físico e a detecção pelo periférico não foi registrado. Calibração, limiar, varredura e filtragem do touch podem acrescentar atraso anterior ao timestamp. Portanto, “latência desde a ISR” é uma descrição sustentada pelos dados; “latência total desde o primeiro contato físico” exigiria uma referência externa, por exemplo uma marcação elétrica observável.

O intervalo em que o operador lê o roteiro e decide tocar não é somado diretamente a R ou L, mas determina a fase em que o evento encontra as tarefas. Logo, a coordenação manual continua sendo uma fonte de variação entre ensaios.

## 5. FAIL_SAFE zera motores antes do fim medido

Em `task_fail_safe`, aproximadamente linhas 828–839, a ordem é:

```text
registrar início
entrar na seção crítica
ativar modo seguro e escrever motores = 0
sair da seção crítica
executar carga artificial fixa
registrar fim
```

Assim, R mede até o final de todo esse trecho. No cenário 05, R=14,792 ms e o código conta uma perda de D=10 ms. É uma perda do deadline atribuído à **conclusão do job instrumentado**. Não se pode afirmar que os motores só foram zerados aos 14,792 ms, pois a escrita aconteceu antes e não tem timestamp próprio.

Da mesma forma, o campo L não é exatamente o tempo de zerar motores: existe código e uma seção crítica entre o início e a escrita. A impressão “motores zerados” ocorre depois da carga e não fornece o timestamp dessa escrita.

O enunciado combina baixa latência de segurança com redução imediata do throttle. Para demonstrar as duas coisas separadamente em nova coleta, convém registrar `evento`, `início`, `motores_zerados` e `fim`. Os logs atuais comprovam estado final seguro e tempo de conclusão do trecho, com esta limitação temporal.

## 6. A fila FUS → CTRL pode sobrescrever amostras

A FUS publica por `xQueueOverwrite` em uma fila de uma posição. Isso mantém o estado mais recente e pode substituir um estado ainda não consumido. O CTRL usa o timestamp da amostra e registra somente jobs concluídos.

A diferença `jobs_FUS − jobs_CTRL` no resumo é um indicador de defasagem. Ela pode incluir amostras sobrescritas, um job em execução ou uma amostra aguardando consumo; os snapshots também são obtidos em momentos específicos. **Não é um contador exato de amostras perdidas.**

Diferenças de 34, 26 e 27 jobs em CUSTOM cooperativo a 80, 160 e 240 MHz são compatíveis com o mecanismo de sobrescrita sob atraso. Para quantificá-las exatamente seria necessário registrar saltos no número de sequência consumido, além dos jobs pendentes.

Um job que nunca chega a ser processado pelo CTRL também não aumenta seu contador de deadline miss. Portanto, “zero misses do CTRL” não implica que todas as amostras da FUS foram controladas. O campo `descartados=0` do touch pertence a outro caminho de eventos e não elimina essa limitação.

## 7. NAV agrega rota e telemetria, com limites de medição diferentes

NAV_PLAN processa B e C na mesma tarefa e usa o mesmo acumulador. Em cada ensaio há 5 comandos B e 7 solicitações C, totalizando 12 jobs. A taxa 5/12=41,667% é da tarefa agregada. Não deve ser automaticamente anunciada como taxa de falha de comandos B, pois o resumo não identifica a distribuição de misses por tipo.

No caminho C, a fotografia do estado é capturada e o fim é medido antes de transmitir o `printf`. A medida cobre a preparação da telemetria instrumentada; não garante o tempo até terminar a transmissão UART ou até a linha aparecer no computador.

O tempo de impressão ainda ocupa/bloqueia a tarefa e pode interferir em eventos seguintes, mesmo quando está fora do intervalo de execução registrado. Uma instrumentação futura pode separar B, preparação C e transmissão C, mantendo a origem de cada deadline explícita.

## 8. Cooperativo configurado, com possibilidade de troca solicitada pela ISR

Os banners `preemption=0` demonstram a configuração compilada reportada pelo firmware. Porém, o callback touch retorna `higher_priority_task_woken`, e o driver consultado do ESP-IDF chama `portYIELD_FROM_ISR()` quando esse retorno indica a necessidade de troca.

No SDK local, a cadeia consultada foi (os trechos do driver e da macro estão preservados em [trechos_sdk.txt](contexto/trechos_sdk.txt), com caminho e hash):

- `esp_driver_touch_sens/hw_ver1/touch_version_specific.c`: callback de ativação → `need_yield` → `portYIELD_FROM_ISR()`.
- `FreeRTOS-Kernel/queue.c`: envio a partir da ISR pode marcar `pxHigherPriorityTaskWoken`.
- `FreeRTOS-Kernel/portable/xtensa/include/freertos/portmacro.h`: solicitação de troca na saída da interrupção.

Esse caminho não contém, no ponto consultado, uma condição que suprima o yield solicitado por `configUSE_PREEMPTION=0`. Ele permite interpretar as latências pequenas de FS mesmo em parte das execuções cooperativas.

A descrição correta é **configuração cooperativa do FreeRTOS com yields explícitos, inclusive solicitados pelo caminho da ISR**. Evite afirmar que a execução é um cooperativo puro em que toda tarefa necessariamente termina antes de qualquer atendimento a D. Bloqueios em filas e chamadas de I/O também podem permitir reescalonamento.

Alterar essa semântica seria uma decisão de implementação para outro experimento. Nesta etapa foi apenas documentada; o SDK e o firmware não foram alterados.

## 9. RM e DM coincidem nas prioridades implementadas

Ambas atribuem FUS=5, CTRL=5, FS=4 e NAV=3. Com o mesmo modo, slicing e frequência, executam a mesma relação de prioridades de aplicação. Isso explica a proximidade dos resultados e impede atribuir diferenças isoladas a uma suposta ordenação distinta.

Para DM, D_FUS=D_CTRL=5 ms, D_FS=10 ms e D_NAV=20 ms justificam diretamente essa ordem. CTRL é encadeada; não é uma tarefa periódica independente no sentido mais simples da teoria clássica.

Para RM, a frequência nominal de FUS/CTRL é 200 Hz. O código usa o intervalo mínimo de aceitação touch para justificar os eventos esporádicos. Há uma ressalva: o debounce é por canal, e NAV reúne B+C. B e C podem chegar próximos um do outro; não há um intervalo mínimo agregado de 1.200 ms para a tarefa NAV. O tratamento de FS/NAV e seu desempate por criticidade devem ser descritos como a **adaptação RM adotada**, não como uma modelagem formal completa de todas as chegadas.

Os limites clássicos simples de utilização RM não devem ser aplicados diretamente a essa combinação de tarefas encadeadas, chegadas agrupadas, bloqueios, filas com sobrescrita e medidas wall-clock.

## 10. CPU, repetição e configuração histórica

`task_metrics` consulta runtime sequencialmente por tarefa, usando uma referência de intervalo do relatório. As consultas e impressões não são simultâneas. A média ponderada derivada é uma aproximação da ocupação atribuída às quatro tarefas; o restante não pode ser identificado automaticamente como IDLE. Tarefas auxiliares, sistema e atribuição do tempo de interrupções dependem do mecanismo de runtime.

Existe uma única execução de cada combinação e um FS por execução. Não há estimativa de variabilidade entre repetições nem evidência suficiente para ordenar configurações por pequenas diferenças de microssegundos. Máximos dependem da duração, da quantidade de eventos e das fases testadas.

Os logs não imprimem `configUSE_TIME_SLICING`. ON/OFF foi associado pelo histórico documentado da coleta e pelos nomes dos ensaios OFF. A alteração posterior do SDK não comprova retroativamente esse valor no binário de cada ensaio. Preservar hash do firmware e imprimir slicing, tick, carga fixa e versão do instrumento melhoraria a rastreabilidade.

O roteiro confirma ordem e contagem B/C; não mede um limite máximo de separação que demonstre simultaneidade física. E o estresse é contado em 2.000 amostras da FUS, nominalmente 10 s, em vez de um temporizador independente de duração exata.

## Como aproveitar os dados existentes

As comparações de frequência, ocupação aproximada, perdas reportadas e respostas do trecho completo de FS continuam úteis. A matriz e os extras podem ser discutidos como uma investigação experimental exploratória. As conclusões devem mencionar as limitações relevantes; não é necessário ocultar resultados desfavoráveis ou alterar os logs.

Para elevar a análise a uma demonstração mais rigorosa, uma nova rodada deveria priorizar: referência temporal coerente da FUS; timestamp da ação de segurança; separação de CPU e tempo decorrido; contadores de sequência no CTRL; definição explícita da semântica cooperativa; e repetições com estímulos em fases controladas. Estes são encaminhamentos, **não mudanças executadas nesta tarefa**.

## Referências técnicas

O enunciado e o código local são as fontes primárias para os requisitos e para a semântica efetivamente implementada. A documentação do [ESP Timer da Espressif](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_timer.html) descreve a base de tempo em microssegundos. O [guia oficial do FreeRTOS](https://www.freertos.org/media/2018/161204_Mastering_the_FreeRTOS_Real_Time_Kernel-A_Hands-On_Tutorial_Guide.pdf) apresenta os modos de escalonamento e o papel de bloqueios/yields; os detalhes da adaptação Xtensa foram conferidos no SDK local.

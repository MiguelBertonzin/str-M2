# Metodologia de testes M2 — roteiro de preparação

> Registro histórico da preparação. Para o estado atual, consulte o README da raiz
> e ensaios_M2/README.md: 20 coletas planejadas, R01 como piloto incluído, sem P01 extra.
> Já há M_UDP/R01_T02 completa com ressalva de agenda; os demais cenários aguardam validação.

Base: seção “Metodologia de teste”, página 4, e itens importantes da página 5
de `Trabalho M2 - STR .pdf`. A preparação foi implementada em 27/10/2026 como
M2-METRICS-2; as baterias ainda não foram executadas. O passo a passo atualizado
está em [ensaios_M2](../ensaios_M2/README.md), com a matriz individual e comandos.

## Estado confirmado

- Teste funcional UDP: HELLO/PING, NAV, FS e bloqueio de NAV após FS funcionaram.
- TIME_TASK: dois logs fornecidos pelo usuário confirmaram duas sincronizações
  separadas por aproximadamente 60,029 s, reinício do contador e envio ao PC.
  No resumo de 120 s: 119 jobs TIME, zero misses e Rmax observado de 40,217 ms.
- TCP compila; o teste funcional na placa ainda está pendente.
- Os ensaios de desenvolvimento atuais não substituem a matriz abaixo.

## Matriz exigida

Critério de seleção da M1: perdas de deadlines hard, seguido das soft. Entre
configurações sem perdas, escolher CUSTOM/240 MHz. Referência: cenários 02 e 07
de `referencias/m1/tabelas_graficos/dados/resumo_cenarios.csv`. A M1 tinha apenas
um ensaio por combinação: são referências históricas, não extremos garantidos.

| Cenário | Referência M1 | Política | Escalonador | CPU | Slicing configurado | Rede |
|---|---|---|---|---|---|---|
| Melhor–UDP | 02 | CUSTOM | Preemptivo | 240 MHz | ON | UDP |
| Melhor–TCP | 02 | CUSTOM | Preemptivo | 240 MHz | ON | TCP |
| Pior–UDP | 07 | DM | Cooperativo | 80 MHz | ON | UDP |
| Pior–TCP | 07 | DM | Cooperativo | 80 MHz | ON | TCP |

Todos em UNICORE, com MONITOR e TIME habilitadas. Slicing ON no modo cooperativo
é o valor configurado na M1, não a afirmação de alternância preemptiva por tick.
Manter/documentar os yields explícitos herdados. Registrar também as prioridades
das novas tarefas (atualmente ambas 1); o empate precisa constar da análise.

TIME OFF versus ON é diagnóstico adicional de impacto, não substitui os quatro
cenários pedidos. Não é necessário repetir toda a matriz de frequências da M1.

## Checklist de preparação e situação atual

Os itens 1 a 6 abaixo descrevem os problemas que motivaram a preparação. Agora:
FUS usa esp_timer com liberações nominais; perdas de FUS/CTRL/filas são contadas;
FS tem timestamp de ação segura; estatísticas completas são calculadas localmente
(HWM99 limitado por bins de 100 us); o PC consulta SNTP e registra cada envio;
BEGIN coordena a janela e o roteiro. Os testes físicos da nova revisão permanecem
pendentes. O checklist original está mantido abaixo como histórico dos critérios.

1. **Referência temporal:** corrigir a liberação nominal de FUS, que atualmente
   pode aparecer depois do início real. Documentar exatamente o que evento,
   início e fim significam em cada tarefa. Separar a ação segura do fim de FS.
2. **Eventos perdidos:** contar amostras sobrescritas na fila FUS→CTRL e distinguir
   um job executado com atraso de uma ativação perdida. Registrar a política
   usada para ambas na análise; não esconder perdas por excluir jobs inexistentes.
3. **Coleta por job:** obter sequência e evento/início/fim suficientes para cada
   distribuição e janela (m,k). A configuração atual envia 1 de cada 100 jobs
   FUS/CTRL e tem filas limitadas. Apenas mudar N para 1 pode saturar o monitor;
   é necessário dimensionar/compactar o registro ou calcular estatísticas locais
   verificáveis e registrar todos os descartes. Medir o custo da instrumentação.
4. **Comunicação:** registrar identificador do ensaio, sequência, protocolo,
   tamanho em bytes e timestamps nas chamadas de envio/recepção da aplicação
   no PC e na ESP32. A data atual `queued_unix_ms` marca a montagem do ACK;
   não é um timestamp do rádio. Falta timestamp de calendário no envio do PC
   e documentação/verificação da sincronização desse relógio.
5. **RTT:** conservar o cálculo monotônico no PC. Explicitar que inclui espera
   pelo ciclo de 500 ms do monitor, escalonamento e retransmissões. Não tratar
   diferenças entre relógios dos dispositivos como latência exata sem avaliar
   o erro de sincronização. Separar ACK de conclusão da tarefa acionada.
6. **Roteiro controlado:** automatizar comandos e coleta. Definir um início comum
   após conexão do cliente e primeira sincronização; registrar aquecimento
   separadamente. Hoje o roteiro GPTimer começa no boot e termina aos 26 s,
   possivelmente antes de o PC conectar. Isso precisa ser coordenado na bateria.
7. **Piloto TCP:** repetir HELLO/PING/NAV/FS/NAV, verificando QUEUED/JOB/SAFE_ACTIVE.
   Executar um piloto de coleta também no cenário lento antes de repetir em lote.

## Proposta de execução (escolhas experimentais, não números impostos pelo PDF)

- Começar com um piloto por cenário. Após validar a coleta, adotar inicialmente
  **5 repetições de 180 s de medição por cenário**, além do aquecimento.
- Reiniciar a placa entre repetições. O fail-safe fica travado; planejar FS no
  final da fase de navegação e observar o estado seguro depois. A duração/agenda
  final do roteiro deve ser fixada no piloto, incluindo quantidade suficiente de
  NAV e FS para a análise desejada; cinco FS isolados não estimam bem percentis.
- Registrar condições equivalentes: rede/AP, localização, sinal, tamanho/cadência
  de mensagens, estímulos, CPU, prioridades e fases de carga. Alternar a ordem
  UDP/TCP entre repetições para reduzir o efeito de mudanças da rede.
- Registrar se as novas atualizações SNTP ocorreram dentro da janela medida.
  Em falhas de sincronização, conservar o log e marcar a condição; não inventar UTC.
- Guardar log serial bruto, JSONL do PC e configuração efetiva por ensaio.
  Perdas de eventos/logs são resultados a reportar. Coleta incompleta não autoriza
  apresentar percentis ou janelas como se representassem todos os jobs.

## Resultados e relatório

- Por tarefa e cenário: jobs, ativações perdidas, misses, WCRT observado (máximo
  de fim−evento), distribuição/HWM e definição explícita do percentil adotado,
  latência ISR→início e ocupação de CPU.
- Para tarefas soft: justificar a classificação e escolher (m,k) antes de analisar
  resultados. Sugestão inicial: (9,10), que é exemplo do enunciado, não imposição.
  Aplicar janelas deslizantes por tarefa e por ensaio; não atravessar reinicializações.
- Comparar RTT, perdas, tentativas e falhas UDP/TCP junto ao efeito nos deadlines.
  O protocolo preferido depende desses resultados, não apenas da média de RTT.
- Descrever interferência por prioridades, tarefas Wi-Fi/lwIP/temporizadores e
  seções críticas. Distinguir espera por mensagem de bloqueio por inversão de
  prioridade; não presumir PIP/PCP onde só existem filas e portMUX.
- O critério de sucesso do PDF pede todos os deadlines atendidos em vários testes.
  Havendo misses, reportar descumprimento e suas condições. A tabela ilustrativa
  do PDF não serve de justificativa para rotular perdas hard como “OK hard”.

O PDF exige touch físico para interrupções. O GPTimer preserva a lógica e ajuda
na repetibilidade, mas seus tempos devem ser identificados como sintéticos.
Manter a verificação física separada; qualquer substituição para a avaliação
depende do aceite do professor. O ponto extra do LED continua opcional e pendente.

**Próximo passo de execução:** rodar M_UDP_P01 com o coletor e conferir o resultado.
Depois, executar os demais pilotos e a matriz, gerar tabelas/gráficos e
redigir a análise. O firmware é C; o cliente Python segue os exemplos de PC
fornecidos pelo professor, cuja utilização é expressamente permitida no item 2.

# Material para o relatório futuro

O relatório final ainda não foi produzido. Usar as coletas conferidas de
`02_coletas` e manter anexos históricos de `03_anexos_chat` separados.

Estrutura sugerida:

1. Identificação, enunciado e objetivo do drone didático.
2. Arquitetura: seis tarefas, prioridades, filas, esp_timer, GPTimer/touch,
   Wi-Fi/SNTP, comandos e comportamento latched do fail-safe.
3. Justificativa hard/soft e definição de cada timestamp/deadline.
4. Seleção de melhor/pior M1, matriz UDP/TCP, repetições e controles experimentais.
5. Instrumentação: perdas, histograma/HWM, janelas (9,10), CPU, custo e limitações.
6. Resultados por cenário/tarefa: tabelas CSV, distribuições com precisão declarada,
   RTT, execução, WCRT observado, perdas e efeito no cumprimento de prazos.
7. Interferência e bloqueio: tarefas do SDK, prioridades e seções críticas.
8. Comparação dos protocolos com evidências e critérios de preferência.
9. Demonstração física touch, limitações, conclusões e trabalhos pendentes.

Não usar percentis de um ensaio misturados com o WCRT de outro sem identificar
a agregação. Não atravessar reinicializações nas janelas (m,k). Distinguir
falha funcional, falha de prazo, perda de ativação e perda de observabilidade.

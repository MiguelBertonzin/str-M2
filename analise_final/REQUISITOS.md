# Correspondência com o enunciado

Referência: [PDF da M2](../Trabalho%20M2%20-%20STR%20.pdf), temática Drone.

| Item | Evidência neste material | Delimitação |
|---|---|---|
| 1 — Melhor/pior da M1 | Matriz e CSVs M1 em referencias/m1; metodologia | Extremos experimentais da M1, não garantidos para toda carga |
| 2 — UDP/TCP por Wi-Fi | 5 repetições por cenário; RTT e falhas em TABELAS.md | UDP teve melhor resultado observado; rede e ordem não foram controladas aleatoriamente |
| 3 — SNTP, timestamps e RTT | sntp_por_repeticao.csv, timestamps_comunicacao.csv, ping_amostras.csv | UTC não garante 1 ms; RTT monotônico inclui atendimento do monitor |
| 4 — WCRT/HWM | tarefas_por_repeticao.csv e tarefas_por_cenario.csv; figura 02 | Máximo observado; HWM99 intervalar por execução, não percentil agregado |
| 5 — (m,k)-firm | (9,10), figura 07, janelas por tarefa | NAV viola nos P; não formar janelas entre reinicializações |
| 6 — Interferência/bloqueio | DISCUSSAO.md e arquitetura | Sem decomposição causal de cada miss; aplicação não usa PIP/PCP |
| 7 — Todos os deadlines em vários testes | Zero falhas nas cinco M_UDP | Não cumprido nos outros três cenários |
| 8 — Logs, misses, ISR e CPU | Dados brutos e figuras 01, 05, 06 | Logs periódicos amostrados; ISR GPTimer não é touch físico |
| 9 — Tabela comparativa | TABELAS.md e oito CSVs | Classes, unidades e denominadores explícitos |

O firmware tem seis tarefas e contempla NAV/FS remotos, sincronização SNTP
e ciclos desde a atualização. O modo físico usa os sensores touch da ESP32,
mas a campanha apresentada foi automática. A exigência literal de interrupções
por touch requer demonstração/coleta física ou aceite da substituição.
Não há evidência física nova apenas pela adição dos argumentos de seleção.

O ponto extra de acender LED ao receber FS remoto não foi implementado.
O relatório final em PDF deve incluir identificação dos autores, enunciado,
método, resultados, discussão, limites e referências, conforme o enunciado.
Os documentos deste diretório são a base técnica para sua elaboração.

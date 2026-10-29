# Evidências complementares de seleção da amostra

As tabelas principais usam as pastas ativas R01 a R05 de cada cenário.
As duas primeiras execuções R01 abaixo foram repetidas e acompanham a
publicação para preservar a proveniência dos resultados.

| Pasta | Primeira execução | Execução incluída na tabela principal |
|---|---|---|
| P_UDP_R01_primeira | 423 hard, 15 soft; 21/22 registros NAV recebidos | 403 hard, 15 soft; 22/22 registros NAV |
| P_TCP_R01_primeira | 634 hard, 15 soft; coleta já conferida | 628 hard, 15 soft; coleta conferida |

A primeira TCP não foi inválida. Usá-la no lugar da R01 ativa produz 3087
falhas hard no total P_TCP, em vez de 3081, sem alterar a ordenação observada
entre os cenários. A primeira UDP evidencia uma ausência de registro,
sem provar por si só perda no rádio. Nenhuma dessas execuções deve ser
ocultada ou classificada como descartável por apresentar mais falhas.

Essas pastas não integram automaticamente as cinco repetições principais;
não duplicar seus dados na agregação. A análise explicita a seleção e seus
limites em [METODOLOGIA.md](../analise_final/METODOLOGIA.md).

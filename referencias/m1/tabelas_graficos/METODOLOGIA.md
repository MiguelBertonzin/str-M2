# Como os dados foram processados

## Fontes e unidade de análise

Foram lidos os 18 arquivos `.txt` em [Validos](../Validos), sem modificá-los. O [inventário](dados/inventario.csv) associa cada ID ao nome original, SHA-256, tamanho e configuração impressa. Os IDs **01–08** são a matriz principal; **E01–E04**, o extra de 160 MHz; **E05–E06**, time slicing desligado; **E07–E10**, RM.

Uma unidade experimental é uma execução completa da placa. Linhas sucessivas de METRIC são resumos da mesma execução; não constituem repetições independentes. Há uma execução por combinação coletada e um único job FAIL_SAFE por execução.

Para interpretar a instrumentação, foram consultados [main.c](contexto/main.c.txt), [sdkconfig](contexto/sdkconfig.txt), as anotações e o [enunciado completo](enunciado_extraido.txt). As cópias de contexto são o estado disponível durante a análise. Sem um hash de firmware em cada log, não é possível demonstrar que todos os detalhes de instrumentação eram idênticos em cada binário histórico.

## Verificações aplicadas

O script confere, para cada arquivo:

- Configuração de política, preempção, frequência e um núcleo no banner; consistência da política nas métricas.
- Duração de pelo menos 30 s, guia concluído e sequência final `CBCABCBCBCBCDC`.
- Contagem touch A=1, B=5, C=7 e D=1; descartes touch reportados iguais a zero.
- Evidência de NAV e telemetria durante estresse e de motores zerados com fail-safe ativo.
- Quatro tarefas por instante de resumo, colunas completas, contadores não decrescentes e perdas menores ou iguais aos jobs.
- Coerência do percentual de perdas com `100 × misses / jobs`, respeitando arredondamento de três casas; mínimo ≤ média ≤ máximo.
- Ausência das mensagens de watchdog, Guru Meditation e erros de sequência procuradas pelo parser.

Isso verifica a consistência do registro e a execução do protocolo. Não demonstra ausência de qualquer falha possível de hardware, nem prova atendimento universal de deadlines. Textos corrompidos em trechos pontuais da saída foram mantidos nas fontes; as linhas METRIC utilizadas passaram pelas verificações estruturais.

## Estatísticas acumuladas e janelas

`jobs`, `misses`, mínimos, médias e máximos são acumulados desde o início. Para a tabela principal é usado o **último conjunto completo de quatro tarefas**. Somar linhas METRIC consecutivas contaria os mesmos jobs várias vezes.

A taxa por tarefa é:

```text
perdas_% = 100 × misses_acumulados / jobs_acumulados
```

A classe hard adotada reúne FUS_IMU, CTRL_ATT e FAIL_SAFE. Sua taxa agregada usa a soma de perdas dividida pela soma de jobs, e não a média dos três percentuais. NAV_PLAN representa a classe soft. O agregado hard é acompanhado obrigatoriamente da tabela por tarefa, pois milhares de jobs periódicos diluem o peso de um único evento de emergência.

Para uma janela entre dois resumos:

```text
novos_jobs   = jobs_atual − jobs_anterior
novas_perdas = misses_atual − misses_anterior
taxa_janela  = 100 × novas_perdas / novos_jobs
```

Na primeira janela os contadores anteriores valem zero. Quando não há novos jobs, a taxa fica vazia no CSV, evitando inventar uma taxa observada.

`cpu_pct` é uma medida da janela, baseada em runtime do FreeRTOS. Sua média ao longo do ensaio foi aproximada por:

```text
CPU_média_tarefa = Σ(cpu_pct_janela × duração_janela) / duração_total
```

Os intervalos são derivados de `tempo_s`, impresso com resolução de milissegundos. A consulta sequencial de runtime e os arredondamentos limitam a precisão. A soma das quatro tarefas não é a ocupação total da CPU. Veja [auditoria](AUDITORIA_MEDICOES.md).

## Tempos e unidades

Os logs usam microssegundos; tabelas e figuras usam milissegundos quando indicado: `ms = us / 1000`. Não houve normalização artificial dos tempos pela frequência.

As definições reproduzem `metrics_record`:

```text
E = max(fim − início, 0)              duração decorrida do trecho
R = max(fim − evento, 0)              resposta desde a referência do evento
L = max(início − evento, 0)           latência de início
miss = R > deadline
```

FUS usa uma referência esperada periódica; CTRL usa o instante de amostragem colocado pela FUS; NAV/FS usam o timestamp no callback da interrupção touch. Não são referências físicas equivalentes. O problema de fase da FUS impede pressupor `R = L + E` em todas as linhas. Para o FS, com um único job por cenário, a decomposição da figura 03 corresponde ao mesmo job e pode ser feita.

Jitter FUS é `|início − evento_esperado|`. Jitter das outras tarefas é a diferença absoluta entre latências de início consecutivas, com zero para o primeiro job. O máximo observado de E é chamado **duração máxima observada do trecho**, sem promover esse valor a limite garantido de WCET.

## Durações diferentes e comparação

Os resumos finais variam entre 30,000 e 35,015 s. As comparações principais mantêm toda a sequência concluída e mostram contagens, denominadores e percentuais. Não se multiplicaram contadores de 30 s para fabricar resultados de 35 s.

O arquivo [recorte próximo de 30 s](dados/recorte_proximo_30s.csv) seleciona o resumo mais próximo de 30 s, com tolerância de 30 ms. A coluna `guia_concluido_nesse_instante` informa se a sequência já terminou. Algumas emergências ocorreram depois desse recorte; por isso ele não substitui as comparações finais do fail-safe. É útil, por exemplo, para comparar contadores periódicos de DM/RM em durações próximas.

Não foram calculados percentis, histogramas de jobs, desvio-padrão ou intervalos de confiança: os resumos não contêm a distribuição individual necessária. Os gráficos de evolução não identificam o instante exato de cada perda dentro de uma janela.

## Integridade e reprodução

O [gerador](gerar_analise.py) calcula hashes antes e depois do processamento, registra as versões de Python e bibliotecas e escreve somente em `tabelas_graficos`. Cada linha extraída inclui o arquivo e a linha original. A verificação está em [integridade.json](dados/integridade.json).

As figuras foram produzidas com Matplotlib, sem interpolação de observações não coletadas. Linhas que conectam frequências ou janelas ajudam a leitura; não representam um modelo ajustado nem uma previsão para outras frequências. Os formatos vetoriais PDF e SVG acompanham os PNG.

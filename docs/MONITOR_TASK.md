# Etapa M2: MONITOR_TASK

A ESP32 agora disponibiliza um servidor Wi-Fi para envio de eventos e comandos
NAV/FS vindos do PC. O protocolo UDP ou TCP e selecionado em menuconfig, um por
compilacao. A origem dos exemplos e o material do professor preservado em
`referencias/professor/Network_ESP32/`. O firmware e C; o cliente de laboratorio
em Python usa sockets da biblioteca padrao, como os exemplos de PC fornecidos.

## Primeiro teste na placa

1. Abra `idf.py menuconfig`, menu **M2 - Drone e experimentos**.
2. Habilite **MONITOR_TASK e Wi-Fi**. Preencha SSID e senha de uma rede 2,4 GHz
   acessivel pela ESP32 e pelo PC. As credenciais ficam em `sdkconfig`, ignorado
   no Git; nao as copie para arquivos de configuracao versionados.
3. Para este teste, escolha **Somente comandos do PC** como origem de eventos.
   Isso evita o D automatico aos 24 s. Escolha **UDP** e mantenha porta **5000**.
4. Compile e grave com `idf.py -p COMx flash monitor`.
5. Aguarde `MONITOR_IP | 192.168... | UDP | porta=5000`. Em outro terminal na
   raiz do projeto, execute:

```text
python pc/monitor_pc.py --host IP_DA_ESP32 --protocol udp --output monitor_udp.jsonl
```

O cliente envia HELLO automaticamente. Digite `PING`, `NAV`, `NAV`, `FS` e,
depois, `NAV`. Os dois primeiros NAV devem receber ACK `QUEUED` e gerar jobs
`NAV_PLAN` com `source=PC_UDP`. FS deve receber `QUEUED`, gerar `FAIL_SAFE` e
zerar os motores; o NAV posterior deve responder `SAFE_ACTIVE`. O fail-safe nao
e desarmado pelo PC: reinicie a placa para repetir o teste funcional.

Os jobs do comando aparecem no cliente como `JOB:`. No serial continuam os
logs de waypoint e motores. `QUEUED` confirma o enfileiramento, enquanto o
registro `job` indica a conclusao do trecho instrumentado. `BUSY` informa que
nao havia espaco na fila e a operacao nao foi aceita.

Para TCP, encerre o cliente, troque o protocolo no menuconfig, compile/grave e rode:

```text
python pc/monitor_pc.py --host IP_DA_ESP32 --protocol tcp --output monitor_tcp.jsonl
```

Se o SSID estiver vazio, a tarefa imprime uma orientacao e continua suas
liberacoes periodicas sem iniciar Wi-Fi; o drone segue executando. Credenciais
erradas ou desconexao geram tentativas de reconexao a intervalos de pelo menos
5 s. Quando a conexao cair, abra novamente o cliente apos a ESP32 obter IP.
O modo touch fisico e o roteiro automatico continuam disponiveis para outros testes.

## Modelo temporal

- Periodo adotado: **500 ms**, com deadline relativo **500 ms**. O PDF especifica
  tarefa periodica e D=500 ms; T=500 ms e a escolha de modelagem desta etapa.
- Prioridade: **1**, abaixo das tarefas funcionais de todas as politicas M1.
  MONITOR e classificada inicialmente como soft: atraso degrada observabilidade
  e comandos remotos; a seguranca local por touch continua independente.
- Um ESP Timer periodico publica a liberacao em uma fila curta. O callback nao
  faz rede nem imprime; a MONITOR_TASK executa os jobs.
- `evento_us` e a liberacao nominal do ciclo; `inicio_us` e `fim_us` sao lidos
  com ESP Timer. R inclui atraso da liberacao/agendamento e o trecho de trabalho.
  A ancoragem nominal ocorre antes de iniciar o timer, evitando referencia futura
  em relacao ao inicio real da tarefa.
- Sockets em modo nao bloqueante; por ciclo, no maximo 8 comandos e 16 logs,
  com verificacao de orcamento de 20 ms entre operacoes. Isso limita o trabalho
  da aplicacao, mas nao constitui prova de duracao maxima de chamadas do SDK.
- A inicializacao Wi-Fi/NVS ocorre antes dos jobs periodicos. Tarefas de Wi-Fi,
  pilha TCP/IP e event loop acrescentam interferencia e precisam entrar na analise.
- `METRIC` agora tem uma quinta tarefa: `MONITOR_TASK`. Ela tambem participa da
  ocupacao de CPU e dos misses soft da sintese. `MONITOR_METRIC` imprime um
  resumo proprio a cada 10 ciclos, inclusive quando o modo PC nao usa o roteiro.
- Liberacoes perdidas quando sua fila esta cheia sao contadas separadamente;
  elas nao devem ser escondidas ao avaliar a qualidade do monitoramento.

Um comando pode esperar quase 500 ms no socket antes do proximo ciclo. O
deadline de FS local e contado desde a ISR; para comandos PC, a referencia do
job FS e o instante em que a mensagem completa e interpretada pela MONITOR_TASK.
Portanto, cumprir FS em 10 ms desde essa referencia **nao comprova 10 ms entre
o envio pelo PC e a acao de seguranca**. O round trip do cliente inclui essa
espera, a rede e o ACK, alem de possiveis retransmissoes.

## Protocolo de aplicacao

Uma conexao/sessao de PC por vez, com uma requisicao pendente. Texto ASCII estrito:

```text
CMD;session;sequence;HELLO\n
CMD;session;sequence;PING\n
CMD;session;sequence;NAV\n
CMD;session;sequence;FS\n
```

`session` e `sequence` sao inteiros de 1 a 4294967295. Um HELLO registra uma
sessao; identificadores crescentes distinguem as requisicoes. A repeticao da
ultima requisicao da mesma sessao devolve seu resultado sem despachar novamente.
Identificador antigo recebe `STALE`; mesmo identificador com outro comando
recebe `SEQ_CONFLICT`. O cliente UDP tenta ate tres vezes com o mesmo pacote.
Ausencia de ACK nao prova que o comando nao foi executado.

No TCP, mensagens podem chegar fragmentadas ou agrupadas: o parser conserva
fragmentos ate `\n`. Linhas maiores que o limite ou contendo controles sao
descartadas inteiras ate o delimitador; o sufixo nao vira outro comando.
No UDP, cada datagrama transporta exatamente um comando. Os retornos sao JSON
delimitado por newline, um JSON por datagrama UDP e um fluxo de linhas no TCP.
Envios TCP parciais conservam o deslocamento para completar a mensagem depois.

Retornos principais:

| Tipo | Conteudo |
|---|---|
| `ack` | Sessao, sequencia, comando, resultado, instante de interpretacao e instante de montagem do ACK |
| `job` | Tarefa, origem, numero do job, evento/inicio/fim, miss e identificador do comando, quando existir |
| `status` | Protocolo, ciclo, contador monotono e contadores de perda/erro |

O campo `queued_us` e o instante de montagem do ACK, anterior ao envio efetivo.
Nao deve ser interpretado como timestamp de transmissao no radio.

## Logs e limites desta etapa

A fila de logs comporta 64 registros e a de transmissao, 32 mensagens. Produtores
nao esperam por rede. Filas cheias, desconexoes e linhas invalidas incrementam
contadores visiveis em `status`/`MONITOR_METRIC`. O historico em RAM e limitado;
antes de conectar o PC podem ocorrer perdas, sem impedir o controle do drone.

Por padrao, envia-se **1 de cada 100 jobs de FUS e CTRL**; NAV, FS e MONITOR nao
sao amostradas. O parametro e configuravel. Os acumuladores `METRIC` continuam
contando todos os jobs, independentemente da amostragem de envio. Coletas com
amostragem ou perdas nao servem para calcular uma distribuicao completa de todos
os jobs nem para (m,k)-firm sem uma metodologia adicional.

O cliente salva JSONL com timestamps locais de recebimento e registros
`pc_roundtrip` para os comandos. RTT e calculado no relogio monotono do PC;
nao se subtrai o relogio do PC do contador da ESP32. A etapa seguinte acrescentou
[TIME_TASK/SNTP](TIME_TASK.md): `time_synced=false` agora identifica ausencia da
primeira sincronizacao (ou TIME desabilitada). Veja o guia para timestamps UTC,
idade da atualizacao e o parametro `--show-time` do cliente.

No modo exclusivamente PC, nao ha validacao de sequencia de touch nem a sintese
de “14/14”. As metricas continuam continuamente. Nos modos touch/automatico,
comandos de rede nao entram no contador do roteiro; os jobs funcionais gerados
pelo PC sao identificados pela origem e tambem contam nas metricas das tarefas.

## Evidencias e verificacao

O registro de compilacao desta etapa esta em [VALIDACAO_MONITOR](VALIDACAO_MONITOR.md).

O log fornecido pelo usuario antes desta etapa foi preservado em
`resultados/base_automatica/CUSTOM_PRE_240MHz_sem_monitor.txt`: 14 eventos,
12 jobs NAV, 1 FS, sem descartes e sem misses reportados. Mantem-se a ressalva
da referencia temporal da FUS registrada na auditoria M1.

Os testes de `tests/test_monitor_pc.py` exercitam o cliente com pares locais
simulados, fragmentacao/coalescencia TCP, limite do buffer e repeticao UDP
conservando identificadores. Eles nao executam o firmware ou o radio da placa.

```text
python -m unittest discover -s tests -v
```

Referencias do SDK consultadas: [sockets/lwIP](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/lwip.html)
e exemplo de rede do professor. As referencias importadas permanecem preservadas.

# Ensaios M2 — começar aqui

Esta pasta reúne planejamento, coletas e anexos do chat para o relatório.
**Há uma coleta completa M_UDP/R01_T02, ainda marcada REVISAR por atraso na agenda.**
A tentativa R01 interrompida está preservada. Faltam 19 coletas completas no plano;
a aceitação final e eventual repetição de R01 dependem da revisão da comparabilidade.
Os testes antigos
estão separados em `03_anexos_chat/H...` e não devem ser misturados à bateria.

Campanha atual: **20 ensaios automáticos**, cinco por cenário, com TIME ligada.
A primeira repetição R01 serve também para conferir a coleta. Por escolha do
usuário, não haverá acionamento físico de touch nesta campanha. Os eventos A/B/C/D
serão produzidos por GPTimer; isso não demonstra o requisito de touch físico do PDF.

## Primeiro teste: M_UDP / R01

Use o terminal **ESP-IDF** na raiz do projeto. Encerre o monitor serial antigo
com **Ctrl + ]** e o cliente Python anterior com `SAIR`. Só um programa pode
usar COM7 por vez.

```powershell
python tools/configurar_teste.py --cenario M_UDP
idf.py -p COM7 flash
python pc/executar_ensaio.py --cenario M_UDP --repeticao R01 --host 192.168.15.23 --serial COM7
```

Ajuste COM7 e o IP se mudaram. O script preserva SSID/senha; a placa já precisa
estar configurada para a rede. Para descobrir o IP, use `idf.py -p COM7 monitor`,
anote `MONITOR_IP`, saia com Ctrl+] e só então abra o coletor.

O coletor abre o serial, reinicia a ESP32 pelo circuito USB, aguarda rede/SNTP,
confere o cenário e envia BEGIN. Depois envia os comandos automaticamente por
180 s, aguarda a conclusão dos jobs e solicita as estatísticas. **Não digite
NAV/FS e não toque nos pads durante os ensaios AUTO.**

Se o reset USB não funcionar na sua placa, use `--sem-reset` e pressione EN/RESET
quando solicitado. Se a pasta R01 já existir, preserve-a e envie o resultado para
conferência antes de repetir; nunca apague um ensaio com falha para esconder o resultado.
Para repetir após uma interrupção, acrescente `--tentativa 2` ao comando do
coletor: a nova pasta será R01_T02. O número da repetição continua R01 e a
tentativa anterior fica preservada. Só uma tentativa aprovada conta como R01.
Mantenha o cabo conectado até aparecer **Arquivos salvos em:** e o terminal
voltar ao PowerShell. São 180 s de medição, mais boot/SNTP e coleta final;
normalmente o processo leva de 3 a 4 minutos, podendo demorar mais na conexão.

Ao terminar, envie pelo chat o identificador **M_UDP_R01**, `RESUMO.md` e,
preferencialmente, os arquivos `manifesto.json`, `estatisticas.json`, `serial.txt`
e `rede.jsonl` (ou um ZIP da pasta). Vou conferir e organizar as evidências.
Se enviar apenas um trecho colado, ele será guardado como anexo parcial.

## Quais variáveis alternar

| Código | Referência M1 | Política | Preempção | CPU | Slicing | Protocolo |
|---|---|---|---|---|---|---|
| M_UDP | Melhor, cenário 02 | CUSTOM | ON | 240 MHz | ON | UDP |
| M_TCP | Melhor, cenário 02 | CUSTOM | ON | 240 MHz | ON | TCP |
| P_UDP | Pior, cenário 07 | DM | OFF/cooperativo | 80 MHz | ON | UDP |
| P_TCP | Pior, cenário 07 | DM | OFF/cooperativo | 80 MHz | ON | TCP |

Você só troca `--cenario`: o configurador altera o conjunto correto. **Após
trocar cenário, grave novamente o firmware.** Mantenha UNICORE, tick de 1 ms,
MONITOR/TIME ON, período SNTP 60 s, porta 5000, amostragem de rede 100, roteiro,
rede, posição da placa/notebook e duração constantes. Slicing ON no modo
cooperativo é o valor declarado na M1; não significa preempção por tick.

Entre R01 e R02 do mesmo cenário não precisa recompilar/gravar: execute o
coletor com o novo número. Ele reinicia a placa e cria outra pasta.

```powershell
python pc/executar_ensaio.py --cenario M_UDP --repeticao R01 --host 192.168.15.23 --serial COM7
```

## Lista completa e ordem

1. **Primeira rodada:** M_UDP_R01, M_TCP_R01, P_UDP_R01 e P_TCP_R01. Conferir
   a coleta antes de multiplicar as repetições. Se corretas, já contam na bateria.
2. **Demais rodadas:** R02, R03, R04 e R05 para os mesmos quatro cenários.
   São **20 ensaios de 180 s**, incluindo R01. A matriz alterna UDP/TCP entre
   repetições. Total de medição: 60 minutos, mais aquecimento, gravação e coleta final.

A lista individual está em [01_planejamento/MATRIZ.csv](01_planejamento/MATRIZ.csv).
Os cinco ensaios e 180 s são escolhas experimentais nossas, não quantidades
impostas pelo professor. Não afirmar WCET garantido com base nessas amostras.

As opções de desenvolvimento `--touch` e `--sem-time` continuam disponíveis no
código, mas não fazem parte desta campanha nem da matriz atual.

## O que fica salvo por ensaio

```text
ensaios_M2/
  01_planejamento/MATRIZ.csv
  02_coletas/M_UDP/R01/
    manifesto.json       configuração, versão, hashes, relógios, estado da coleta
    serial.txt           bytes originais do serial
    rede.jsonl           mensagens e timestamps do notebook
    roteiro.json         agenda prevista
    comandos.jsonl       agenda executada, atrasos, ACKs e falhas
    estatisticas.json    acumuladores completos por tarefa, recebidos da ESP32
    tarefas.csv          métricas para tabelas
    resumo.json          conferências e RTT
    RESUMO.md            leitura humana
  03_anexos_chat/         arquivos enviados aqui, com origem e SHA256
  04_relatorio/           guia para o relatório futuro
```

`COLETADO` significa que o processo terminou, não que os deadlines foram
atendidos. `REVISAR` no resumo aponta problemas da coleta/agenda a investigar.
Misses e eventos perdidos são resultados a reportar, não motivos para apagar logs.

## Como interpretar as medições

- A janela começa no BEGIN e dura 180 s no relógio monotônico da ESP32. Jobs são
  incluídos pelo instante de evento; startup e aquecimento ficam no serial, fora
  dos acumuladores da janela. BEGIN só é aceito uma vez por boot.
- FUS agora usa liberações de esp_timer a 5 ms, com fila e perdas explícitas.
  A antiga referência de tick que podia ficar posterior ao início foi removida.
  Essa mudança adiciona uma fonte de interferência e deve constar no relatório;
  os valores históricos da M1 não são diretamente reprocessáveis como M2.
- Estatísticas locais consideram todos os jobs válidos: WCRT/HWM100 exato
  observado, HWM99 como **intervalo de histograma de 100 us**, (9,10)-firm nas
  tarefas soft. Respostas acima de 102,4 ms vão para overflow: o intervalo do
  percentil pode ficar mais largo, até o máximo observado, sem fingir precisão.
- O histograma representa jobs executados; ativações perdidas aparecem à parte.
  Com perdas, (m,k) sobre todas as ativações é inconclusivo. Não preencher lacunas
  de logs individuais como se fossem sucessos.
- O código ainda envia amostras individuais de FUS/CTRL para limitar tráfego,
  mas a amostragem não afeta os acumuladores locais. A instrumentação custa CPU,
  memória e curtas seções críticas; mantenha a mesma em todos os cenários.
- `exec_*` mede tempo decorrido do corpo instrumentado, podendo incluir
  preempções/esperas; `cpu_*` mede CPU nas janelas completas do serial. Não chamar
  a maior duração observada de WCET formal. Printfs fora dos trechos de algumas
  tarefas continuam contribuindo para interferência/CPU.
- FS registra instante da ação segura e fim da tarefa separadamente. D automático
  acontece aos 24 s do GPTimer; os FS remotos posteriores reafirmam o estado
  seguro, não simulam pousos novos. A fase após D permanece em fail-safe.
- `isr_latency_max_ms` separa ativações por ISR das remotas. AUTO mede GPTimer;
  TOUCH mede a partir da callback do sensor, não desde o primeiro contato do dedo.
  Jitter das periódicas é atraso desde a liberação nominal; nas tarefas por
  evento é variação da latência entre jobs consecutivos da janela.
- O roteiro agora contabiliza também o pequeno job NAV que trata A/STRESS:
  são 13 jobs NAV do roteiro (5 B + 7 C + 1 A), além dos comandos aceitos do PC.
  O histórico M1 tinha 12 porque não instrumentava o tratamento de A.
- Agenda PC: NAV em 1,3,...21 s; PING a cada 2 s; NAV em 27 s deve receber
  SAFE_ACTIVE; FS em 30,75 s, 40,75 s,...170,75 s. O zero do PC é a chamada de
  envio de BEGIN, e o zero do GPTimer é a interpretação na ESP: essa diferença
  de fase é registrada, não presumida nula.
- RTT usa somente o relógio monotônico do PC e inclui monitor de 500 ms e retries.
  A falta de ACK não comprova que o comando não executou. Logs guardam cada tentativa.
- PC consulta SNTP no começo/fim, registra offset estimado sem alterar Windows
  e aplica a referência às datas registradas. `tx_mono_us`/`tx_unix_ms` marcam o
  preparo imediato da chamada send da ESP; `socket_rx_us` marca retorno recv.
  No TCP, recepção de uma mensagem é o recebimento do seu último fragmento.
  Não são timestamps de rádio. Datagramas/pacotes podem ser perdidos; tamanhos
  são bytes de payload da aplicação, sem cabeçalhos Wi-Fi/IP/TCP/UDP.
- Sincronização não garante precisão de 1 ms nem relógios idênticos. Para delays
  unidirecionais, avaliar offset, deriva e assimetria; usar RTT como comparação
  principal. Consulta PC baseada na [RFC 5905](https://www.rfc-editor.org/rfc/rfc5905.html).

As próximas análises devem justificar hard/soft, interferência, seções críticas
e bloqueio. Filas não são prova automática de inversão de prioridade. O critério
do enunciado pede todos os deadlines atendidos em vários testes: perdas hard
não podem receber a conclusão “OK hard”. O LED de ponto extra permanece opcional.

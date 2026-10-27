# Apresentação presencial sem depender do chat

Use o terminal ESP-IDF na raiz do projeto. Substitua COM7 e o IP pelos da sua placa. Prepare a rede Wi-Fi 2,4 GHz com acesso SNTP previamente.

## Alterar parâmetros livremente

Execute `idf.py menuconfig`. Em **M2 - Drone e experimentos**:

| Parâmetro | Escolha |
|---|---|
| Prioridades | 1=CUSTOM, 2=DM, 3=RM |
| Preempção | Escalonamento preemptivo: ligado/desligado |
| Time slicing | Ligado/desligado |
| Protocolo | UDP/TCP |
| Origem dos eventos | Automático, touch ou somente comandos do PC |
| Wi-Fi | SSID e senha da sua rede |
| TIME/SNTP | Habilitação, servidor e intervalo de atualização |

Para **80, 160 ou 240 MHz**, use a busca `/` do menuconfig por `ESP_DEFAULT_CPU_FREQ_MHZ` e abra a escolha correspondente. Mantenha UNICORE. Salve e saia.

```powershell
idf.py -p COM7 flash monitor
```

O firmware recompila e é gravado; confira os parâmetros no banner serial. Essas mudanças não são instantâneas em execução. No modo cooperativo continuam existindo yields explícitos. Não execute o configurador de cenário após uma escolha manual: ele substituiria os parâmetros pelos do cenário.

## Demonstrar uma combinação livre

Deixe o monitor serial aberto e use outro terminal ESP-IDF para a rede:

```powershell
python pc/monitor_pc.py --host 192.168.15.23 --protocol udp --output demonstracao_udp.jsonl --show-time
```

Substitua o IP pelo MONITOR_IP e use `tcp` se esse for o protocolo gravado. HELLO é enviado automaticamente. Digite os comandos no prompt:

- `PING`: verifica resposta da placa.
- `BEGIN`: inicia a janela e, no modo AUTO com espera pelo PC, o roteiro. Aguarde a primeira sincronização SNTP; WAIT_SYNC indica que é necessário tentar novamente após a sincronização.
- `NAV`: solicita navegação. Após o fail-safe, recebe SAFE_ACTIVE.
- `FS`: ativa estado seguro até reiniciar. O LED do ponto extra ainda não é acionado.
- `REPORT`: solicita estatísticas; para resultados finais, aguarde a duração configurada (180 s por padrão), mais cerca de 7 s.
- `SAIR`: fecha o cliente.

No modo AUTO, D ativa fail-safe aos 24 s. Para controlar apenas pelo PC, selecione essa origem no menuconfig; BEGIN mede, mas não produz o roteiro automático. Reinicie a placa para outra janela ou para liberar o fail-safe.

**O cliente interativo é para demonstração:** salva mensagens JSONL, mas não produz todos os arquivos do coletor automático nem consulta SNTP do PC como ele. Não substitui a bateria completa.

## Reproduzir um cenário da bateria

```powershell
python tools/configurar_teste.py --cenario M_TCP
idf.py -p COM7 flash
python pc/executar_ensaio.py --cenario M_TCP --repeticao R01 --host 192.168.15.23 --serial COM7
```

Escolhas: M_UDP, M_TCP, P_UDP e P_TCP. Use repetição/tentativa livre. Feche o monitor serial com Ctrl+] antes do coletor, que precisa da mesma porta. Aguarde “Arquivos salvos em:” e retorno ao prompt antes de desconectar.

O coletor valida os quatro cenários fixos e rejeita diferenças. RM/160 MHz, por exemplo, é demonstração fora da matriz: use o cliente interativo, sem rotular como um cenário padrão.

## Problemas comuns

- idf.py não encontrado: abra/ative o terminal ESP-IDF.
- Sem ACK: confira IP da ESP32, protocolo, rede e porta 5000.
- Serial ocupada: feche outro monitor/coletor.
- ALREADY_STARTED: reinicie a placa.
- Sem SNTP: confira internet, DNS e UDP 123; não tratar UTC inválida como sincronizada.
- Alteração não aparece: salve menuconfig e grave novamente. sdkconfig.defaults não sobrescreve escolhas existentes em sdkconfig.

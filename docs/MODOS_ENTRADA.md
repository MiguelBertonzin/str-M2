# Seleção de entradas: automatizado ou físico

O projeto usa o mesmo firmware e as mesmas tarefas para duas fontes de
eventos. O modo padrão é **automatizado**, utilizado na campanha analisada.
O modo **físico** permite demonstrar os sensores touch durante a apresentação.
Não é necessário substituir arquivos C.

| Opção | Fonte A/B/C/D | Repetição no coletor |
|---|---|---|
| `--automatizado` ou opção omitida | ISR do GPTimer | R01 a R05; Pxx para demonstração/piloto adicional |
| `--fisico` | Sensores touch da ESP32 | T01, T02 etc. |

`--touch` continua disponível como alias de `--fisico`. As duas fontes são
mutuamente exclusivas. A opção do configurador escolhe o que será compilado;
a opção do coletor confere a fonte gravada na placa. **Trocar a fonte exige
configurar e gravar novamente.** O argumento Python sozinho não altera um
firmware já em execução.

## Demonstração automatizada

No terminal ESP-IDF, na raiz do projeto:

```powershell
python tools/configurar_teste.py --cenario M_UDP --automatizado
idf.py -p COM7 flash
python pc/executar_ensaio.py --cenario M_UDP --repeticao P01 --automatizado --host auto --serial COM7
```

P01 identifica uma demonstração adicional e preserva R01 a R05 da campanha.
Se P01 já existir, usar P02 ou `--tentativa 2`. Os comandos antigos sem
`--automatizado` continuam equivalentes: AUTO é o padrão.

Após `MEDICAO INICIADA`, o roteiro ocorre por timer durante uma janela de
60 segundos. O PC envia PING, NAV e FS conforme a agenda. Aguardar
`Arquivos salvos em:` e o retorno ao terminal antes de desconectar.

## Demonstração com touch físico

```powershell
python tools/configurar_teste.py --cenario M_UDP --fisico
idf.py -p COM7 flash
python pc/executar_ensaio.py --cenario M_UDP --repeticao T01 --fisico --host auto --serial COM7
```

Os pinos utilizados pelo firmware são:

| Entrada | GPIO | Ação |
|---|---:|---|
| A | 4 | Perturbação e carga de estresse |
| B | 27 | Navegação/rota |
| C | 13 | Telemetria |
| D | 32 | Fail-safe |

Deixar os contatos livres durante a calibração e **iniciar os toques somente
depois de `MEDICAO INICIADA`**. O primeiro passo é C (GPIO13), mesmo que a
primeira instrução do guia tenha aparecido durante o boot. Depois, seguir
as linhas `GUIA` no próprio terminal:

```text
C → B → C → A → B → C → B → C → B → C → B+C → D → C
```

B+C representa dois eventos, totalizando 14. Os contatos devem ser soltos
entre ativações; o firmware aplica debounce de 1,2 s por canal e exige
liberação estável de 500 ms. Concluir o roteiro dentro da janela de 60 s.
Um toque fora da ordem deixa a demonstração marcada para revisão; D mantém
a ação de segurança. Reiniciar a placa permite outra execução.

No coletor físico, o PC envia apenas PING. NAV e FS vêm dos contatos, de modo
que os eventos manuais não se misturem com comandos automáticos. Os arquivos
ficam em `ensaios_M2/02_coletas/AGENDA_60S_R2/CENARIO/Txx/`, separados das
repetições Rxx. A validação verifica a fonte reportada pela placa e rejeita
AUTO quando `--fisico` foi solicitado, ou TOUCH no modo automatizado.

Esta demonstração não é temporalmente equivalente à campanha automática:
os instantes dos contatos dependem do operador. A latência registrada parte
do callback da ISR, não do instante inicial do contato do dedo. A campanha
publicada continua baseada nas vinte repetições automáticas; uma coleta
física só pode ser descrita como realizada depois de executar e revisar Txx.

## Retorno ao fluxo padrão

```powershell
python tools/configurar_teste.py --cenario M_UDP --automatizado
idf.py -p COM7 flash
python pc/executar_ensaio.py --cenario M_UDP --repeticao P02 --automatizado --host auto --serial COM7
```

M_TCP, P_UDP e P_TCP aceitam as mesmas opções. Ao trocar cenário ou fonte,
configurar e gravar; ao repetir com a mesma configuração, executar apenas o
coletor. O coletor reinicia a placa e usa a serial exclusivamente: fechar
`idf.py monitor` com Ctrl+] antes de iniciá-lo.

Para uma demonstração livre sem roteiro, os comandos interativos de rede
estão em [APRESENTACAO.md](APRESENTACAO.md). O cliente interativo exige o IP
reportado pela placa e não substitui a coleta completa com estatísticas.

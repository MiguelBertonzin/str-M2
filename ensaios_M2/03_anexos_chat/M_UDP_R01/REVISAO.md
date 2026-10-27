# M_UDP_R01 — primeira tentativa interrompida

O usuário informou a retirada do cabo USB antes da conclusão. O terminal
enviado mostra comandos até aproximadamente 144 s da agenda; o serial bruto
chega a aproximadamente 151 s desde o boot. A janela prevista era de 180 s
após BEGIN, além do tempo necessário para receber as estatísticas finais.

O manifesto encerrou com status FALHOU e erro:
`Sem ACK para REPORT; resultado da execucao e incerto`.
Não foram produzidos estatisticas.json e RESUMO.md. Portanto, essa tentativa
não conta como uma repetição completa. A gravação do firmware, conexão Wi-Fi,
comandos e três atualizações SNTP foram observados; isso não substitui a coleta final.

O anexo original está preservado como terminal_interrupcao_usb.txt, com origem
e SHA256 em origens.json. Os arquivos brutos permanecem em
`ensaios_M2/02_coletas/M_UDP/R01/`. Repetir como R01, tentativa 2 (R01_T02),
mantendo o cabo até a mensagem final do coletor e retorno ao PowerShell.

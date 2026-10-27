# M_UDP_R01 — tentativa 2: coleta completa com ressalva de agenda

A coleta terminou normalmente, gerou estatísticas das seis tarefas, CSV e resumo.
O anexo termina com “Arquivos salvos em:” e retorno ao PowerShell. Esta é a
primeira coleta completa de M_UDP/R01; a primeira tentativa foi interrompida.

Acumuladores da janela de 180 s: FUS 36000, CTRL 36000, NAV 24, FS 16,
MONITOR 360, TIME 180. Zero misses, ativações perdidas ou timestamps inválidos.
Três novas atualizações SNTP. RTT de 116 comandos: média 700,681 ms, máximo
2484 ms. Esses tempos incluem espera no monitor e retransmissões.

Ressalva mantida: o analisador marcou REVISAR porque 34 comandos foram enviados
mais de 500 ms depois da agenda prevista. O cliente espera o ACK antes de enviar
o próximo comando; RTT maior que o espaçamento previsto causa atraso acumulado.
Isso não é um miss das seis tarefas e não indica interrupção da coleta. Não
afirmar que a fase de chegada foi idêntica entre cenários sem examinar os logs.

Contabilizar como primeira coleta completa, com a ressalva preservada para
revisão da comparabilidade. A contagem final de ensaios aceitos deve distinguir
coleta concluída de atendimento de prazos e de conformidade com a agenda.

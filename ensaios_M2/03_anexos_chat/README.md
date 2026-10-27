# Anexos recebidos no chat

Arquivos originais preservados com nomes descritivos e `origens.json` (origem,
quantidade de bytes e SHA256). Hxx são testes de desenvolvimento anteriores à
instrumentação M2-METRICS-2; não são repetições da bateria principal.

Ao receber novos resultados, usar o ID informado pelo usuário, por exemplo
`M_UDP_R01`. Sem ID/configuração suficientes, arquivar como provisório e não
atribuir silenciosamente um cenário. Nunca sobrescrever um anexo diferente.

Comando de arquivamento, para uso durante a revisão dos anexos:

```text
python tools/arquivar_anexo.py --origem "CAMINHO_DO_ANEXO" --id M_UDP_R01 --nome serial_enviado.txt
```

Logs colados podem ter linhas quebradas pelo terminal; preservar os originais.
Os arquivos brutos do coletor são preferíveis para análise automatizada.

# Publicacao e rastreabilidade

O repositorio publica firmware, ferramentas, enunciado, documentacao atual,
20 coletas da campanha final, fontes da campanha e analises reproduziveis.
Compilacoes, caches, tentativas interrompidas e documentos de desenvolvimento
nao integram a arvore principal publicada.

As duas primeiras R01 substituidas sao evidencias complementares pertinentes
a selecao da amostra. A primeira P_TCP ja era valida; sua substituicao deve
ser explicitada, sem selecionar resultados pela menor quantidade de falhas.

As credenciais locais nao sao publicadas. Quando um log contem SSID, a copia
publica usa placeholder. PUBLICACAO_MANIFESTO.json registra SHA256 original,
SHA256 publicado e a indicacao de sanitizacao. Os originais locais permanecem
preservados. A sanitizacao nao altera tempos, contadores ou metricas.

A analise e regenerada sobre as copias publicadas para que o inventario
integridade_dados.csv corresponda aos arquivos disponiveis no repositorio.
O snapshot fontes_campanha corresponde aos hashes registrados nos manifestos.
Os scripts atuais acrescentam aliases de entrada para apresentacao; o codigo
C do firmware medido nao foi alterado por essa parametrizacao.

A ferramenta preparar_publicacao_final.py e de manutencao do checkout local
.publication; nao e necessaria para compilar, coletar ou reproduzir analises.

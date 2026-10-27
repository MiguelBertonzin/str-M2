# Publicação para reprodução

Snapshot de 27/10/2026. Inclui fontes, configurações de exemplo sem credenciais, enunciado M2, referências M1/professor, testes e evidências disponíveis. Não inclui builds, ambientes Python, configurações pessoais do editor nem sdkconfig local.

O firmware não foi alterado nesta preparação. A agenda com espera por ACK permanece como na coleta publicada: a ressalva dos 34 comandos atrasados não foi corrigida silenciosamente.

Credenciais Wi-Fi conhecidas são substituídas por placeholders nas cópias publicadas de textos/logs. Originais locais são preservados. O manifesto `PUBLICACAO_MANIFESTO.json` na raiz publicada identifica arquivos modificados na sanitização e SHA-256 das cópias publicadas. Hashes anteriores nos manifests de coleta/importação continuam sendo os dos originais; uma cópia sanitizada pode legitimamente diferir deles. Não modificar retroativamente os hashes históricos para aparentar identidade.

Arquivos de evidência mantêm métricas e a indicação de falhas/ressalvas. Endereços IP locais, características da placa/rede e caminhos históricos podem aparecer como contexto de coleta; não são instruções para reutilizar a configuração de outra máquina.

Cada colega deve configurar Wi-Fi, porta serial e IP próprios. Recomenda-se manter a mesma versão de firmware/ESP-IDF e condições equivalentes entre os quatro cenários, registrando qualquer diferença. Resultados de redes/placas diferentes devem ser identificados separadamente.

Publicar resultados de desenvolvimento não equivale a declarar aprovação do trabalho. A coleta completa atual continua REVISAR; faltam os outros cenários, consolidação, análise de interferência/bloqueio, relatório e resolução da diferença entre GPTimer e touch físico exigido. O LED extra não foi implementado.

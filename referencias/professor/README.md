# Exemplos de rede do professor — ESP32 e PC

Cópia dos cinco arquivos disponibilizados pelo Prof. Felipe Viel em
[Real_Time_Systems / Codes / Network_ESP32](https://github.com/VielF/Real_Time_Systems/tree/main/Codes/Network_ESP32).

Versão importada: commit
[`03eb2eef47a77e53c5fbfadebf6f5a32d3bbcaf7`](https://github.com/VielF/Real_Time_Systems/tree/03eb2eef47a77e53c5fbfadebf6f5a32d3bbcaf7/Codes/Network_ESP32).
Os arquivos originais estão em `Network_ESP32/`, preservados sem alterações.
O arquivo `IMPORTACAO_NETWORK_ESP32.json` registra origem, commit e hashes.

| Arquivo | Conteúdo para consulta |
|---|---|
| [tcp_udp_esp32_wifi_con.c](Network_ESP32/tcp_udp_esp32_wifi_con.c) | Wi-Fi, envio UDP da ESP32 ao PC e servidor TCP na ESP32. No `app_main` original, apenas a tarefa UDP está habilitada; a criação da tarefa TCP está comentada. |
| [tcp_client.py](Network_ESP32/tcp_client.py) | Cliente TCP no PC, conectando ao servidor da ESP32 na porta 5000. |
| [udp_server.py](Network_ESP32/udp_server.py) | Receptor UDP no PC, escutando a porta 6010. |
| [esp32_sntp_con.c](Network_ESP32/esp32_sntp_con.c) | Conexão Wi-Fi e exemplo de sincronização de data/hora via SNTP. |
| [README.md original](Network_ESP32/README.md) | Orientação do professor sobre os exemplos e a rede Wi-Fi. |

## Uso durante o desenvolvimento

Esses arquivos são referências independentes, ainda fora da compilação do drone.
Cada exemplo C contém seu próprio `app_main`; a integração será feita por etapas,
aproveitando as funções pertinentes no projeto M2. A descrição acima considera
o comportamento do código, pois alguns comentários dos originais trocam os papéis
de cliente/servidor.

Os exemplos usam nomes de Wi-Fi, senhas e endereços IP que precisarão ser ajustados
ao ambiente de teste quando forem executados. Os scripts Python do PC são os
originais fornecidos pelo professor.

Nenhum exemplo foi executado, compilado ou integrado ao firmware nesta importação.
Os bytes baixados foram conferidos com os hashes Git de origem e os hashes SHA-256
das cópias locais estão registrados no manifesto.

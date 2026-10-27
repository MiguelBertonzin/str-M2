"""Teste do cliente PC e do enquadramento, com pares de rede simulados locais.

Nao executa o firmware ESP32 nem comprova prazo de Wi-Fi. Rodar com:
python -m unittest discover -s tests -v
"""

import importlib.util
import json
from pathlib import Path
import socket
import threading
import unittest

spec = importlib.util.spec_from_file_location("monitor_pc", Path(__file__).resolve().parents[1] / "pc/monitor_pc.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MonitorTests(unittest.TestCase):
    def test_report_udp_reune_seis_tarefas_apos_ack(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(('127.0.0.1', 0))
        server.settimeout(2)
        failures = []
        def peer():
            try:
                data, address = server.recvfrom(1024)
                _, session, seq, command = data.decode().strip().split(';')
                self.assertEqual(command, 'REPORT')
                ack = dict(type='ack', session=int(session), seq=int(seq), command=command, status='OK')
                server.sendto(json.dumps(ack).encode(), address)
                for name in ('FUS_IMU', 'CTRL_ATT', 'NAV_PLAN', 'FAIL_SAFE', 'MONITOR_TASK', 'TIME_TASK'):
                    server.sendto(json.dumps(dict(type='analysis', report_seq=int(seq), task=name, jobs=17)).encode(), address)
            except Exception as error: failures.append(error)
        thread = threading.Thread(target=peer)
        thread.start()
        client = module.MonitorClient('127.0.0.1', server.getsockname()[1], 'udp', output=None)
        try:
            result = client.report(timeout=2)
            self.assertEqual(len(result), 6)
            self.assertTrue(all(r['jobs'] == 17 for r in result))
        finally:
            client.close(); thread.join(3); server.close()
        self.assertFalse(failures, failures)

    def test_hora_antes_da_primeira_sincronizacao(self):
        message = dict(cycle=3, time_synced=False, unix_ms=None, sync_count=0,
                       cycles_since_sync=-1, sync_age_ms=-1, release_dropped=0)
        self.assertIn("AGUARDANDO_SNTP", module.format_time_status(message))
        self.assertIn("ciclos_desde_sync=-1", module.format_time_status(message))

    def test_hora_utc_com_milissegundos_e_idade_preservada(self):
        message = dict(cycle=99, time_synced=True, unix_ms=1767225600123, sync_count=2,
                       cycles_since_sync=70, sync_age_ms=70123, release_dropped=1)
        text = module.format_time_status(message)
        self.assertIn("2026-01-01T00:00:00.123+00:00", text)
        self.assertIn("idade_sync_ms=70123", text)
        self.assertIn("release_dropped=1", text)

    def test_tcp_json_fragmentado_e_multiplas_linhas(self):
        parser = module.JsonLines()
        self.assertEqual(parser.feed(b'{"type":"jo'), [])
        self.assertEqual(parser.feed(b'b"}\n{"type":"ack"}\n{"t'),
                         [{"type": "job"}, {"type": "ack"}])
        self.assertEqual(parser.feed(b'ype":"status"}\n'), [{"type": "status"}])

    def test_limite_de_tcp_sem_newline(self):
        with self.assertRaises(ValueError):
            module.JsonLines().feed(b"x" * 65537)

    def test_limite_e_por_linha_nao_por_lote_tcp(self):
        messages = module.JsonLines().feed(b'{"type":"status"}\n' * 5000)
        self.assertEqual(len(messages), 5000)

    def test_udp_retransmissao_preserva_identificador(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server.bind(("127.0.0.1", 0))
        server.settimeout(2)
        received = []
        failures = []

        def peer():
            try:
                # Primeiro datagrama simula um ACK perdido. O cliente repete
                # a mesma operacao, para permitir deduplicacao na ESP32.
                for attempt in range(2):
                    data, address = server.recvfrom(1024)
                    received.append(data)
                    if attempt == 1:
                        _, session, seq, command = data.decode().strip().split(";")
                        ack = {"type": "ack", "session": int(session), "seq": int(seq),
                               "command": command, "status": "QUEUED"}
                        server.sendto(json.dumps(ack).encode() + b"\n", address)
            except Exception as error:
                failures.append(error)

        thread = threading.Thread(target=peer)
        thread.start()
        client = module.MonitorClient("127.0.0.1", server.getsockname()[1], "udp", output=None)
        try:
            result = client.command("NAV", timeout=0.1)
            self.assertEqual(result["attempts"], 2)
            self.assertEqual(result["ack"]["status"], "QUEUED")
            self.assertEqual(received[0], received[1])
        finally:
            client.close()
            thread.join(3)
            server.close()
        self.assertFalse(failures, failures)

    def test_tcp_ack_fragmentado_junto_de_status(self):
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.bind(("127.0.0.1", 0))
        server.listen(1)
        server.settimeout(2)
        failures = []

        def peer():
            try:
                connection, _ = server.accept()
                with connection:
                    connection.settimeout(2)
                    request = bytearray()
                    while b"\n" not in request:
                        request.extend(connection.recv(1024))
                    _, session, seq, command = request.decode().strip().split(";")
                    ack = {"type": "ack", "session": int(session), "seq": int(seq),
                           "command": command, "status": "OK"}
                    data = json.dumps(ack).encode() + b'\n{"type":"status"}\n'
                    connection.sendall(data[:5])
                    connection.sendall(data[5:])
                    # Mantem o socket aberto ate o cliente terminar.
                    while connection.recv(1024):
                        pass
            except Exception as error:
                failures.append(error)

        thread = threading.Thread(target=peer)
        thread.start()
        client = module.MonitorClient("127.0.0.1", server.getsockname()[1], "tcp", output=None)
        try:
            result = client.command("HELLO", timeout=1)
            self.assertEqual(result["ack"]["command"], "HELLO")
            self.assertEqual(result["attempts"], 1)
            self.assertGreaterEqual(result["round_trip_ms"], 0)
        finally:
            client.close()
            thread.join(3)
            server.close()
        self.assertFalse(failures, failures)


if __name__ == "__main__":
    unittest.main()

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
import time
import tempfile

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pc'))
from agenda import send_schedule, collect_responses

spec = importlib.util.spec_from_file_location("monitor_pc", Path(__file__).resolve().parents[1] / "pc/monitor_pc.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MonitorTests(unittest.TestCase):
    def test_report_espera_tarefas_lentas_sem_misturar_outro_report(self):
        server=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        server.bind(('127.0.0.1',0)); server.settimeout(2)
        failures=[]; progress=[]
        tasks=('FUS_IMU','CTRL_ATT','NAV_PLAN','FAIL_SAFE','MONITOR_TASK','TIME_TASK')
        def peer():
            try:
                data,address=server.recvfrom(1024)
                _,session,seq,command=data.decode().strip().split(';')
                seq=int(seq)
                def send(message): server.sendto(json.dumps(message).encode(),address)
                send(dict(type='ack',session=int(session),seq=seq,command=command,status='OK'))
                # Seis respostas de outra solicitacao nao podem completar esta.
                for task in tasks: send(dict(type='analysis',report_seq=seq+99,task=task))
                for task in tasks:
                    time.sleep(.03)
                    send(dict(type='analysis',report_seq=seq,task=task))
            except Exception as error: failures.append(error)
        thread=threading.Thread(target=peer); thread.start()
        client=module.MonitorClient('127.0.0.1',server.getsockname()[1],'udp',output=None)
        try:
            result=client.report(timeout=1,on_progress=lambda seq,received,expected: progress.append(len(received)))
            self.assertEqual([a['task'] for a in result],list(tasks))
            self.assertEqual(len({a['report_seq'] for a in result}),1)
            self.assertTrue(any(0<n<6 for n in progress))
            self.assertEqual(progress[-1],6)
        finally:
            client.close(); thread.join(3); server.close()
        self.assertFalse(failures,failures)

    def test_agenda_udp_tcp_envia_todos_antes_de_ack_e_correlaciona_fora_de_ordem(self):
        for protocol in ('udp', 'tcp'):
            with self.subTest(protocol=protocol):
                server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM if protocol=='udp' else socket.SOCK_STREAM)
                server.bind(('127.0.0.1', 0)); server.settimeout(3)
                if protocol=='tcp': server.listen(1)
                failures=[]; received=[]
                def peer():
                    connection=None
                    try:
                        if protocol=='tcp':
                            connection,_=server.accept(); connection.settimeout(3)
                        pending=b''
                        while len(received)<3:
                            if protocol=='udp': data,address=server.recvfrom(1024)
                            else: data=connection.recv(1024)
                            if not data: raise ConnectionError('Cliente encerrou antes dos tres envios')
                            pending += data
                            while b'\n' in pending:
                                line,pending=pending.split(b'\n',1)
                                _,session,seq,command=line.decode().split(';')
                                received.append((int(session),int(seq),command))
                        # ACK somente depois dos tres envios, invertido e duplicado.
                        # NAV fica sem ACK, mas nao impede o envio posterior de FS.
                        for index in (2,0,0):
                            session,seq,command=received[index]
                            payload=json.dumps(dict(type='ack',session=session,seq=seq,command=command,status='OK')).encode()+b'\n'
                            if protocol=='udp': server.sendto(payload,address)
                            else:
                                connection.sendall(payload[:7]); connection.sendall(payload[7:])
                        if connection:
                            while connection.recv(1024): pass
                    except Exception as error: failures.append(error)
                    finally:
                        if connection: connection.close()
                thread=threading.Thread(target=peer); thread.start()
                client=module.MonitorClient('127.0.0.1',server.getsockname()[1],protocol,output=None,show_jobs=False)
                try:
                    with tempfile.TemporaryDirectory() as directory:
                        actions=send_schedule(client,[(.02,'PING'),(.04,'NAV'),(.06,'FS')],
                                              time.perf_counter(),Path(directory),duration=1)
                        self.assertTrue(all(a['actual_s'] >= a['planned_s'] for a in actions))
                        client.wait_for(lambda c: len(c.acks)>=2,timeout=2)
                        first=client.finish_command(actions[0]['request'], timeout=.001)
                        self.assertTrue(first['ack_late'])
                        last=client.finish_command(actions[2]['request'],timeout=1)
                        self.assertEqual(first['ack']['seq'],actions[0]['request']['seq'])
                        self.assertEqual(last['ack']['seq'],actions[2]['request']['seq'])
                        with self.assertRaises(TimeoutError):
                            client.finish_command(actions[1]['request'],timeout=.001)
                        self.assertEqual([r[2] for r in received],['PING','NAV','FS'])
                        self.assertEqual(len(set(r[1] for r in received)),3)
                finally:
                    client.close(); thread.join(4); server.close()
                self.assertFalse(failures,failures)

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

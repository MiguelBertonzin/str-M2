"""Cliente de laboratorio baseado nos exemplos TCP/UDP do Prof. Felipe Viel.

Somente biblioteca padrao. Firmware da ESP32 em C. Uma sessao e um comando
pendente por cliente; retransmissoes UDP usam exatamente o mesmo identificador.
"""

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import secrets
import socket
import threading
import time


def format_time_status(message):
    """Hora UTC recebida da ESP32; nunca substitui SNTP pelo relogio do PC."""
    utc = "AGUARDANDO_SNTP"
    if message.get("time_synced") and message.get("unix_ms") is not None:
        utc = datetime.fromtimestamp(message["unix_ms"] / 1000, timezone.utc).isoformat(timespec="milliseconds")
    return (f"TIME: ciclo={message['cycle']} UTC={utc} "
            f"sync={message['sync_count']} ciclos_desde_sync={message['cycles_since_sync']} "
            f"idade_sync_ms={message['sync_age_ms']} "
            f"release_dropped={message['release_dropped']}")


class JsonLines:
    def __init__(self):
        self.pending = bytearray()
        self.last_lengths = []

    def feed(self, data):
        self.pending.extend(data)
        messages = []
        self.last_lengths = []
        while b"\n" in self.pending:
            line, _, tail = self.pending.partition(b"\n")
            self.pending = bytearray(tail)
            if len(line) > 65536:
                raise ValueError("Resposta TCP excedeu o limite de uma mensagem")
            if line:
                messages.append(json.loads(line))
                self.last_lengths.append(len(line) + 1)
        if len(self.pending) > 65536:
            raise ValueError("Resposta TCP excedeu o limite sem enquadramento valido")
        return messages


class MonitorClient:
    def __init__(self, host, port=5000, protocol="udp", output="monitor.jsonl", show_time=False):
        self.protocol = protocol.lower()
        self.show_time = show_time
        self.clock_reference = None
        self.session = secrets.randbelow(0xFFFFFFFF) + 1
        self.sequence = 0
        self.output = Path(output).open("a", encoding="utf-8") if output else None
        self.output_lock = threading.Lock()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM if self.protocol == "tcp" else socket.SOCK_DGRAM)
        self.sock.settimeout(5)
        try:
            self.sock.connect((host, port))
        except OSError:
            self.sock.close()
            if self.output:
                self.output.close()
            raise
        self.sock.settimeout(0.2)
        self.condition = threading.Condition()
        self.acks = {}
        self.latest_time = {}
        self.latest_config = {}
        self.latest_status = {}
        self.analyses = {}
        self.failure = None
        self.running = True
        self.thread = threading.Thread(target=self._receive, daemon=True)
        self.thread.start()

    def _save(self, record):
        if self.clock_reference and 'pc_wall_ns' in record:
            record['pc_sntp_estimate_ns'] = record['pc_wall_ns'] + self.clock_reference['offset_ns']
            record['pc_sntp_reference_wall_ns'] = self.clock_reference['pc_wall_ns']
        if self.output:
            with self.output_lock:
                self.output.write(json.dumps(record, ensure_ascii=False) + "\n")
                self.output.flush()

    def _receive(self):
        parser = JsonLines()
        try:
            while self.running:
                try:
                    data = self.sock.recv(65536)
                    received_mono_ns = time.monotonic_ns()
                    received_wall_ns = time.time_ns()
                except socket.timeout:
                    continue
                if not data:
                    raise ConnectionError("ESP32 encerrou a conexao")
                messages = parser.feed(data) if self.protocol == "tcp" else [json.loads(data)]
                lengths = parser.last_lengths if self.protocol == "tcp" else [len(data)]
                for message, wire_size in zip(messages, lengths):
                    if not isinstance(message, dict):
                        raise ValueError("Mensagem deve ser um objeto JSON")
                    record = {"pc_wall_ns": received_wall_ns, "pc_mono_ns": received_mono_ns,
                              "protocol": self.protocol.upper(), "rx_bytes": wire_size, "message": message}
                    self._save(record)
                    with self.condition:
                        if message.get("type") == "time": self.latest_time = message
                        if message.get("type") == "config": self.latest_config = message
                        if message.get("type") == "status": self.latest_status = message
                        if message.get("type") == "analysis":
                            self.analyses[(message["report_seq"], message["task"])] = message
                        self.condition.notify_all()
                    if message.get("type") == "ack" and message.get("session") == self.session:
                        with self.condition:
                            self.acks[message["seq"]] = (message, record["pc_mono_ns"])
                            self.condition.notify_all()
                    elif message.get("type") == "job" and message.get("command_seq"):
                        print("JOB:", json.dumps(message), flush=True)
                    elif message.get("type") == "time" and self.show_time:
                        print(format_time_status(message), flush=True)
        except (OSError, ValueError, ConnectionError) as error:
            if self.running:
                with self.condition:
                    self.failure = error
                    self.condition.notify_all()

    def command(self, command, timeout=2.0, attempts=3):
        command = command.upper()
        if command not in {"HELLO", "PING", "NAV", "FS", "BEGIN", "REPORT"}:
            raise ValueError("Comando deve ser HELLO, PING, NAV, FS, BEGIN ou REPORT")
        if self.sequence >= 0xFFFFFFFF:
            raise ValueError("Sequencia esgotada; abra uma nova sessao")
        self.sequence += 1
        sequence = self.sequence
        payload = f"CMD;{self.session:010d};{sequence:010d};{command}\n".encode("ascii")
        first_sent = None
        retries = attempts if self.protocol == "udp" else 1
        for attempt in range(retries):
            sent_wall_ns = time.time_ns()
            sent_mono_ns = time.monotonic_ns()
            if first_sent is None: first_sent = sent_mono_ns
            # Inicio da chamada de envio; nao e timestamp de transmissao no radio.
            self.sock.sendall(payload)
            self._save({"type": "pc_send", "protocol": self.protocol.upper(),
                        "session": self.session, "seq": sequence, "command": command,
                        "attempt": attempt + 1, "tx_bytes": len(payload),
                        "pc_wall_ns": sent_wall_ns, "pc_mono_ns": sent_mono_ns,
                        "send_return_mono_ns": time.monotonic_ns()})
            limit = time.monotonic() + timeout
            with self.condition:
                while sequence not in self.acks and self.failure is None:
                    remaining = limit - time.monotonic()
                    if remaining <= 0:
                        break
                    self.condition.wait(remaining)
                if sequence in self.acks:
                    message, received_ns = self.acks.pop(sequence)
                    result = {"ack": message, "round_trip_ms": (received_ns - first_sent) / 1e6,
                              "attempts": attempt + 1, "pc_tx_mono_ns": first_sent,
                              "pc_rx_mono_ns": received_ns}
                    self._save({"type": "pc_roundtrip", "protocol": self.protocol.upper(),
                                "session": self.session, "seq": sequence, "command": command,
                                "pc_tx_mono_ns": first_sent, "pc_rx_mono_ns": received_ns,
                                **result})
                    # Inclui a espera pelo ciclo de 500 ms; nao e latencia pura de Wi-Fi.
                    return result
                if self.failure:
                    raise ConnectionError(str(self.failure))
        self._save({"type": "command_timeout", "session": self.session, "seq": sequence,
                    "command": command, "attempts": retries, "pc_mono_ns": time.monotonic_ns()})
        raise TimeoutError(f"Sem ACK para {command}; resultado da execucao e incerto")

    def wait_for(self, predicate, timeout=10):
        limit = time.monotonic() + timeout
        with self.condition:
            while not predicate(self):
                if self.failure: raise ConnectionError(str(self.failure))
                remaining = limit - time.monotonic()
                if remaining <= 0: raise TimeoutError("Estado esperado nao recebido")
                self.condition.wait(remaining)

    def report(self, timeout=8):
        result = self.command("REPORT")
        seq = result["ack"]["seq"]
        self.wait_for(lambda c: sum(key[0] == seq for key in c.analyses) == 6, timeout)
        with self.condition:
            return [value.copy() for (report, _), value in self.analyses.items() if report == seq]

    def close(self):
        self.running = False
        self.sock.close()
        self.thread.join(timeout=2)
        if self.output:
            self.output.close()


def main():
    parser = argparse.ArgumentParser(description="MONITOR_TASK: comandos e logs da ESP32")
    parser.add_argument("--host", required=True, help="IP mostrado em MONITOR_IP")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--protocol", choices=["udp", "tcp"], default="udp")
    parser.add_argument("--output", default="monitor.jsonl")
    parser.add_argument("--show-time", action="store_true", help="Mostrar hora e ciclos SNTP recebidos da ESP32")
    args = parser.parse_args()
    client = MonitorClient(args.host, args.port, args.protocol, args.output, args.show_time)
    try:
        print("HELLO:", client.command("HELLO"))
        print("Comandos: PING, NAV, FS, BEGIN, REPORT, SAIR. BEGIN inicia uma janela por boot.")
        print("FS trava o estado seguro ate reiniciar a placa.")
        print("Logs recebidos salvos em", args.output)
        print("Mesmo quando JOB/TIME aparece depois de >, pode digitar o proximo comando e Enter.")
        while True:
            command = input("> ").strip().upper()
            if command in {"SAIR", "EXIT", "QUIT"}:
                break
            if command:
                try:
                    print(json.dumps(client.command(command), ensure_ascii=False))
                except ValueError as error:
                    print(error)
    finally:
        client.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ConnectionError, TimeoutError) as error:
        raise SystemExit(str(error))

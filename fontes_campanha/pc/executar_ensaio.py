"""Coleta um ensaio com serial, rede, roteiro, referencias SNTP e resumo final."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import socket
import subprocess
import sys
import threading
import time
from uuid import uuid4

from monitor_pc import MonitorClient
from relogio_sntp import query
from agenda import (CAMPAIGN, REVISION, FIRMWARE_REVISION, ACK_TIMEOUT_S, SEND_TOLERANCE_MS,
                    RUN_SECONDS, SNTP_INTERVAL_S, DRAIN_SECONDS,
                    schedule, send_schedule, collect_responses)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from configurar_teste import SCENARIOS


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def report_progress(sequence, received, tasks):
    missing = ', '.join(task for task in tasks if task not in received)
    print(f'REPORT {sequence}: {len(received)}/{len(tasks)} tarefas recebidas'
          + (f'; aguardando {missing}' if missing else '; estatisticas completas'), flush=True)


def prepare_folder(folder, root, overwrite=False):
    """Reutiliza o nome somente com opcao explicita, arquivando a coleta anterior."""
    root = Path(root).resolve()
    folder = Path(folder)
    collection_root = (root / 'ensaios_M2/02_coletas').resolve()
    if folder.is_symlink() or not folder.resolve().is_relative_to(collection_root):
        raise ValueError('Pasta de coleta fora do diretorio permitido.')
    archived = None
    if folder.exists():
        if not overwrite:
            raise FileExistsError('A coleta ja existe. Use outra --tentativa ou '
                                  '--sobrescrever para arquivar a anterior e reutilizar o nome.')
        if not folder.is_dir():
            raise ValueError('O caminho da coleta existe, mas nao e uma pasta.')
        archive_root = root / 'ensaios_M2/03_anexos_chat/tentativas_substituidas'
        if not archive_root.resolve().is_relative_to(root):
            raise ValueError('Pasta de arquivo fora do projeto.')
        archive_root.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
        archived = archive_root / f'{folder.parent.name}_{folder.name}_{stamp}_{uuid4().hex[:8]}'
        folder.rename(archived)
    folder.mkdir(parents=True, exist_ok=False)
    return archived


def validate_endpoint(reported, requested, scenario):
    host, protocol, port = reported
    if protocol != SCENARIOS[scenario]['protocol'] or port != 5000:
        raise ValueError('Protocolo/porta da placa diferente do cenario. '
                         'Execute configurar_teste.py e depois idf.py -p PORTA flash.')
    if requested != 'auto' and requested != host:
        raise ValueError(f'IP da placa mudou: MONITOR_IP={host}, --host={requested}. '
                         'Use --host auto ou o novo IP.')
    return host


def validate_config(actual, scenario, touch, diagnostic):
    expected = dict(SCENARIOS[scenario], revision=FIRMWARE_REVISION,
                    input='TOUCH' if touch else 'AUTO', duration_s=RUN_SECONDS,
                    sample_n=100, time_enabled=not diagnostic,
                    auto_wait_pc=not touch, tick_hz=1000, sntp_interval_s=0 if diagnostic else SNTP_INTERVAL_S)
    differences = {k: (actual.get(k), v) for k, v in expected.items() if actual.get(k) != v}
    if differences: raise ValueError(f'Firmware diferente do cenario pedido (atual, esperado): {differences}')
    if actual.get('run_start_us', 0): raise ValueError('Ensaio ja iniciado. Reinicie a placa.')


class SerialCapture:
    def __init__(self, port, directory):
        import serial  # Disponivel no ambiente ESP-IDF.
        self.output = (directory / 'serial.txt').open('xb')
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=0.2)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        try: self.serial.open()
        except Exception:
            self.output.close()
            raise
        self.serial.reset_input_buffer()
        self.running = True
        self.error = None
        self.network_ready = threading.Event()
        self.endpoint = None
        self.thread = threading.Thread(target=self.read, daemon=True)
        self.thread.start()

    def read(self):
        pending = bytearray()
        try:
            while self.running:
                data = self.serial.read(4096)
                if data:
                    self.output.write(data)
                    self.output.flush()
                    pending.extend(data)
                    while b'\n' in pending:
                        line, _, pending = pending.partition(b'\n')
                        text = line.decode('utf-8', errors='replace').strip()
                        network = re.match(r'^MONITOR_IP\s*\|\s*([0-9.]+)\s*\|\s*(UDP|TCP)\s*\|\s*porta=(\d+)', text)
                        if network:
                            self.endpoint = (network[1], network[2], int(network[3]))
                            self.network_ready.set()
                        if text.startswith(('GUIA', 'MONITOR_IP', 'TIME_SYNC', 'RUN_BEGIN', 'AUTO_ERROR')):
                            print('[ESP32]', text, flush=True)
        except Exception as error:
            if self.running: self.error = str(error)

    def wait_network(self, timeout=60):
        limit = time.perf_counter() + timeout
        while not self.network_ready.wait(min(1, max(0, limit-time.perf_counter()))):
            if self.error: raise OSError(self.error)
            if time.perf_counter() >= limit:
                raise TimeoutError('MONITOR_IP nao apareceu. Confira Wi-Fi/serial ou use --sem-reset.')
        return self.endpoint

    def reset(self):
        self.serial.dtr = False
        self.serial.rts = True
        time.sleep(0.15)
        self.serial.rts = False

    def close(self):
        self.running = False
        self.thread.join(2)
        self.serial.close()
        self.output.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cenario', required=True, choices=SCENARIOS)
    p.add_argument('--repeticao', required=True, help='P01, R01..R05, T01 ou D01')
    p.add_argument('--tentativa', type=int, default=1, help='Use 2, 3... para repetir uma coleta sem sobrescrever a anterior')
    p.add_argument('--sobrescrever', action='store_true',
                   help='Arquiva a coleta existente e reutiliza sua pasta para uma nova execucao')
    p.add_argument('--host', default='auto', help='auto (padrao): le MONITOR_IP pela serial; ou IP explicito')
    p.add_argument('--serial', required=True, help='Exemplo: COM7; feche idf.py monitor antes')
    p.add_argument('--touch', action='store_true', help='Roteiro fisico guiado, sem NAV/FS automaticos pelo PC')
    p.add_argument('--sem-reset', action='store_true', help='Para placas sem circuito de auto-reset: pressione EN manualmente')
    p.add_argument('--ntp-server', default='pool.ntp.org')
    args = p.parse_args()
    if not 1 <= args.tentativa <= 99: p.error('--tentativa deve estar entre 1 e 99')
    if not re.fullmatch(r'(P\d{2}|R0[1-5]|T\d{2}|D\d{2})', args.repeticao):
        p.error('Use P01 (piloto), R01..R05 (bateria), T01 (touch) ou D01 (TIME OFF).')
    if args.touch != args.repeticao.startswith('T'): p.error('--touch exige repeticao Txx e vice-versa')
    diagnostic = args.repeticao.startswith('D')
    attempt_label = args.repeticao if args.tentativa == 1 else f'{args.repeticao}_T{args.tentativa:02d}'
    folder = ROOT / 'ensaios_M2/02_coletas' / CAMPAIGN / args.cenario / attempt_label
    try:
        archived = prepare_folder(folder, ROOT, args.sobrescrever)
    except (OSError, ValueError) as error:
        p.error(str(error))
    if archived:
        print(f'Tentativa anterior arquivada em: {archived}', flush=True)
    metadata = dict(id=f'{args.cenario}_{attempt_label}', scenario=args.cenario,
                    campaign=CAMPAIGN, collector_revision=REVISION, duration_s=RUN_SECONDS,
                    agenda_policy=dict(send_attempts=1, ack_timeout_s=ACK_TIMEOUT_S,
                                       send_tolerance_ms=SEND_TOLERANCE_MS,
                                       origin='primeiro envio BEGIN; timestamps ESP conferidos separadamente'),
                    repetition=args.repeticao, created_utc=datetime.now(timezone.utc).isoformat(),
                    attempt=args.tentativa,
                    status='INICIADO', host_requested=args.host, serial=args.serial, touch=args.touch,
                    diagnostic=diagnostic, source_hashes={})
    for base in ('main', 'pc', 'tools'):
        for source in (ROOT / base).glob('*'):
            if source.is_file():
                metadata['source_hashes'][source.relative_to(ROOT).as_posix()] = hashlib.sha256(source.read_bytes()).hexdigest()
    write_json(folder / 'manifesto.json', metadata)
    capture = client = None
    try:
        capture = SerialCapture(args.serial, folder)
        if not args.sem_reset: capture.reset()
        else: print('Pressione EN/RESET agora; coleta serial iniciada.', flush=True)
        print('Aguardando boot e rede da ESP32...', flush=True)
        host = validate_endpoint(capture.wait_network(), args.host, args.cenario)
        metadata['host'] = host
        write_json(folder / 'manifesto.json', metadata)
        print(f'Campanha {CAMPAIGN} | {args.cenario}/{attempt_label} | ESP32 {host}', flush=True)
        limit = time.perf_counter() + 60
        while True:
            try:
                client = MonitorClient(host, protocol=SCENARIOS[args.cenario]['protocol'].lower(),
                                       output=folder / 'rede.jsonl', show_jobs=False)
                client.command('HELLO')
                client.wait_for(lambda c: bool(c.latest_config), 5)
                break
            except (OSError, TimeoutError) as error:
                if client: client.close(); client = None
                if time.perf_counter() > limit: raise TimeoutError('Sem conexao; confira IP/porta/serial. ' + str(error))
                time.sleep(2)
        validate_config(client.latest_config, args.cenario, args.touch, diagnostic)
        metadata['config'] = client.latest_config.copy()
        print('Conferindo SNTP do notebook e da ESP32...', flush=True)
        metadata['pc_sntp_initial'] = query(args.ntp_server)
        client.clock_reference = metadata['pc_sntp_initial']
        if not diagnostic: client.wait_for(lambda c: c.latest_time.get('time_synced'), 90)
        metadata['esp_time_initial'] = client.latest_time.copy()
        metadata['network_initial'] = client.latest_status.copy()
        # Nao repetir BEGIN: um ACK perdido nao autoriza reiniciar/realinhar a janela.
        start_ack = client.command('BEGIN', timeout=2, attempts=1)
        if start_ack['ack']['status'] != 'STARTED': raise ValueError(str(start_ack))
        # Agenda no relogio do PC desde a chamada de envio de BEGIN. A diferenca
        # ate BEGIN ser interpretado na ESP e parte do atraso observado, nao zerada.
        origin = start_ack['pc_tx_mono_ns'] / 1e9
        metadata['begin_ack'] = start_ack
        metadata['status'] = 'MEDINDO'
        write_json(folder / 'manifesto.json', metadata)
        print(f'MEDICAO INICIADA: {RUN_SECONDS} segundos. Mantenha o cabo conectado ate "Arquivos salvos em:" e o retorno ao PowerShell.', flush=True)
        events = ([(float(t), 'PING') for t in range(2, RUN_SECONDS-1, 2)] if args.touch else schedule())
        write_json(folder / 'roteiro.json', events)
        if args.touch: print('Agora siga GUIA: C B C A B C B C B C B+C D C. Uma vez por boot.', flush=True)
        actions = send_schedule(client, events, origin, folder)
        # Inclui margem para o BEGIN chegar e para jobs pendentes terminarem.
        time.sleep(max(0, origin + RUN_SECONDS + DRAIN_SECONDS - time.perf_counter()))
        collect_responses(client, actions, folder)
        print('Medicao encerrada. Recebendo estatisticas finais; mantenha o cabo conectado...', flush=True)
        for attempt in range(3):
            try:
                analyses = client.report(timeout=30, on_progress=report_progress)
                break
            except TimeoutError:
                if attempt == 2: raise
        write_json(folder / 'estatisticas.json', analyses)
        metadata['network_final'] = client.latest_status.copy()
        metadata['config_final'] = client.latest_config.copy()
        metadata['esp_time_final'] = client.latest_time.copy()
        try: metadata['pc_sntp_final'] = query(args.ntp_server)
        except (OSError, ValueError) as error: metadata['pc_sntp_final_error'] = str(error)
        metadata['serial_error'] = capture.error
        metadata['status'] = 'COLETADO' if not capture.error else 'COLETA_INCOMPLETA'
    except BaseException as error:
        metadata['status'] = 'INTERROMPIDO' if isinstance(error, KeyboardInterrupt) else 'FALHOU'
        metadata['error'] = str(error)
        raise
    finally:
        if client and client.analyses:
            # Preserva respostas parciais mesmo quando o REPORT nao completa.
            with client.condition:
                partial = [value.copy() for value in client.analyses.values()]
            write_json(folder / 'estatisticas_recebidas.json', partial)
        if client: client.close()
        if capture: capture.close()
        metadata['finished_utc'] = datetime.now(timezone.utc).isoformat()
        write_json(folder / 'manifesto.json', metadata)
        subprocess.run([sys.executable, str(ROOT / 'tools/atualizar_indice.py')], check=False)
    subprocess.run([sys.executable, str(ROOT / 'tools/analisar_ensaio.py'), str(folder)], check=True)
    subprocess.run([sys.executable, str(ROOT / 'tools/atualizar_indice.py')], check=True)
    print('Arquivos salvos em:', folder)


if __name__ == '__main__':
    try: main()
    except (OSError, ValueError, TimeoutError) as error: raise SystemExit(str(error))

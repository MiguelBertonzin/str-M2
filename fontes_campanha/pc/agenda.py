"""Contrato experimental: envios absolutos sem esperar respostas."""
import json
import time

CAMPAIGN = 'AGENDA_60S_R2'
REVISION = 'M2-AGENDA-60S-2'
FIRMWARE_REVISION = 'M2-METRICS-4'
RUN_SECONDS = 60
SNTP_INTERVAL_S = 20
DRAIN_SECONDS = 7
ACK_TIMEOUT_S = 5.0
SEND_TOLERANCE_MS = 100.0


def schedule(duration=RUN_SECONDS):
    # 5 s de margem inicial, 3 s entre ultima NAV ativa e D (aos 24 s).
    events = [(float(t), 'NAV') for t in range(5, 22, 2)]
    events += [(float(t), 'PING') for t in range(2, duration - 1, 2)]
    events += [(float(t) + .75, 'FS') for t in range(30, duration - 5, 10)]
    events += [(27.0, 'NAV')]
    return sorted(events)


def expected_status(command, planned_s):
    if command == 'PING': return 'OK'
    if command == 'NAV' and planned_s == 27: return 'SAFE_ACTIVE'
    return 'QUEUED'


def send_schedule(client, events, origin, folder, duration=RUN_SECONDS,
                  clock=time.perf_counter, sleep=time.sleep):
    """Emite no horario absoluto e persiste cada envio antes de passar ao seguinte.

    Nao espera ACK nem reenvia datagramas. Se o PC perder um horario por mais de
    100 ms, registra o comando nao enviado: nao produz uma rajada de compensacao.
    Atraso/erro de envio e atraso de resposta sao resultados distintos.
    """
    actions = []
    with (folder / 'envios.jsonl').open('x', encoding='utf-8') as stream:
        for offset, command in events:
            # Reavaliar o relogio: sleep pode retornar antes do alvo observado.
            while clock() < origin + offset:
                sleep(max(0, origin + offset - clock()))
            actual = clock() - origin
            record = dict(planned_s=offset, actual_s=actual,
                          lateness_ms=max(0, actual-offset)*1000, command=command)
            if actual >= duration or record['lateness_ms'] > SEND_TOLERANCE_MS:
                record['skipped'] = 'Horario perdido no PC; nao enviado para evitar rajada'
            else:
                try:
                    request = client.start_command(command)
                    record['request'] = request
                    # Horario efetivo da chamada send, nao da iteracao do loop.
                    record['actual_s'] = request['pc_tx_mono_ns']/1e9-origin
                    record['lateness_ms'] = max(0, record['actual_s']-offset)*1000
                    record['send_duration_ms'] = (request['send_return_mono_ns']-request['pc_tx_mono_ns'])/1e6
                except OSError as error:
                    record['error'] = str(error)
            actions.append(record)
            stream.write(json.dumps(record)+'\n'); stream.flush()
            print(f"{record['actual_s']:6.2f}s {command}: "
                  f"{'ENVIADO' if 'request' in record else record.get('error', record.get('skipped'))}", flush=True)
            if client.failure:
                raise ConnectionError(f'Conexao interrompida: {client.failure}')
    return actions


def collect_responses(client, actions, folder):
    with (folder / 'comandos.jsonl').open('x', encoding='utf-8') as stream:
        for record in actions:
            if 'request' in record:
                try:
                    record['result'] = client.finish_command(record['request'], ACK_TIMEOUT_S)
                except TimeoutError as error:
                    record['error'] = str(error)
            stream.write(json.dumps(record)+'\n'); stream.flush()

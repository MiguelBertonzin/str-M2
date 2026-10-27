"""Analisa acumuladores completos da ESP32; nao calcula percentis de logs amostrados."""
import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics

SOFT = {'NAV_PLAN', 'MONITOR_TASK', 'TIME_TASK'}
HARD = {'FUS_IMU', 'CTRL_ATT', 'FAIL_SAFE'}


def percentile(values, fraction):
    return sorted(values)[max(0, math.ceil(len(values) * fraction) - 1)] if values else None


def cpu_windows(text, begin, end):
    origin = re.search(r'^MEASUREMENT_ORIGIN_US=(\d+)', text, re.MULTILINE)
    samples = {}
    malformed = 0
    if not origin: return samples, ['Origem da medicao ausente no serial; CPU nao consolidada.']
    origin = int(origin[1])
    for line in text.splitlines():
        if not line.startswith('METRIC;') or line.startswith('METRIC;tempo'): continue
        fields = line.strip().split(';')
        try:
            if len(fields) != 20: raise ValueError()
            now = origin + float(fields[1]) * 1e6
            # Somente janelas de 5 s completamente dentro do ensaio.
            if now - 5_000_000 >= begin and now <= end:
                samples.setdefault(fields[3], []).append(float(fields[16]))
        except ValueError: malformed += 1
    return samples, ([f'{malformed} linhas METRIC incompletas no serial.'] if malformed else [])


def summarize_stats(stats):
    rows = []
    for a in stats:
        jobs = a['jobs']
        uncertain = a['lost'] or a['invalid']
        mk = ('INCONCLUSIVO: perdas/invalidos' if uncertain else
              'SEM JANELA COMPLETA' if not a['windows'] else
              'VIOLADO' if a['violations'] else 'ATENDIDO NO ENSAIO')
        rows.append(dict(task=a['task'], kind='SOFT' if a['task'] in SOFT else 'HARD',
                         jobs=jobs, misses=a['misses'], lost=a['lost'], invalid=a['invalid'],
                         miss_pct=100*a['misses']/jobs if jobs else None,
                         exec_mean_ms=a['exec_sum_us']/jobs/1000 if jobs else None,
                         exec_max_ms=a['exec_max_us']/1000 if jobs else None,
                         wcrt_ms=a['wcrt_us']/1000 if jobs else None,
                         p99_low_ms=a['p99_low_us']/1000 if jobs else None,
                         p99_high_ms=min(a['p99_high_us'], a['wcrt_us'])/1000 if jobs else None,
                         latency_max_ms=a['latency_max_us']/1000 if jobs else None,
                         jitter_mean_ms=a.get('jitter_sum_us', 0)/jobs/1000 if jobs else None,
                         jitter_max_ms=a.get('jitter_max_us', 0)/1000 if jobs else None,
                         isr_jobs=a.get('isr_jobs', 0),
                         isr_latency_max_ms=a.get('isr_latency_max_us', 0)/1000 if a.get('isr_jobs') else None,
                         isr_safe_max_ms=a.get('isr_safe_max_us', 0)/1000 if a['task'] == 'FAIL_SAFE' and a.get('isr_jobs') else None,
                         safe_max_ms=a['safe_max_us']/1000 if a['task'] == 'FAIL_SAFE' and jobs else None,
                         mk_status=mk if a['task'] in SOFT else 'NAO APLICADO',
                         mk_windows=a['windows'] if a['task'] in SOFT else None,
                         mk_violations=a['violations'] if a['task'] in SOFT else None))
    return rows


def analyze(folder):
    metadata = json.loads((folder / 'manifesto.json').read_text(encoding='utf-8-sig'))
    stats = json.loads((folder / 'estatisticas.json').read_text(encoding='utf-8-sig'))
    names = {a['task'] for a in stats}
    if names != HARD | SOFT or len(stats) != 6: raise ValueError('Faltam estatisticas de alguma tarefa')
    begin, end = stats[0]['run_start_us'], stats[0]['run_end_us']
    if begin <= 0 or end <= begin: raise ValueError('Janela de ensaio nao iniciada')
    if len({(a['run_start_us'], a['run_end_us'], a['report_seq']) for a in stats}) != 1:
        raise ValueError('Estatisticas de janelas/REPORT diferentes')
    serial = (folder / 'serial.txt').read_text(encoding='utf-8', errors='replace')
    cpu, flags = cpu_windows(serial, begin, end)
    rows = summarize_stats(stats)
    for row in rows:
        values = cpu.get(row['task'], [])
        row['cpu_window_mean_pct'] = statistics.mean(values) if values else None
        row['cpu_window_max_pct'] = max(values) if values else None
    for task, period in [('FUS_IMU', 5000), ('MONITOR_TASK', 500000), ('TIME_TASK', 1000000)]:
        if task == 'TIME_TASK' and metadata.get('diagnostic'): continue
        a = next(a for a in stats if a['task'] == task)
        expected = (end - begin) // period
        if abs(a['jobs'] + a['lost'] + a['invalid'] - expected) > 1:
            flags.append(f'{task}: contagem de liberacoes diferente do esperado; verificar backlog/coleta.')
    if any(a['invalid'] for a in stats): flags.append('Ha timestamps invalidos; nao usar como resultado final.')
    actions = [json.loads(line) for line in (folder / 'comandos.jsonl').read_text().splitlines() if line]
    expected_counts = {'PING': 89} if metadata.get('touch') else {'PING': 89, 'NAV': 12, 'FS': 15}
    actual_counts = {cmd: sum(a.get('command') == cmd for a in actions) for cmd in expected_counts}
    if actual_counts != expected_counts:
        flags.append(f'Agenda incompleta: {actual_counts}; esperado {expected_counts}.')
    if 'GUIA | sequencia concluida' not in serial:
        flags.append('Serial nao confirma os 14 eventos do roteiro.')
    if 'inesperado ou antes da instrucao' in serial:
        flags.append('Roteiro touch fora de ordem/antecipado.')
    if serial.count('MEASUREMENT_ORIGIN_US=') > 1:
        flags.append('Mais de um boot registrado; verificar reinicio durante a coleta.')
    late = sum(a['lateness_ms'] > 500 for a in actions)
    if late: flags.append(f'{late} comandos atrasaram mais de 500 ms em relacao ao roteiro do PC.')
    failures = sum('error' in a or 'skipped' in a for a in actions)
    if failures: flags.append(f'{failures} comandos sem resultado confirmado/nao enviados.')
    records = [json.loads(line) for line in (folder / 'rede.jsonl').read_text(encoding='utf-8').splitlines() if line]
    rtts = [r['round_trip_ms'] for r in records if r.get('type') == 'pc_roundtrip'
            and r.get('command') in ('PING', 'NAV', 'FS')]
    if not rtts: flags.append('Nenhum RTT de comando de teste registrado.')
    samples = [r for r in records if r.get('type') == 'pc_roundtrip' and r.get('command') == 'PING']
    network = dict(rtt_n=len(rtts), rtt_mean_ms=statistics.mean(rtts) if rtts else None,
                   rtt_p99_ms=percentile(rtts, .99), rtt_max_ms=max(rtts) if rtts else None,
                   ping_n=len(samples), ping_rtt_mean_ms=statistics.mean(r['round_trip_ms'] for r in samples) if samples else None,
                   commands_failed=failures, late_commands=late)
    initial, final = metadata.get('network_initial', {}), metadata.get('network_final', {})
    for key in ('log_dropped', 'tx_dropped', 'release_dropped', 'bad_frames'):
        network[key + '_delta_observed'] = final.get(key, 0) - initial.get(key, 0)
    if any(r.get('message', {}).get('tx_bytes', r.get('rx_bytes')) != r.get('rx_bytes') for r in records if 'rx_bytes' in r):
        flags.append('Tamanho recebido diverge do tx_bytes declarado.')
    syncs = metadata.get('esp_time_final', {}).get('sync_count', 0) - metadata.get('esp_time_initial', {}).get('sync_count', 0)
    if not metadata.get('diagnostic') and syncs < 2: flags.append('Menos de duas novas atualizacoes SNTP durante o ensaio.')
    if metadata.get('pc_sntp_final_error'): flags.append('Consulta SNTP final do PC falhou; verificar referencia UTC.')
    if 'AUTO_ERROR' in serial or 'Guru Meditation' in serial or 'abort()' in serial:
        flags.append('Erro de firmware registrado no serial.')
    if metadata.get('status') != 'COLETADO': flags.append('Coleta nao terminou normalmente.')
    hard_failures = sum(r['misses'] + r['lost'] + r['invalid'] for r in rows if r['kind'] == 'HARD')
    soft_failures = sum(r['misses'] + r['lost'] + r['invalid'] for r in rows if r['kind'] == 'SOFT')
    if sum(a['jobs'] for a in stats if a['task'] == 'FAIL_SAFE') == 0: flags.append('Sem execucao de FS neste ensaio.')
    result = dict(id=metadata['id'], status='REVISAR' if flags else 'COLETA_CONFERIDA',
                  flags=flags, hard_failures=hard_failures, soft_failures=soft_failures,
                  all_deadlines_observed=(hard_failures == 0 and soft_failures == 0 and not flags),
                  duration_s=(end-begin)/1e6, new_sntp_updates=syncs, tasks=rows, network=network)
    (folder / 'resumo.json').write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding='utf-8')
    with (folder / 'tarefas.csv').open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]), delimiter=';')
        writer.writeheader(); writer.writerows(rows)
    lines = [f"# {metadata['id']}", '', f"Estado da coleta: **{result['status']}**.", '',
             '| Tarefa | Jobs | Misses | Perdidos | WCRT (ms) | HWM99 intervalo (ms) |',
             '|---|---:|---:|---:|---:|---|']
    for r in rows:
        lines.append(f"| {r['task']} | {r['jobs']} | {r['misses']} | {r['lost']} | {r['wcrt_ms']} | {r['p99_low_ms']} .. {r['p99_high_ms']} |")
    lines += ['', 'HWM100 = WCRT observado. HWM99 usa histograma de 100 us; o intervalo e um limite, nao um percentil exato.',
              'Os acumuladores incluem todos os jobs validos da janela; os logs individuais FUS/CTRL sao amostrados.', '',
              f'Falhas hard (misses + perdidos + invalidos): {hard_failures}. Soft: {soft_failures}.',
              f'RTT: {network["rtt_n"]} respostas; media={network["rtt_mean_ms"]} ms; max={network["rtt_max_ms"]} ms.',
              'RTT inclui espera pelo monitor e retransmissoes; nao e apenas tempo de radio.', '', '## Conferencias', '']
    lines += ['- ' + flag for flag in flags] or ['- Nenhuma inconsistencia automatica detectada; revisar logs e contexto antes do relatorio.']
    lines += ['', 'Detalhes de CPU e (9,10)-firm por tarefa em tarefas.csv; transmissao e descartes em resumo.json.',
              'Perdas tornam (m,k) sobre ativacoes inconclusivo. Uma coleta conferida pode ter deadlines descumpridos.']
    (folder / 'RESUMO.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print(metadata['id'], result['status'], f'hard={hard_failures}, soft={soft_failures}')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('pasta', type=Path)
    analyze(parser.parse_args().pasta)

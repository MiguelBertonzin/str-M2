"""Gera tabelas, figuras e resumos da campanha final, sem modificar os dados."""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics as st

ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = ('M_UDP', 'M_TCP', 'P_UDP', 'P_TCP')
TASKS = ('FUS_IMU', 'CTRL_ATT', 'NAV_PLAN', 'FAIL_SAFE', 'MONITOR_TASK', 'TIME_TASK')
DEADLINES = dict(zip(TASKS, (5, 5, 20, 10, 500, 1000)))
COLORS = ('#277DA8', '#43AA8B', '#E9A23B', '#C85552')


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def percentile(values, p):
    return sorted(values)[max(0, math.ceil(len(values) * p) - 1)]


def write_csv(path, rows):
    if not rows:
        return
    with path.open('w', newline='', encoding='utf-8-sig') as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0]), delimiter=';')
        writer.writeheader()
        writer.writerows(rows)


def fmt(value, digits=3):
    return f'{value:.{digits}f}'.replace('.', ',')


def table(rows, fields):
    lines = ['| ' + ' | '.join(title for _, title in fields) + ' |',
             '|' + '|'.join('---' for _ in fields) + '|']
    for row in rows:
        values = [fmt(row[key]) if isinstance(row[key], float) else str(row[key]) for key, _ in fields]
        lines.append('| ' + ' | '.join(values) + ' |')
    return '\n'.join(lines)


def load_runs(base):
    runs, tasks, pings, clocks, packets, provenance = [], [], [], [], [], []
    for scenario in SCENARIOS:
        for number in range(1, 6):
            repetition = f'R{number:02}'
            folder = base / scenario / repetition
            summary = read_json(folder / 'resumo.json')
            manifest = read_json(folder / 'manifesto.json')
            stats = {r['task']: r for r in read_json(folder / 'estatisticas.json')}
            assert summary['status'] == 'COLETA_CONFERIDA', folder
            assert not summary['flags'] and summary['duration_s'] == 60, folder
            assert set(stats) == set(TASKS), folder
            records = [json.loads(line) for line in (folder / 'rede.jsonl').read_text(encoding='utf-8').splitlines() if line]
            samples = [r for r in records if r.get('type') == 'pc_roundtrip' and r.get('command') == 'PING']
            assert len(samples) == 29 and all(r['attempts'] == 1 for r in samples), folder
            for sample in samples:
                assert abs(sample['round_trip_ms'] - (sample['pc_rx_mono_ns'] - sample['pc_tx_mono_ns']) / 1e6) < 1e-6
                pings.append(dict(cenario=scenario, repeticao=repetition, seq=sample['seq'],
                                  rtt_ms=sample['round_trip_ms'], pc_tx_mono_ns=sample['pc_tx_mono_ns'],
                                  pc_rx_mono_ns=sample['pc_rx_mono_ns']))
            rtt = [r['round_trip_ms'] for r in samples]
            net = summary['network']
            runs.append(dict(cenario=scenario, repeticao=repetition, inicio_utc=manifest['created_utc'],
                duracao_s=summary['duration_s'], status=summary['status'],
                hard=summary['hard_failures'], soft=summary['soft_failures'],
                comandos=summary['agenda']['sent_commands'], acks_ausentes=summary['agenda']['missing_acks'],
                nav_logs=summary['agenda']['event_trace_counts']['NAV_PLAN']['received'],
                fs_logs=summary['agenda']['event_trace_counts']['FAIL_SAFE']['received'],
                ping_n=len(rtt), ping_media_ms=st.mean(rtt), ping_mediana_ms=st.median(rtt),
                ping_p95_ms=percentile(rtt, .95), ping_p99_ms=percentile(rtt, .99), ping_max_ms=max(rtt),
                rssi_inicio_dbm=manifest['config']['wifi_rssi_dbm'],
                rssi_final_dbm=manifest.get('config_final', {}).get('wifi_rssi_dbm'),
                canal_inicio=manifest['config']['wifi_channel'],
                novas_syncs=summary['new_sntp_updates'],
                descartes_log_observados=net['log_dropped_delta_observed'],
                descartes_tx_observados=net['tx_dropped_delta_observed'],
                descartes_evento_observados=net['event_log_dropped_delta_observed']))
            for t in summary['tasks']:
                a = stats[t['task']]
                tasks.append(dict(cenario=scenario, repeticao=repetition, tarefa=t['task'], tipo=t['kind'],
                    deadline_ms=DEADLINES[t['task']], jobs=t['jobs'], misses=t['misses'], perdidas=t['lost'],
                    invalidas=t['invalid'], miss_pct_jobs=t['miss_pct'],
                    falha_pct_ativacoes=100 * (t['misses'] + t['lost'] + t['invalid']) /
                        (t['jobs'] + t['lost'] + t['invalid']),
                    wcrt_ms=t['wcrt_ms'], hwm99_inf_ms=t['p99_low_ms'], hwm99_sup_ms=t['p99_high_ms'],
                    hist_overflow=a['hist_overflow'], c_decorrido_media_ms=t['exec_mean_ms'],
                    c_decorrido_max_ms=t['exec_max_ms'], jitter_media_ms=t['jitter_mean_ms'],
                    jitter_max_ms=t['jitter_max_ms'], isr_jobs=t['isr_jobs'],
                    isr_latencia_max_ms=t['isr_latency_max_ms'], isr_acao_segura_max_ms=t['isr_safe_max_ms'],
                    acao_segura_max_ms=t['safe_max_ms'],
                    cpu_media_janelas_pct=t['cpu_window_mean_pct'], cpu_max_janela_pct=t['cpu_window_max_pct'],
                    mk_estado=t['mk_status'], mk_janelas=t['mk_windows'], mk_violacoes=t['mk_violations'],
                    mk_min_acertos=a['min_hits'] if t['kind'] == 'SOFT' else None))
            c0, c1 = manifest['pc_sntp_initial'], manifest['pc_sntp_final']
            clocks.append(dict(cenario=scenario, repeticao=repetition,
                offset_pc_inicial_ms=c0['offset_ns']/1e6, offset_pc_final_ms=c1['offset_ns']/1e6,
                variacao_offset_ms=(c1['offset_ns']-c0['offset_ns'])/1e6,
                atraso_consulta_inicial_ms=c0['network_delay_ns']/1e6,
                atraso_consulta_final_ms=c1['network_delay_ns']/1e6,
                syncs_esp_na_janela=summary['new_sntp_updates']))
            send = {(r['session'], r['seq']): r for r in records if r.get('type') == 'pc_send'}
            for r in records:
                msg = r.get('message', {})
                if msg.get('type') != 'ack' or msg.get('command') not in ('PING', 'NAV', 'FS'):
                    continue
                s = send[(msg['session'], msg['seq'])]
                packets.append(dict(cenario=scenario, repeticao=repetition, comando=msg['command'],
                    protocolo=msg['protocol'], seq=msg['seq'], pc_envio_wall_ns=s['pc_wall_ns'],
                    pc_recepcao_wall_ns=r['pc_wall_ns'], pc_envio_mono_ns=s['pc_mono_ns'],
                    pc_recepcao_mono_ns=r['pc_mono_ns'], pc_envio_bytes=s['tx_bytes'],
                    pc_recepcao_bytes=r['rx_bytes'], esp_socket_rx_unix_ms=msg.get('socket_rx_unix_ms'),
                    esp_socket_rx_mono_us=msg.get('socket_rx_us'), esp_envio_unix_ms=msg.get('tx_unix_ms'),
                    esp_envio_mono_us=msg.get('tx_mono_us'), esp_sync_count=msg.get('tx_sync_count'),
                    offset_pc_inicial_ms=c0['offset_ns']/1e6, offset_pc_final_ms=c1['offset_ns']/1e6))
            for name in ('manifesto.json', 'resumo.json', 'estatisticas.json', 'rede.jsonl', 'serial.txt', 'comandos.jsonl', 'envios.jsonl'):
                p = folder / name
                provenance.append(dict(arquivo=f'{scenario}/{repetition}/{name}', bytes=p.stat().st_size,
                                       sha256=hashlib.sha256(p.read_bytes()).hexdigest()))
    assert len(runs) == 20 and len(tasks) == 120 and len(pings) == 580
    assert sum(r['comandos'] for r in runs) == 840
    return runs, tasks, pings, clocks, packets, provenance


def aggregate(runs, tasks, pings):
    scenarios, aggregate_tasks = [], []
    for s in SCENARIOS:
        rr = [r for r in runs if r['cenario'] == s]
        tt = [t for t in tasks if t['cenario'] == s]
        pp = [p['rtt_ms'] for p in pings if p['cenario'] == s]
        hard = [t for t in tt if t['tipo'] == 'HARD']
        soft = [t for t in tt if t['tipo'] == 'SOFT']
        row = dict(cenario=s, repeticoes=5, duracao_total_s=300,
            hard_misses=sum(t['misses'] for t in hard), hard_perdidas=sum(t['perdidas'] for t in hard),
            hard_invalidas=sum(t['invalidas'] for t in hard), hard_total=sum(r['hard'] for r in rr),
            soft_misses=sum(t['misses'] for t in soft), soft_perdidas=sum(t['perdidas'] for t in soft),
            soft_total=sum(r['soft'] for r in rr),
            ping_n=len(pp), ping_media_ms=st.mean(pp), ping_mediana_ms=st.median(pp),
            ping_p95_ms=percentile(pp,.95), ping_p99_ms=percentile(pp,.99), ping_max_ms=max(pp),
            ping_dp_medias_repeticoes_ms=st.stdev(r['ping_media_ms'] for r in rr),
            ping_min_media_repeticao_ms=min(r['ping_media_ms'] for r in rr),
            ping_max_media_repeticao_ms=max(r['ping_media_ms'] for r in rr),
            descartes_log_observados=sum(r['descartes_log_observados'] for r in rr),
            descartes_tx_observados=sum(r['descartes_tx_observados'] for r in rr),
            rssi_inicio_min_dbm=min(r['rssi_inicio_dbm'] for r in rr),
            rssi_inicio_max_dbm=max(r['rssi_inicio_dbm'] for r in rr))
        assert row['hard_total'] == row['hard_misses']+row['hard_perdidas']+row['hard_invalidas']
        scenarios.append(row)
        for task in TASKS:
            v = [t for t in tt if t['tarefa']==task]
            jobs = sum(t['jobs'] for t in v)
            lost, invalid = sum(t['perdidas'] for t in v), sum(t['invalidas'] for t in v)
            misses = sum(t['misses'] for t in v)
            soft_task = v[0]['tipo'] == 'SOFT'
            aggregate_tasks.append(dict(cenario=s, tarefa=task, deadline_ms=DEADLINES[task],
                jobs=jobs, misses=misses, perdidas=lost, invalidas=invalid,
                miss_pct_jobs=100*misses/jobs,
                falha_pct_ativacoes=100*(misses+lost+invalid)/(jobs+lost+invalid),
                wcrt_max_observado_ms=max(t['wcrt_ms'] for t in v),
                hwm99_menor_limite_ms=min(t['hwm99_inf_ms'] for t in v),
                hwm99_maior_limite_ms=max(t['hwm99_sup_ms'] for t in v),
                c_decorrido_media_ponderada_ms=sum(t['c_decorrido_media_ms']*t['jobs'] for t in v)/jobs,
                c_decorrido_max_ms=max(t['c_decorrido_max_ms'] for t in v),
                cpu_media_das_repeticoes_pct=st.mean(t['cpu_media_janelas_pct'] for t in v),
                cpu_max_janela_pct=max(t['cpu_max_janela_pct'] for t in v),
                jitter_max_ms=max(t['jitter_max_ms'] for t in v),
                isr_latencia_max_ms=max((t['isr_latencia_max_ms'] for t in v if t['isr_latencia_max_ms'] is not None), default=None),
                isr_acao_segura_max_ms=max((t['isr_acao_segura_max_ms'] for t in v if t['isr_acao_segura_max_ms'] is not None), default=None),
                acao_segura_max_ms=max((t['acao_segura_max_ms'] for t in v if t['acao_segura_max_ms'] is not None), default=None),
                mk_janelas_soma=sum(t['mk_janelas'] for t in v) if soft_task else None,
                mk_violacoes_soma=sum(t['mk_violacoes'] for t in v) if soft_task else None))
    return scenarios, aggregate_tasks


def figures(output, runs, tasks, pings, scenarios, aggregated):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams.update({'font.size':10, 'axes.spines.top':False, 'axes.spines.right':False,
                         'savefig.dpi':180, 'font.family':'DejaVu Sans'})
    def save(fig, name):
        fig.savefig(output / f'{name}.png', bbox_inches='tight')
        fig.savefig(output / f'{name}.pdf', bbox_inches='tight')
        plt.close(fig)
    fig, ax = plt.subplots(figsize=(9,4.6))
    x = list(range(4))
    miss = [r['hard_misses'] for r in scenarios]
    lost = [r['hard_perdidas'] for r in scenarios]
    ax.bar(x, miss, label='Violações de prazo hard', color='#277DA8')
    ax.bar(x, lost, bottom=miss, label='Ativações hard perdidas', color='#E9A23B')
    for i,r in enumerate(scenarios): ax.text(i,r['hard_total']+25,str(r['hard_total']),ha='center')
    ax.set(xticks=x,xticklabels=SCENARIOS,ylabel='Ocorrências em cinco repetições de 60 s',title='Falhas hard: violações e perdas são contadas separadamente')
    ax.legend(); save(fig,'01_falhas_hard')
    fig, axs = plt.subplots(3,2,figsize=(11,10))
    for ax,task in zip(axs.flat,TASKS):
        values=[next(t['wcrt_max_observado_ms'] for t in aggregated if t['cenario']==s and t['tarefa']==task) for s in SCENARIOS]
        ax.bar(SCENARIOS,values,color=COLORS)
        ax.axhline(DEADLINES[task],color='black',linestyle='--',label=f'D = {DEADLINES[task]} ms')
        for i,v in enumerate(values): ax.text(i,v,f'{v:.2f}',ha='center',va='bottom',fontsize=8)
        ax.set(title=task,ylabel='Resposta máxima observada (ms)'); ax.legend(fontsize=8)
    fig.suptitle('WCRT / HWM(100%): máximo entre cinco repetições',y=1.01)
    fig.tight_layout(); save(fig,'02_wcrt_deadlines')
    fig,ax=plt.subplots(figsize=(9,4.8))
    for s,c in zip(SCENARIOS,COLORS):
        v=sorted(p['rtt_ms'] for p in pings if p['cenario']==s)
        ax.step(v,[(i+1)/len(v) for i in range(len(v))],where='post',label=s,color=c)
    ax.set(xlabel='RTT de PING na aplicação (ms)',ylabel='Fração acumulada',title='Distribuição empírica: 145 PINGs por cenário (5 × 29)')
    ax.legend(); ax.grid(alpha=.2); save(fig,'03_ping_ecdf')
    fig,ax=plt.subplots(figsize=(9,4.5))
    for i,(s,c) in enumerate(zip(SCENARIOS,COLORS)):
        vals=[r['ping_media_ms'] for r in runs if r['cenario']==s]
        ax.scatter([i+(j-2)*.04 for j in range(5)],vals,color=c,s=45)
        ax.plot([i-.23,i+.23],[st.mean(vals)]*2,color='black',linewidth=2)
    ax.set(xticks=x,xticklabels=SCENARIOS,ylabel='RTT médio de PING por repetição (ms)',title='Variação entre repetições; traço preto = média das cinco médias')
    ax.grid(axis='y',alpha=.2); save(fig,'04_ping_repeticoes')
    fig,ax=plt.subplots(figsize=(10,5))
    bottom=[0.0]*4
    for task in TASKS:
        values=[next(t['cpu_media_das_repeticoes_pct'] for t in aggregated if t['cenario']==s and t['tarefa']==task) for s in SCENARIOS]
        ax.bar(SCENARIOS,values,bottom=bottom,label=task)
        bottom=[a+b for a,b in zip(bottom,values)]
    ax.set(ylabel='CPU média das tarefas instrumentadas (%)',title='Média das janelas completas, agregada por repetição',ylim=(0,100))
    ax.legend(loc='upper left',bbox_to_anchor=(1,1)); save(fig,'05_cpu_aplicacao')
    fig,axs=plt.subplots(1,2,figsize=(11,4.5))
    for ax,task in zip(axs,('NAV_PLAN','FAIL_SAFE')):
        vals=[next(t['isr_latencia_max_ms'] for t in aggregated if t['cenario']==s and t['tarefa']==task) for s in SCENARIOS]
        ax.bar(SCENARIOS,vals,color=COLORS)
        for i,v in enumerate(vals): ax.text(i,v,f'{v:.3f}',ha='center',va='bottom',fontsize=8)
        ax.set(title=task,ylabel='Máximo ISR GPTimer → início (ms)')
    fig.suptitle('Latência da ISR; não inclui detecção de toque físico',y=1.02)
    fig.tight_layout(); save(fig,'06_latencia_isr')
    fig,ax=plt.subplots(figsize=(9,4.6))
    for k,task in enumerate(('NAV_PLAN','MONITOR_TASK','TIME_TASK')):
        vals=[next(t for t in aggregated if t['cenario']==s and t['tarefa']==task) for s in SCENARIOS]
        rates=[100*t['mk_violacoes_soma']/t['mk_janelas_soma'] for t in vals]
        ax.bar([i+(k-1)*.24 for i in x],rates,width=.24,label=task)
    ax.set(xticks=x,xticklabels=SCENARIOS,ylabel='Janelas (9,10) violadas (%)',title='Janelas calculadas dentro de cada repetição, sem unir reinícios',ylim=(0,110))
    ax.legend(); save(fig,'07_mk_firm')
    fig,ax=plt.subplots(figsize=(9,4.5))
    ax.bar([i-.18 for i in x],[s['descartes_log_observados'] for s in scenarios],width=.36,label='Descartes de log')
    ax.bar([i+.18 for i in x],[s['descartes_tx_observados'] for s in scenarios],width=.36,label='Descartes de transmissão')
    ax.set(xticks=x,xticklabels=SCENARIOS,ylabel='Soma dos deltas observados (5 repetições)',title='Pressão de observabilidade — não é taxa de perda no rádio')
    ax.legend(); save(fig,'08_descartes_observados')
    fig,ax=plt.subplots(figsize=(9,4.5))
    fs=[next(t for t in aggregated if t['cenario']==s and t['tarefa']=='FAIL_SAFE') for s in SCENARIOS]
    ax.bar([i-.18 for i in x],[r['acao_segura_max_ms'] for r in fs],width=.36,label='Evento → motores zerados')
    ax.bar([i+.18 for i in x],[r['wcrt_max_observado_ms'] for r in fs],width=.36,label='Evento → fim da tarefa')
    ax.axhline(10,color='black',linestyle='--',label='Deadline de conclusão = 10 ms')
    ax.set(xticks=x,xticklabels=SCENARIOS,ylabel='Máximo observado (ms)',title='FAIL_SAFE: ação segura e conclusão são instantes diferentes')
    ax.legend(fontsize=9); save(fig,'09_fail_safe_acao_conclusao')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dados',type=Path,default=ROOT/'ensaios_M2/02_coletas/AGENDA_60S_R2')
    parser.add_argument('--saida',type=Path,default=ROOT/'analise_final')
    parser.add_argument('--sem-graficos',action='store_true',help='Gera somente tabelas e resumo; dispensa matplotlib.')
    args=parser.parse_args()
    tables=args.saida/'tabelas'; plots=args.saida/'figuras'
    tables.mkdir(parents=True,exist_ok=True); plots.mkdir(parents=True,exist_ok=True)
    runs,tasks,pings,clocks,packets,provenance=load_runs(args.dados)
    scenarios,aggregated=aggregate(runs,tasks,pings)
    for name,rows in [('repeticoes',runs),('tarefas_por_repeticao',tasks),('ping_amostras',pings),
                      ('sntp_por_repeticao',clocks),('timestamps_comunicacao',packets),
                      ('cenarios',scenarios),('tarefas_por_cenario',aggregated),('integridade_dados',provenance)]:
        write_csv(tables/f'{name}.csv',rows)
    (args.saida/'resultados.json').write_text(json.dumps(dict(campaign='AGENDA_60S_R2',scenarios=scenarios,
        tasks=aggregated,runs=runs),indent=2,ensure_ascii=False),encoding='utf-8')
    content=['# Tabelas consolidadas da campanha AGENDA_60S_R2','',
        'Cinco repetições de 60 s por cenário. Valores derivados dos acumuladores locais e dos logs do PC.', '',
        '## Comparação por cenário','',table(scenarios,[('cenario','Cenário'),('hard_misses','Hard: violações'),
        ('hard_perdidas','Hard: perdas'),('hard_total','Hard total'),('soft_misses','Soft: violações'),
        ('ping_media_ms','PING médio (ms)'),('ping_p99_ms','PING p99 (ms)'),('ping_max_ms','PING máximo (ms)')]),'',
        'PING: 145 amostras por cenário. Percentis de RTT usam nearest-rank sobre as amostras dos cinco ensaios; '
        'essas amostras não são 145 repetições independentes. Dispersão entre as cinco execuções em cenarios.csv.', '',
        '## Resposta, execução e cumprimento de prazos por tarefa','',
        table(aggregated,[('cenario','Cenário'),('tarefa','Tarefa'),('deadline_ms','D (ms)'),
             ('wcrt_max_observado_ms','WCRT (ms)'),('c_decorrido_max_ms','C decorrido máx. (ms)'),
             ('misses','Misses'),('perdidas','Perdas'),('miss_pct_jobs','Misses / jobs (%)')]),'',
        'WCRT/HWM100 é o maior valor observado. C decorrido inclui eventuais preempções e esperas; não é WCET formal.', '',
        '## HWM99 por execução','',
        'Não foi calculado um HWM99 global a partir dos percentis individuais. Os limites exatos reportados por '
        'cada histograma estão em tarefas_por_repeticao.csv. Os campos menor/maior limite de tarefas_por_cenario.csv '
        'descrevem apenas o envelope dos cinco intervalos, não um percentil agregado.', '',
        '## Evidência de integridade','',
        'integridade_dados.csv registra SHA256 dos arquivos usados na geração. Caso a publicação remova dados pessoais '
        'de logs, regenerar este inventário sobre as cópias publicadas; os arquivos locais originais permanecem preservados.','']
    (args.saida/'TABELAS.md').write_text('\n'.join(content),encoding='utf-8')
    if not args.sem_graficos:
        figures(plots,runs,tasks,pings,scenarios,aggregated)
    print(f'Gerados: {len(runs)} ensaios, {len(tasks)} linhas tarefa/ensaio, {len(pings)} PINGs, {len(packets)} ACKs correlacionados.')
    print('Saida:',args.saida)


if __name__=='__main__':
    main()

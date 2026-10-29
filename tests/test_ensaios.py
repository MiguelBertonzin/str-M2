import importlib.util
import json
from pathlib import Path
import struct
import sys
import unittest
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pc'), str(ROOT / 'tools')]
import atualizar_indice
import relogio_sntp as ntp
from configurar_teste import apply_config, SCENARIOS
from executar_ensaio import schedule, validate_config, prepare_folder, validate_endpoint
from agenda import (send_schedule, collect_responses, expected_status, REVISION, CAMPAIGN,
                    FIRMWARE_REVISION, RUN_SECONDS, SNTP_INTERVAL_S)
from analisar_ensaio import summarize_stats, cpu_windows, percentile, analyze, check_agenda, sntp_updates_in_window


class ExperimentTests(unittest.TestCase):
    def test_sntp_nao_conta_sincronizacoes_durante_coleta_final(self):
        text=('TIME_SYNC | atualizacoes=1 | mono_us=1000000\n'
              'TIME_SYNC | atualizacoes=2 | mono_us=21000000\n'
              'TIME_SYNC | atualizacoes=2 | mono_us=21000000\n'
              'TIME_SYNC | atualizacoes=3 | mono_us=41000000\n'
              'TIME_SYNC | atualizacoes=4 | mono_us=61000000\n'
              'TIME_SYNC | atualizacoes=5 | mono_us=81000000\n')
        self.assertEqual(sntp_updates_in_window(text,2000000,62000000),3)
        self.assertEqual(sntp_updates_in_window(text,2000000,61000000),2)

    def test_indice_separa_legado_e_campanha_com_mesmo_id(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            (root/'01_planejamento').mkdir()
            (root/'01_planejamento/MATRIZ.csv').write_text(
                'id;cenario;repeticao;estado\nM_UDP_R01;M_UDP;R01;AGUARDANDO\n',encoding='utf-8')
            for campaign,path in [('LEGADO','02_coletas/M_UDP/R01'),(CAMPAIGN,f'02_coletas/{CAMPAIGN}/M_UDP/R01')]:
                folder=root/path; folder.mkdir(parents=True)
                (folder/'manifesto.json').write_text(json.dumps(dict(id='M_UDP_R01',scenario='M_UDP',
                    repetition='R01',status='FALHOU' if campaign=='LEGADO' else 'COLETADO',campaign=campaign)))
            with patch.object(atualizar_indice,'ROOT',root): atualizar_indice.update()
            import csv
            with (root/'INDICE.csv').open() as stream: rows=list(csv.DictReader(stream,delimiter=';'))
            self.assertEqual(len(rows),2)
            self.assertEqual({r['campanha']:r['estado'] for r in rows},{'LEGADO':'FALHOU',CAMPAIGN:'COLETADO'})

    def test_ip_automatico_e_diagnostico_de_firmware_nao_gravado(self):
        endpoint = ('10.20.30.28', 'UDP', 5000)
        self.assertEqual(validate_endpoint(endpoint, 'auto', 'M_UDP'), endpoint[0])
        with self.assertRaisesRegex(ValueError, 'IP da placa mudou'):
            validate_endpoint(endpoint, '10.1.2.28', 'M_UDP')
        with self.assertRaisesRegex(ValueError, 'flash'):
            validate_endpoint(endpoint, 'auto', 'M_TCP')

    def test_agenda_nao_acumula_espera_por_ack_nem_compensa_horarios_perdidos(self):
        class Clock:
            now = 100.0
            def read(self): return self.now
            def sleep(self, seconds): self.now += seconds
        class Client:
            failure = None
            def __init__(self, clock): self.clock, self.sent = clock, []
            def start_command(self, command):
                self.sent.append((self.clock.now, command))
                # Primeiro send bloqueia por 1.5 s; a segunda janela e perdida.
                start = self.clock.now
                if len(self.sent) == 1: self.clock.now += 1.5
                return dict(command=command, seq=len(self.sent), session=1, attempts=1,
                            pc_tx_mono_ns=int(start*1e9), send_return_mono_ns=int(self.clock.now*1e9))
            def finish_command(self, *args):
                raise AssertionError('O emissor nao pode esperar respostas')
        clock = Clock(); client = Client(clock)
        with tempfile.TemporaryDirectory() as directory:
            actions = send_schedule(client, [(1., 'PING'), (2., 'NAV'), (3., 'FS')], 100.,
                                    Path(directory), clock=clock.read, sleep=clock.sleep)
        self.assertEqual(client.sent, [(101., 'PING'), (103., 'FS')])
        self.assertIn('skipped', actions[1])
        self.assertEqual(actions[2]['lateness_ms'], 0)

    def test_analise_detecta_nav_recusada_e_carga_diferente(self):
        events = schedule(); session=123; begin=1000000
        metadata = dict(collector_revision=REVISION, touch=False,
                        agenda_policy=dict(send_attempts=1, ack_timeout_s=5., send_tolerance_ms=100.),
                        begin_ack=dict(ack=dict(session=session, rx_us=begin)))
        actions=[]; records=[]
        for seq, (offset, command) in enumerate(events, 3):
            status=expected_status(command, offset)
            ack=dict(session=session, seq=seq, command=command, status=status,
                     rx_us=begin+int(offset*1e6))
            actions.append(dict(command=command, planned_s=offset, actual_s=offset,
                                lateness_ms=0, request=dict(seq=seq, attempts=1),
                                result=dict(ack=ack, round_trip_ms=500, ack_late=False)))
            records.append(dict(type='pc_send', session=session, seq=seq, command=command))
            if status=='QUEUED':
                records.append(dict(message=dict(type='job', session=session, command_seq=seq,
                                                 task='NAV_PLAN' if command=='NAV' else 'FAIL_SAFE')))
        stats=[dict(task='NAV_PLAN', jobs=22, isr_jobs=13, run_start_us=begin, run_end_us=begin+RUN_SECONDS*1000000),
               dict(task='FAIL_SAFE', jobs=4, isr_jobs=1)]
        flags, result=check_agenda(metadata, actions, records, stats)
        self.assertEqual(flags, [])
        self.assertTrue(result['offered_schedule_ok'])
        records.append(next(r.copy() for r in records if r.get('type')=='pc_send'))
        flags,result=check_agenda(metadata, actions, records, stats)
        self.assertTrue(any('unica tentativa' in f for f in flags))
        self.assertFalse(result['offered_schedule_ok'])
        records.pop()
        nav=next(a for a in actions if a['command']=='NAV')
        nav['result']['ack']['status']='SAFE_ACTIVE'
        stats[0]['jobs']=21
        flags,result=check_agenda(metadata, actions, records, stats)
        self.assertTrue(any('fase esperada' in f for f in flags))
        self.assertTrue(any('carga planejada' in f for f in flags))
        # Atraso da rede pode mudar a carga aceita mesmo com emissor pontual.
        self.assertTrue(result['offered_schedule_ok'])

    def test_reutilizacao_exige_opcao_e_preserva_tentativa_anterior(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = root / 'ensaios_M2/02_coletas/M_TCP/R01_T03'
            self.assertIsNone(prepare_folder(folder, root))
            evidence = b'log original\x00\xff'
            (folder / 'serial.txt').write_bytes(evidence)
            with self.assertRaises(FileExistsError):
                prepare_folder(folder, root)
            self.assertEqual((folder / 'serial.txt').read_bytes(), evidence)
            archived = prepare_folder(folder, root, overwrite=True)
            self.assertEqual((archived / 'serial.txt').read_bytes(), evidence)
            self.assertEqual(list(folder.iterdir()), [])
            with self.assertRaises(ValueError):
                prepare_folder(root / 'fora_das_coletas', root, overwrite=True)

    def test_coleta_incompleta_gera_resumo_revisar_sem_inventar_resultados(self):
        (ROOT / 'tests/.build').mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=ROOT / 'tests/.build') as temporary:
            folder = Path(temporary)
            metadata = dict(id='FIXTURE_TESTE', status='COLETADO', touch=False,
                            esp_time_initial={'sync_count': 1}, esp_time_final={'sync_count': 4})
            stats = []
            for name, jobs in [('FUS_IMU', 36000), ('CTRL_ATT', 36000), ('NAV_PLAN', 24),
                               ('FAIL_SAFE', 16), ('MONITOR_TASK', 360), ('TIME_TASK', 180)]:
                stats.append(dict(task=name, report_seq=7, run_start_us=1000000, run_end_us=181000000,
                    jobs=jobs, misses=0, lost=0, invalid=0, exec_sum_us=jobs*100,
                    exec_max_us=100, wcrt_us=200, p99_low_us=200, p99_high_us=299,
                    latency_max_us=100, safe_max_us=150, windows=max(0, jobs-9), violations=0))
            (folder / 'manifesto.json').write_text(json.dumps(metadata), encoding='utf-8')
            (folder / 'estatisticas.json').write_text(json.dumps(stats), encoding='utf-8')
            (folder / 'serial.txt').write_text('MEASUREMENT_ORIGIN_US=0\nGUIA | sequencia concluida\n', encoding='utf-8')
            (folder / 'comandos.jsonl').write_text('', encoding='utf-8')
            (folder / 'rede.jsonl').write_text('', encoding='utf-8')
            result = analyze(folder)
            self.assertEqual(result['status'], 'REVISAR')
            self.assertFalse(result['all_deadlines_observed'])
            self.assertTrue(any('Agenda incompleta' in flag for flag in result['flags']))
            self.assertEqual(len((folder / 'tarefas.csv').read_text().splitlines()), 7)
            self.assertTrue((folder / 'RESUMO.md').exists())

    def test_preserva_credenciais_e_substitui_escolhas_mutuamente_exclusivas(self):
        text = 'CONFIG_M2_WIFI_SSID="rede teste"\nCONFIG_M2_WIFI_PASSWORD="teste senha"\nCONFIG_M2_MONITOR_TCP=y\nCONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_80=y\n'
        changed = apply_config(text, 'M_UDP')
        self.assertIn('CONFIG_M2_WIFI_PASSWORD="teste senha"', changed)
        self.assertIn('# CONFIG_M2_MONITOR_TCP is not set', changed)
        self.assertIn('# CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_80 is not set', changed)
        self.assertIn('CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y', changed)
        self.assertIn('CONFIG_M2_RUN_SECONDS=60', changed)
        self.assertIn('CONFIG_M2_SNTP_INTERVAL_S=20', changed)
        self.assertEqual(changed, apply_config(changed, 'M_UDP'))

    def test_nao_aceita_cenario_incorreto(self):
        config = dict(SCENARIOS['M_UDP'], revision=FIRMWARE_REVISION, input='AUTO',
                      duration_s=RUN_SECONDS, sample_n=100, time_enabled=True, run_start_us=0,
                      auto_wait_pc=True, tick_hz=1000, sntp_interval_s=SNTP_INTERVAL_S)
        validate_config(config, 'M_UDP', False, False)
        with self.assertRaises(ValueError): validate_config(config, 'P_UDP', False, False)
        config['duration_s'] = 180
        with self.assertRaises(ValueError): validate_config(config, 'M_UDP', False, False)
        config['duration_s'] = RUN_SECONDS
        config['sntp_interval_s'] = 60
        with self.assertRaises(ValueError): validate_config(config, 'M_UDP', False, False)
        config['sntp_interval_s'] = SNTP_INTERVAL_S
        config['run_start_us'] = 1
        with self.assertRaises(ValueError): validate_config(config, 'M_UDP', False, False)

    def test_roteiro_nao_envia_fs_antes_do_d_e_verifica_nav_bloqueada(self):
        events = schedule()
        self.assertEqual(events, sorted(events))
        self.assertTrue(all(t > 24 for t, cmd in events if cmd == 'FS'))
        self.assertIn((27.0, 'NAV'), events)
        self.assertEqual(sum(cmd == 'NAV' and t < 24 for t, cmd in events), 9)
        self.assertEqual(len(events), 42)
        self.assertEqual(sum(cmd=='PING' for _,cmd in events),29)
        self.assertEqual(sum(cmd=='FS' for _,cmd in events),3)
        self.assertEqual(min(t for t,cmd in events if cmd=='NAV'), 5)
        self.assertTrue(all(0 <= t < RUN_SECONDS for t, _ in events))

    def test_sntp_offset_delay_e_resposta_correlacionada(self):
        t1 = 1793000000 * 10**9
        origin = ntp.ntp_stamp(t1)
        data = bytearray(48)
        data[0], data[1] = 0x24, 2
        data[24:32] = origin
        # Servidor 1 s adiantado; rede simetrica 20 ms e processamento 5 ms.
        data[32:40] = ntp.ntp_stamp(t1 + 1_020_000_000)
        data[40:48] = ntp.ntp_stamp(t1 + 1_025_000_000)
        result = ntp.parse_reply(data, origin, t1, t1 + 45_000_000, 45_000_000)
        self.assertAlmostEqual(result['offset_ns'], 1_000_000_000, delta=2)
        self.assertAlmostEqual(result['network_delay_ns'], 40_000_000, delta=2)
        with self.assertRaises(ValueError): ntp.parse_reply(data, b'badstamp', t1, t1+45_000_000, 45_000_000)
        data[1] = 0
        with self.assertRaises(ValueError): ntp.parse_reply(data, origin, t1, t1+45_000_000, 45_000_000)

    def test_era_ntp_apos_2036(self):
        value = 2200000000123456789
        self.assertAlmostEqual(ntp.unix_ns(ntp.ntp_stamp(value), value), value, delta=2)

    def test_mk_nao_afirma_sucesso_se_faltam_eventos(self):
        sample = dict(task='NAV_PLAN', jobs=10, misses=0, lost=1, invalid=0,
                      exec_sum_us=10000, exec_max_us=1000, wcrt_us=1500,
                      p99_low_us=1500, p99_high_us=1599, latency_max_us=500,
                      safe_max_us=0, windows=1, violations=0)
        result = summarize_stats([sample])[0]
        self.assertIn('INCONCLUSIVO', result['mk_status'])
        self.assertEqual(result['p99_high_ms'], 1.5)
        sample.update(lost=0, windows=0)
        self.assertEqual(summarize_stats([sample])[0]['mk_status'], 'SEM JANELA COMPLETA')

    def test_percentil_por_rank_e_sem_amostras(self):
        self.assertEqual(percentile(list(range(1, 101)), .99), 99)
        self.assertIsNone(percentile([], .99))


if __name__ == '__main__': unittest.main()

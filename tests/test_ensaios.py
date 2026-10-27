import importlib.util
import json
from pathlib import Path
import struct
import sys
import unittest
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pc'), str(ROOT / 'tools')]
import relogio_sntp as ntp
from configurar_teste import apply_config, SCENARIOS
from executar_ensaio import schedule, validate_config
from analisar_ensaio import summarize_stats, cpu_windows, percentile, analyze


class ExperimentTests(unittest.TestCase):
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
        self.assertEqual(changed, apply_config(changed, 'M_UDP'))

    def test_nao_aceita_cenario_incorreto(self):
        config = dict(SCENARIOS['M_UDP'], revision='M2-METRICS-2', input='AUTO',
                      duration_s=180, sample_n=100, time_enabled=True, run_start_us=0,
                      auto_wait_pc=True, tick_hz=1000, sntp_interval_s=60)
        validate_config(config, 'M_UDP', False, False)
        with self.assertRaises(ValueError): validate_config(config, 'P_UDP', False, False)
        config['run_start_us'] = 1
        with self.assertRaises(ValueError): validate_config(config, 'M_UDP', False, False)

    def test_roteiro_nao_envia_fs_antes_do_d_e_verifica_nav_bloqueada(self):
        events = schedule()
        self.assertEqual(events, sorted(events))
        self.assertTrue(all(t > 24 for t, cmd in events if cmd == 'FS'))
        self.assertIn((27.0, 'NAV'), events)
        self.assertEqual(sum(cmd == 'NAV' and t < 24 for t, cmd in events), 11)
        self.assertTrue(all(0 <= t < 180 for t, _ in events))

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

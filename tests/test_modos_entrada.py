"""CLI de apresentacao: fonte de eventos explicita, sem misturar coletas fisicas."""
import contextlib
import io
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'pc'), str(ROOT / 'tools')]
import configurar_teste
import executar_ensaio


class InputModeTests(unittest.TestCase):
    def test_configurador_default_automatico_e_alias_fisico(self):
        parser = configurar_teste.build_parser()
        for option in ([], ['--automatizado']):
            args = parser.parse_args(['--cenario', 'M_UDP'] + option)
            self.assertFalse(args.touch)
            config = configurar_teste.apply_config('', args.cenario, args.touch)
            self.assertIn('CONFIG_M2_INPUT_AUTO=y', config)
            self.assertIn('CONFIG_M2_AUTO_WAIT_PC=y', config)
        for option in ('--fisico', '--touch'):
            args = parser.parse_args(['--cenario', 'M_UDP', option])
            self.assertTrue(args.touch)
            config = configurar_teste.apply_config('', args.cenario, args.touch)
            self.assertIn('CONFIG_M2_INPUT_TOUCH=y', config)
            self.assertIn('# CONFIG_M2_INPUT_AUTO is not set', config)

    def test_cli_rejeita_fontes_conflitantes(self):
        for parser, base in (
            (configurar_teste.build_parser(), ['--cenario', 'M_UDP']),
            (executar_ensaio.build_parser(), ['--cenario', 'M_UDP', '--repeticao', 'R01', '--serial', 'COM7']),
        ):
            with self.subTest(parser=parser.prog), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    parser.parse_args(base + ['--fisico', '--automatizado'])

    def test_coletor_mantem_bateria_automatica_e_isola_fisico_em_txx(self):
        base = ['--cenario', 'M_UDP', '--serial', 'COM7']
        for option in ([], ['--automatizado']):
            self.assertFalse(executar_ensaio.parse_args(base + ['--repeticao', 'R01'] + option).touch)
        for option in ('--fisico', '--touch'):
            self.assertTrue(executar_ensaio.parse_args(base + ['--repeticao', 'T01', option]).touch)
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                executar_ensaio.parse_args(base + ['--repeticao', 'R01', option])
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            executar_ensaio.parse_args(base + ['--repeticao', 'T01', '--automatizado'])

    def test_coletor_rejeita_placa_com_fonte_diferente(self):
        from agenda import FIRMWARE_REVISION, RUN_SECONDS, SNTP_INTERVAL_S
        config = dict(configurar_teste.SCENARIOS['M_UDP'], revision=FIRMWARE_REVISION,
                      input='AUTO', duration_s=RUN_SECONDS, sample_n=100,
                      time_enabled=True, auto_wait_pc=True, tick_hz=1000,
                      sntp_interval_s=SNTP_INTERVAL_S, run_start_us=0)
        executar_ensaio.validate_config(config, 'M_UDP', False, False)
        with self.assertRaisesRegex(ValueError, 'Firmware diferente'):
            executar_ensaio.validate_config(config, 'M_UDP', True, False)
        config.update(input='TOUCH', auto_wait_pc=False)
        executar_ensaio.validate_config(config, 'M_UDP', True, False)
        with self.assertRaisesRegex(ValueError, 'Firmware diferente'):
            executar_ensaio.validate_config(config, 'M_UDP', False, False)


if __name__ == '__main__':
    unittest.main()

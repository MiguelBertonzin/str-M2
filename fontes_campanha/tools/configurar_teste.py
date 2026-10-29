"""Aplica apenas variaveis do ensaio ao sdkconfig local; preserva SSID/senha."""
import argparse
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'pc'))
from agenda import RUN_SECONDS, SNTP_INTERVAL_S

SCENARIOS = {
    'M_UDP': dict(policy=1, cpu_mhz=240, preemption=1, slicing=1, protocol='UDP'),
    'M_TCP': dict(policy=1, cpu_mhz=240, preemption=1, slicing=1, protocol='TCP'),
    'P_UDP': dict(policy=2, cpu_mhz=80, preemption=0, slicing=1, protocol='UDP'),
    'P_TCP': dict(policy=2, cpu_mhz=80, preemption=0, slicing=1, protocol='TCP'),
}


def apply_config(text, scenario, touch=False, time_enabled=True):
    c = SCENARIOS[scenario]
    values = {
        'M2_PRIORITY_POLICY': c['policy'], 'M2_PREEMPTION': bool(c['preemption']),
        'M2_TIME_SLICING': True, 'FREERTOS_UNICORE': True, 'FREERTOS_HZ': 1000,
        'FREERTOS_USE_TRACE_FACILITY': True, 'FREERTOS_GENERATE_RUN_TIME_STATS': True,
        'FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER': True,
        'M2_INPUT_AUTO': not touch, 'M2_INPUT_TOUCH': touch, 'M2_INPUT_PC': False,
        'M2_AUTO_WAIT_PC': not touch, 'M2_MONITOR_ENABLE': True, 'M2_TIME_ENABLE': time_enabled,
        'M2_MONITOR_UDP': c['protocol'] == 'UDP', 'M2_MONITOR_TCP': c['protocol'] == 'TCP',
        'M2_MONITOR_PORT': 5000, 'M2_MONITOR_SAMPLE_N': 100, 'M2_RUN_SECONDS': RUN_SECONDS,
        'M2_SNTP_INTERVAL_S': SNTP_INTERVAL_S, 'ESP_DEFAULT_CPU_FREQ_MHZ': c['cpu_mhz'],
    }
    for freq in (80, 160, 240): values[f'ESP_DEFAULT_CPU_FREQ_MHZ_{freq}'] = freq == c['cpu_mhz']
    for key, value in values.items():
        name = 'CONFIG_' + key
        line = (f'{name}=y' if value else f'# {name} is not set') if isinstance(value, bool) else f'{name}={value}'
        pattern = rf'^(?:{name}=.*|# {name} is not set)$'
        text, count = re.subn(pattern, line, text, flags=re.MULTILINE)
        if count == 0: text += '\n' + line + '\n'
    return text


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cenario', required=True, choices=SCENARIOS)
    p.add_argument('--touch', action='store_true')
    p.add_argument('--sem-time', action='store_true', help='Somente diagnostico D01; nao bateria final')
    args = p.parse_args()
    path = Path(__file__).resolve().parents[1] / 'sdkconfig'
    if not path.exists(): raise SystemExit('Crie sdkconfig com idf.py menuconfig primeiro.')
    text = path.read_text(encoding='utf-8')
    path.write_text(apply_config(text, args.cenario, args.touch, not args.sem_time), encoding='utf-8')
    print(f'Configurado {args.cenario}: {SCENARIOS[args.cenario]}, entrada={"TOUCH" if args.touch else "AUTO"}, TIME={not args.sem_time}.')
    print('Agora execute: idf.py -p COM7 flash (ajuste COM7 se necessario).')
    print('As credenciais Wi-Fi existentes foram preservadas.')
    print(f'Janela: {RUN_SECONDS} s; intervalo SNTP: {SNTP_INTERVAL_S} s.')


if __name__ == '__main__': main()

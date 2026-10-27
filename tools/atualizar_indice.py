"""Atualiza o indice de arquivos coletados sem alterar o planejamento ou logs."""
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / 'ensaios_M2'


def update():
    with (ROOT / '01_planejamento/MATRIZ.csv').open(encoding='utf-8-sig', newline='') as f:
        planned = {row['id']: row for row in csv.DictReader(f, delimiter=';')}
    for path in sorted((ROOT / '02_coletas').glob('*/*/manifesto.json')):
        metadata = json.loads(path.read_text(encoding='utf-8-sig'))
        record = planned.setdefault(metadata['id'], dict(id=metadata['id'], cenario=metadata['scenario'], repeticao=metadata['repetition']))
        record['estado'] = metadata['status']
        record['pasta'] = path.parent.relative_to(ROOT).as_posix()
        summary = path.parent / 'resumo.json'
        if summary.exists():
            result = json.loads(summary.read_text(encoding='utf-8'))
            record['conferencia'] = result['status']
            record['falhas_hard'] = result['hard_failures']
            record['falhas_soft'] = result['soft_failures']
    fields = ['id','cenario','repeticao','tipo','entrada','time','duracao_s','estado','conferencia','falhas_hard','falhas_soft','pasta']
    with (ROOT / 'INDICE.csv').open('w', encoding='utf-8', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=fields, delimiter=';')
        writer.writeheader(); writer.writerows(planned.values())
    print('Indice atualizado:', ROOT / 'INDICE.csv')


if __name__ == '__main__': update()

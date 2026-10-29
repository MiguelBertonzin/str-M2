"""Recalcula a campanha em copias temporarias e confere fontes sem alterar coletas."""
import contextlib
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.analisar_ensaio import analyze


def main():
    base = ROOT / 'ensaios_M2/02_coletas/AGENDA_60S_R2'
    rows = []
    for scenario in ('M_UDP', 'M_TCP', 'P_UDP', 'P_TCP'):
        for rep in range(1, 6):
            folder = base / scenario / f'R{rep:02d}'
            manifest = json.loads((folder / 'manifesto.json').read_text(encoding='utf-8-sig'))
            saved = json.loads((folder / 'resumo.json').read_text(encoding='utf-8-sig'))
            with tempfile.TemporaryDirectory(prefix='m2_audit_') as temp:
                copy = Path(temp) / 'coleta'
                shutil.copytree(folder, copy)
                with contextlib.redirect_stdout(io.StringIO()):
                    recalculated = analyze(copy)
            changed = [name for name, digest in manifest['source_hashes'].items()
                       if not (ROOT / name).is_file() or
                       hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest]
            rows.append(dict(
                id=manifest['id'], recomputation_identical=(saved == recalculated),
                status=recalculated['status'], flags=recalculated['flags'],
                sources_different_from_current=changed,
                config=manifest['config'], created_utc=manifest['created_utc'],
                hard=recalculated['hard_failures'], soft=recalculated['soft_failures'],
                tasks=recalculated['tasks'], network=recalculated['network'],
                source_evidence_sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in folder.iterdir() if p.is_file()},
            ))
    output = ROOT / 'ensaios_M2/04_relatorio/AUDITORIA_COLETAS.json'
    output.write_text(json.dumps(dict(campaign='AGENDA_60S_R2', runs=rows),
                                 ensure_ascii=False, indent=2), encoding='utf-8')
    for row in rows:
        n = row['network']
        print(row['id'], row['status'], 'recalculo=', row['recomputation_identical'],
              'fontes_diferentes=', row['sources_different_from_current'],
              'descartes_log/tx=', n['log_dropped_delta_observed'], n['tx_dropped_delta_observed'])
    print('Auditoria:', output)


if __name__ == '__main__':
    main()

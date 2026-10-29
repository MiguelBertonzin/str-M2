"""Preserva anexos recebidos pelo chat, sem sobrescrever, com origem e SHA256."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil


def archive(source, test_id, name):
    if not re.fullmatch(r'[A-Za-z0-9_-]+', test_id): raise ValueError('ID invalido')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+', name) or name in ('.', '..'): raise ValueError('Nome invalido')
    root = Path(__file__).resolve().parents[1] / 'ensaios_M2/03_anexos_chat' / test_id
    root.mkdir(parents=True, exist_ok=True)
    dest = root / name
    source = source.resolve(strict=True)
    manifest = root / 'origens.json'
    entries = json.loads(manifest.read_text(encoding='utf-8')) if manifest.exists() else []
    data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if dest.exists():
        if hashlib.sha256(dest.read_bytes()).hexdigest() == digest: return dest
        raise FileExistsError(dest)
    with dest.open('xb') as output: output.write(data)
    entries.append(dict(file=name, source=str(source), bytes=len(data), sha256=digest,
                        classification='anexo do usuario; nao promover automaticamente a ensaio final'))
    manifest.write_text(json.dumps(entries, indent=2, ensure_ascii=False), encoding='utf-8')
    return dest


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--origem', type=Path, required=True)
    p.add_argument('--id', required=True)
    p.add_argument('--nome', required=True)
    a = p.parse_args()
    print(archive(a.origem, a.id, a.nome))

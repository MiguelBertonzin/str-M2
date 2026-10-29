"""Prepara o checkout de publicacao com lista explicita de materiais finais."""
from pathlib import Path
import hashlib
import json
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / '.publication'


def main():
    if not (DEST / '.git').is_dir():
        raise SystemExit('Checkout .publication com .git necessario.')
    entries = {}
    def add(source, relative=None):
        if not source.is_file():
            raise FileNotFoundError(source)
        entries[relative or source.relative_to(ROOT).as_posix()] = source
    def tree(path, prefix=None):
        for p in sorted(path.rglob('*')):
            if p.is_file() and not any(x in {'.git', '__pycache__', '.build'} for x in p.parts):
                add(p, f'{prefix}/{p.relative_to(path).as_posix()}' if prefix else None)

    for name in ('.gitignore', '.gitattributes', 'README.md', 'ROTEIRO_RAPIDO_TESTES.txt',
                 'CMakeLists.txt', 'sdkconfig.defaults', 'sdkconfig.wifi.example', 'Trabalho M2 - STR .pdf'):
        add(ROOT / name)
    for name in ('main', 'cmake', 'configs', 'pc', 'tools', 'tests', 'analise_final'):
        tree(ROOT / name)
    for name in ('ARQUITETURA_FINAL.md', 'METODOLOGIA_FINAL.md', 'MODOS_ENTRADA.md',
                 'APRESENTACAO.md', 'PUBLICACAO.md'):
        add(ROOT / 'docs' / name)
    add(ROOT / 'ensaios_M2/01_planejamento/MATRIZ.csv')
    add(ROOT / 'ensaios_M2/README.md')
    add(ROOT / 'evidencias_complementares/README.md')
    for scenario in ('M_UDP', 'M_TCP', 'P_UDP', 'P_TCP'):
        for rep in range(1, 6):
            tree(ROOT / f'ensaios_M2/02_coletas/AGENDA_60S_R2/{scenario}/R{rep:02d}')
    archives = ROOT / 'ensaios_M2/03_anexos_chat/tentativas_substituidas'
    for scenario, hard in (('P_UDP', 423), ('P_TCP', 634)):
        candidates = [p for p in archives.glob(f'{scenario}_R01_*')
                      if (p / 'resumo.json').exists() and
                      json.loads((p / 'resumo.json').read_text(encoding='utf-8'))['hard_failures'] == hard]
        if len(candidates) != 1:
            raise RuntimeError('Evidencia adicional ambigua: ' + scenario)
        tree(candidates[0], f'evidencias_complementares/{scenario}_R01_primeira')

    measured = json.loads((ROOT / 'ensaios_M2/02_coletas/AGENDA_60S_R2/M_TCP/R01/manifesto.json').read_text(encoding='utf-8'))
    for name, digest in measured['source_hashes'].items():
        original = ROOT / 'tests/.build/fontes_medidas' / name
        source = original if original.exists() else ROOT / name
        if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise RuntimeError('Fonte medida indisponivel: ' + name)
        add(source, 'fontes_campanha/' + name)

    for name in ('referencias/m1/tabelas_graficos/dados/resumo_cenarios.csv',
                 'referencias/m1/tabelas_graficos/dados/metricas_finais_por_tarefa.csv'):
        add(ROOT / name)
    tree(ROOT / 'referencias/professor')

    secrets = {}
    for config in (ROOT / 'sdkconfig', ROOT / 'sdkconfig.old'):
        if config.exists():
            for key, quoted in re.findall(r'^CONFIG_M2_WIFI_(SSID|PASSWORD)=(".*")$', config.read_text(encoding='utf-8'), re.M):
                value = json.loads(quoted)
                if value:
                    secrets[value] = 'SUA_REDE_2_4_GHZ' if key == 'SSID' else 'SUA_SENHA'
    # SSIDs de sessoes anteriores podem ser diferentes da configuracao atual.
    for source in entries.values():
        if source.name == 'serial.txt':
            for ssid in re.findall(r'wifi:connected with (.*?), aid', source.read_text(encoding='utf-8', errors='replace')):
                if ssid:
                    secrets[ssid] = 'REDE_DE_TESTE'

    generated = {}
    records = []
    for name, source in sorted(entries.items()):
        original = source.read_bytes()
        data = original
        for secret, placeholder in secrets.items():
            data = data.replace(secret.encode(), placeholder.encode())
            data = data.replace(json.dumps(secret)[1:-1].encode(), placeholder.encode())
        for secret in secrets:
            if secret.encode() in data:
                raise RuntimeError('Credencial remanescente: ' + name)
        generated[name] = data
        records.append(dict(path=name, sha256=hashlib.sha256(data).hexdigest(),
                            original_sha256=hashlib.sha256(original).hexdigest(), sanitized=data != original))
    # Verifica todos os destinos antes de remover arquivos antigos do checkout.
    dest_root = DEST.resolve()
    if dest_root != (ROOT.resolve() / '.publication'):
        raise RuntimeError('Destino fora do checkout esperado.')
    existing = [p for p in DEST.rglob('*') if p.is_file() and '.git' not in p.relative_to(DEST).parts]
    for path in existing:
        if path.is_symlink() or not path.resolve().is_relative_to(dest_root):
            raise RuntimeError('Destino inseguro: ' + str(path))
    for path in existing:
        if path.relative_to(DEST).as_posix() not in generated:
            path.unlink()
    for name, data in generated.items():
        target = DEST / name
        if not target.resolve().is_relative_to(dest_root):
            raise RuntimeError('Destino invalido.')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    (DEST / 'PUBLICACAO_MANIFESTO.json').write_text(json.dumps(dict(
        note='Material final. Hash original e hash publicado distinguem sanitizacao de credenciais; originais preservados localmente.',
        files=records), ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(dict(files=len(records), sanitized=sum(r['sanitized'] for r in records),
                          bytes=sum(len(v) for v in generated.values()))))


if __name__ == '__main__':
    main()

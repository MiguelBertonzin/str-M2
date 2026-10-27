"""Conferencias somente de leitura: importacao, contrato do roteiro e builds.

Nao substitui ensaio em ESP32. Biblioteca padrao Python, fora do firmware C.
"""

import hashlib
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def verificar_importacao():
    pasta = ROOT / "referencias/m1"
    manifesto = json.loads((pasta / "IMPORTACAO.json").read_text(encoding="utf-8"))
    for item in manifesto["arquivos"]:
        copia = pasta / item["arquivo"]
        assert hashlib.sha256(copia.read_bytes()).hexdigest() == item["sha256"], copia
        original = Path(manifesto["origem"]) / item["arquivo"]
        if original.is_file():
            assert hashlib.sha256(original.read_bytes()).hexdigest() == item["sha256"], original
    assert len(list((pasta / "Validos").glob("*.txt"))) == 18
    print(f'OK: {len(manifesto["arquivos"])} referencias integras e 18 logs M1.')


def verificar_roteiro():
    texto = (ROOT / "main/event_script.c").read_text(encoding="utf-8")
    entradas = re.findall(r"\{\s*(\d+)ULL,\s*([^}]+)\}", texto)
    assert len(entradas) == 13, "Esperados 13 alarmes para 14 eventos"
    ordem = []
    ultimo_canal = {}
    anterior = 0
    simultaneos = []
    for tempo, mascara in entradas:
        tempo = int(tempo)
        assert tempo > anterior, "Agenda deve ser estritamente crescente"
        anterior = tempo
        canais = re.findall(r"INPUT_([ABCD])_MASK", mascara)
        assert canais and len(set(canais)) == len(canais)
        assert re.fullmatch(r"\s*INPUT_[ABCD]_MASK(?:\s*\|\s*INPUT_[ABCD]_MASK)*\s*", mascara)
        if len(canais) > 1:
            simultaneos.append((tempo, set(canais)))
        # Contrato do despachante: emergencia primeiro; depois B, C e A.
        for canal in "DBCA":
            if canal not in canais:
                continue
            if canal in ultimo_canal:
                assert tempo - ultimo_canal[canal] >= 1_200_000, "Intervalo menor que o debounce M1"
            ultimo_canal[canal] = tempo
            ordem.append(canal)
    assert "".join(ordem) == "CBCABCBCBCBCDC", ordem
    assert {c: ordem.count(c) for c in "ABCD"} == {"A": 1, "B": 5, "C": 7, "D": 1}
    assert simultaneos == [(22_000_000, {"B", "C"})]
    assert ultimo_canal["D"] == 24_000_000 and anterior == 26_000_000
    print("OK: roteiro M1 preservado: 12 eventos B/C, 1 A, 1 FS, B+C juntos. M2 instrumenta tambem o job NAV de A.")


def verificar_builds():
    cenarios = {
        "build": (1, 1, 240, "AUTO"),
        "build_pior": (0, 2, 80, "AUTO"),
        "build_touch": (1, 1, 240, "TOUCH"),
    }
    for nome, (preempcao, politica, mhz, fonte) in cenarios.items():
        pasta = ROOT / nome
        banco = pasta / "compile_commands.json"
        if not banco.exists():
            print(f"PENDENTE: {nome} ainda nao configurado.")
            continue
        config = (pasta / "config/sdkconfig.h").read_text(encoding="utf-8")
        for macro, valor in {
            "CONFIG_M2_PRIORITY_POLICY": politica,
            "CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ": mhz,
            "CONFIG_FREERTOS_UNICORE": 1,
            "CONFIG_FREERTOS_HZ": 1000,
            f"CONFIG_M2_INPUT_{fonte}": 1,
        }.items():
            assert re.search(rf"^#define {macro} {valor}$", config, re.M), (nome, macro)
        overlay = (pasta / "m2_freertos_config/freertos/FreeRTOSConfig.h").read_text(encoding="utf-8")
        for macro, valor in {"configUSE_PREEMPTION": preempcao, "configUSE_TIME_SLICING": 1}.items():
            assert re.search(rf"^#define\s+{macro}\s+{valor}\s*$", overlay, re.M), (nome, macro)
        comandos = json.loads(banco.read_text(encoding="utf-8"))
        for arquivo in ("main.c", "tasks.c", "queue.c"):
            comando = next(c["command"] for c in comandos if Path(c["file"]).name == arquivo)
            assert comando.index("m2_freertos_config") < comando.index("components/freertos/config/include"), (nome, arquivo)
        firmware = pasta / "STR_M2_Drone.bin"
        assert firmware.is_file(), f"{nome}: configurado, mas firmware ainda nao gerado"
        print(f"OK: {nome}: preemption={preempcao}, politica={politica}, {mhz} MHz, {fonte}; kernel e app com override local.")


if __name__ == "__main__":
    verificar_importacao()
    verificar_roteiro()
    verificar_builds()

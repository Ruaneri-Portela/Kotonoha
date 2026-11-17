#!/usr/bin/env python3
"""Create bounded synthetic FlgH GlobalFlag data in a marked build/ QA profile.

Wire format follows SchoolDaysPersistenceCodec.cpp:42-109, 155-171, 230-276:
signed varints, length-prefixed UTF-16LE XOR code units, FlgH entries. This
tool never reads scripts, GPKs or an existing personal game profile.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import tempfile

MARKER = b"school-days-synthetic-qa-profile-v1\n"
MAX_ENDINGS = 22
MAX_FLAG_BYTES = 32 * 1024 * 1024
# Functional unlock metadata only; there are no commercial scripts or texts.
SYNTHETIC_REPLAY_KEY = "REP02_2S_W03"


def fail(message: str) -> None:
    raise ValueError(message)


def varint(value: int) -> bytes:
    """Mirror SchoolDaysPersistenceCodec::WriteVarint exactly."""
    if not -(1 << 31) <= value < (1 << 31):
        fail("int32 fora do intervalo")
    magnitude = ~value if value < 0 else value
    sign = 0x40 if value < 0 else 0
    for groups in range(1, 6):
        if magnitude < (1 << (6 + 7 * (groups - 1))):
            break
    if groups == 1:
        return bytes([magnitude | sign])
    encoded = [((magnitude >> (7 * (groups - 1))) & 0x3F) | sign | 0x80]
    for group in range(groups - 2, -1, -1):
        encoded.append(
            ((magnitude >> (7 * group)) & 0x7F) | (0x80 if group else 0)
        )
    return bytes(encoded)


def xor_string(value: str) -> bytes:
    raw = value.encode("utf-16-le")
    units = len(raw) // 2
    if units >= 0x400:
        fail("chave excede capacidade do codec FlgH")
    encoded = bytearray(varint(units))
    for i in range(units):
        original = int.from_bytes(raw[2 * i : 2 * i + 2], "little")
        encoded.extend((original ^ i).to_bytes(2, "little"))
    return bytes(encoded)


def payload(completed: int, *, all_clear: bool, end_no: int,
            replay: bool, route_map: bool) -> tuple[bytes, dict[str, int]]:
    if not 0 <= completed <= MAX_ENDINGS:
        fail("completed deve estar entre 0 e 22")
    if not -1 <= end_no <= 99:
        fail("end-no sintético deve estar entre -1 e 99")
    flags = [
        ("EndClear", 11, -1 if completed else 0),
        ("EndNo", 3, end_no),
        ("AllClear", 11, -1 if all_clear else 0),
    ]
    flags.extend((f'[End{index:02d}]="', 11, -1)
                 for index in range(completed))
    if replay:
        flags.append((SYNTHETIC_REPLAY_KEY, 11, -1))
    if route_map:
        flags.append(("SP100", 11, -1))
    encoded = bytearray(b"FlgH")
    encoded.extend(varint(len(flags)))
    for key, kind, value in flags:
        encoded.extend(xor_string(key))
        encoded.extend(varint(-1))  # FlagsHash identifier
        encoded.extend(varint(kind))
        encoded.extend(varint(value))
    if len(encoded) > MAX_FLAG_BYTES:
        fail("GlobalFlag excedeu limite de 32 MiB")
    return bytes(encoded), {key: value for key, _, value in flags}


def decode_flags(data: bytes) -> dict[str, int]:
    """Bounded independent roundtrip check; matches the C++ FlgH entry layout."""
    if not 5 <= len(data) <= MAX_FLAG_BYTES or data[:4] != b"FlgH":
        fail("assinatura ou tamanho FlgH inválido")
    offset = 4

    def read_varint() -> int:
        nonlocal offset
        if offset >= len(data):
            fail("varint truncado")
        current = data[offset]
        offset += 1
        negative = bool(current & 0x40)
        magnitude = current & 0x3F
        for _ in range(4):
            if not current & 0x80:
                return ~magnitude if negative else magnitude
            if offset >= len(data):
                fail("varint truncado")
            current = data[offset]
            offset += 1
            magnitude = (magnitude << 7) | (current & 0x7F)
        if current & 0x80:
            fail("varint longo demais")
        return ~magnitude if negative else magnitude

    def read_name() -> str:
        nonlocal offset
        count = read_varint()
        if not 0 <= count < 0x400 or offset + 2 * count > len(data):
            fail("string truncada ou fora de capacidade")
        raw = bytearray()
        for index in range(count):
            unit = int.from_bytes(
                data[offset + 2 * index : offset + 2 * index + 2], "little"
            )
            raw.extend((unit ^ index).to_bytes(2, "little"))
        offset += 2 * count
        return raw.decode("utf-16-le")

    count = read_varint()
    if not 0 <= count <= 200_000:
        fail("contagem de entradas FlgH inválida")
    found: dict[str, int] = {}
    for _ in range(count):
        key = read_name()
        read_varint()  # identifier
        kind = read_varint()
        if kind not in (3, 11):
            fail("tipo de entrada inesperado em perfil sintético")
        found[key] = read_varint()
    if offset != len(data):
        fail("bytes extras em FlgH")
    return found


def contained(path: Path, base: Path) -> bool:
    try:
        path.relative_to(base)
    except ValueError:
        return False
    return True


def guarded_root(given: Path) -> Path:
    """Allow writes only under this checkout's build/, never in real profiles."""
    root_build = (Path(__file__).resolve().parents[3] / "build").resolve()
    if not root_build.is_dir():
        fail("a pasta build/ do repositório não existe")
    supplied = given.expanduser().absolute()
    resolved = supplied.resolve()
    if not contained(resolved, root_build) or resolved == root_build:
        fail("somente subpastas de build/ deste checkout são aceitas")
    if not contained(supplied, root_build):
        fail("caminho atravessa uma pasta externa ou link simbólico")
    probe = supplied
    while probe != root_build:
        if probe.is_symlink():
            fail("caminho com link simbólico recusado")
        if probe.parent == probe:
            fail("caminho não pertence a build/")
        probe = probe.parent
    return resolved


def require_marker(root: Path, initialize: bool) -> None:
    marker = root / ".qa-profile"
    if initialize:
        if root.exists() and (not root.is_dir() or any(root.iterdir())):
            fail("--init exige diretório inexistente ou completamente vazio")
        root.mkdir(parents=True, exist_ok=True)
        with marker.open("xb") as stream:
            stream.write(MARKER)
        return
    if (not root.is_dir() or marker.is_symlink() or not marker.is_file()
            or marker.read_bytes() != MARKER):
        fail("perfil recusado: falta .qa-profile com marcador sintético válido")


def atomic_publish(target: Path, encoded: bytes, replace: bool) -> None:
    parent = target.parent
    if parent.is_symlink() or (parent.exists() and not parent.is_dir()):
        fail("Save/ inválido ou link simbólico")
    parent.mkdir(exist_ok=True)
    if target.is_symlink() or (target.exists() and not target.is_file()):
        fail("GlobalFlag.DAT inválido ou link simbólico")
    if target.exists() and not replace:
        fail("GlobalFlag.DAT já existe; forneça --replace explicitamente")
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb", prefix=".globalflag-synthetic-", suffix=".pending",
            dir=parent, delete=False
        ) as stream:
            temporary = Path(stream.name)
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        # Exclusive installation when --replace is absent avoids races with a
        # second QA writer; replace mode is explicitly opt-in.
        if replace:
            os.replace(temporary, target)
        else:
            with target.open("xb") as stream:
                stream.write(encoded)
                stream.flush()
                os.fsync(stream.fileno())
        if target.read_bytes() != encoded:
            fail("bytes de GlobalFlag divergem após gravação")
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--documents", type=Path, help="Perfil sintético em build/")
    parser.add_argument("--init", action="store_true",
                        help="Cria marcador apenas em diretório vazio")
    parser.add_argument("--completed", type=int, default=21,
                        help="Número de finais marcados (0..22; padrão 21)")
    parser.add_argument("--all-clear", action="store_true",
                        help="Define explicitamente AllClear, para QA de idempotência")
    parser.add_argument("--end-no", type=int,
                        help="EndNo (padrão: último final ou zero)")
    parser.add_argument("--unlock-replay", action="store_true",
                        help="Libera exatamente uma H-scene sintética por chave")
    parser.add_argument("--unlock-route-map", action="store_true",
                        help="Define SP100 para testar persistência de unlock")
    parser.add_argument("--corrupt", choices=("magic", "truncate"),
                        help="Cria entrada inválida para QA de falha no boot")
    parser.add_argument("--replace", action="store_true",
                        help="Autoriza substituir GlobalFlag.DAT já existente")
    parser.add_argument("--dry-run", action="store_true",
                        help="Valida e imprime resumo sem alterar arquivos")
    parser.add_argument("--self-test", action="store_true",
                        help="Verifica wire format sem acessar disco")
    args = parser.parse_args()

    if args.self_test:
        samples = [(0, False, 0), (21, False, 20), (22, True, 21)]
        for count, all_clear, end_no in samples:
            encoded, expected = payload(
                count, all_clear=all_clear, end_no=end_no,
                replay=True, route_map=True
            )
            if decode_flags(encoded) != expected:
                fail("teste de codec sintético falhou")
        if varint(-1) != b"\x40" or varint(64) != b"\x80\x40":
            fail("varint incompatível com codec C++")
        print("PASS: FlgH sintético, varint e XOR UTF-16LE")
        return 0

    if args.documents is None:
        parser.error("--documents é obrigatório fora de --self-test")
    end_no = args.end_no
    if end_no is None:
        end_no = max(0, args.completed - 1)
    encoded, expected = payload(
        args.completed, all_clear=args.all_clear, end_no=end_no,
        replay=args.unlock_replay, route_map=args.unlock_route_map
    )
    if decode_flags(encoded) != expected:
        fail("verificação independente de FlgH falhou")
    if args.corrupt == "magic":
        encoded = b"BadH" + encoded[4:]
    elif args.corrupt == "truncate":
        encoded = encoded[:-1]

    root = guarded_root(args.documents)
    destination = root / "Save" / "GlobalFlag.DAT"
    print("Destino QA:", destination)
    print("Finalizados:", args.completed, "/ 22 | AllClear:",
          int(args.all_clear), "| EndNo:", end_no)
    print("Bytes:", len(encoded), "| SHA-256:",
          hashlib.sha256(encoded).hexdigest())
    if args.dry_run:
        print("DRY RUN: nenhum arquivo foi criado ou alterado")
        return 0
    require_marker(root, args.init)
    atomic_publish(destination, encoded, args.replace)
    print("Perfil sintético gravado; EndBGView/EndingMax vêm do título, não do FlgH")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, UnicodeError) as exc:
        raise SystemExit(f"ERRO: {exc}")

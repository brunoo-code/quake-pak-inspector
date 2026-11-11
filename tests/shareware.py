"""Reproduce the integration check using an independently supplied Quake 1.06 PAK."""

import argparse
import hashlib
from pathlib import Path
import struct
import subprocess


def verify(executable: Path, archive: Path) -> None:
    data = archive.read_bytes()
    expected_hash = "35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af"
    if hashlib.sha256(data).hexdigest() != expected_hash:
        raise SystemExit("SHA-256 mismatch: expected Quake 1.06 shareware ID1/PAK0.PAK")

    magic, offset, length = struct.unpack_from("<4sII", data)
    if (magic, offset, length) != (b"PACK", 18254423, 21696):
        raise SystemExit("Unexpected PAK directory")
    expected = {}
    for name, start, size in struct.iter_unpack("<56sII", data[offset : offset + length]):
        expected[name.split(b"\0", 1)[0].decode("ascii")] = (start, size)

    def run(*args: str | Path) -> str:
        return subprocess.check_output([executable.resolve(), *args], text=True)

    lines = run("list", archive).splitlines()
    actual = {}
    for line in lines[1:]:
        start, size, name = line.split("\t")
        actual[name] = (int(start), int(size))
    if lines[0] != "offset\tbytes\tpath" or len(lines) != 340 or actual != expected:
        raise SystemExit("Inspector index differs from the independent directory reader")

    target = "maps/e1m1.bsp"
    start, size = expected[target]
    preview = run("inspect", archive, target).splitlines()
    expected_preview = [f"{target} | offset {start} | {size} bytes | preview 128 bytes"]
    for position in range(start, start + 128, 16):
        chunk = data[position : position + 16]
        printable = "".join(chr(byte) if 32 <= byte <= 126 else "." for byte in chunk)
        expected_preview.append(f"{position:08x}  {chunk.hex(' ')}  |{printable}|")
    if preview != expected_preview or run("diff", archive, archive):
        raise SystemExit("Inspector preview or archive self-diff failed")

    print(f"PASS: {len(actual)} entries, 128 preview bytes, empty self-diff")
    print(f"{target}: offset {start}, {size} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    verify(args.executable, args.archive)

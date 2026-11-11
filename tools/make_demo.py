"""Build reproducible example archives with original text payloads."""

from pathlib import Path
import struct
import sys


def pack(entries):
    payload = bytearray()
    directory = bytearray()
    for name, data in entries:
        encoded = name.encode("ascii")
        if not 0 < len(encoded) < 56:
            raise ValueError(f"Invalid fixture path: {name}")
        directory.extend(struct.pack("<56sII", encoded, 12 + len(payload), len(data)))
        payload.extend(data)
    return struct.pack("<4sII", b"PACK", 12 + len(payload), len(directory)) + payload + directory


def make_demo(output):
    output.mkdir(parents=True, exist_ok=True)
    base = [
        ("config/arena.cfg", b"timelimit 10\n"),
        ("docs/readme.txt", b"Example arena, original training data.\n"),
        ("docs/old.txt", b"Old release notes.\n"),
    ]
    patch = [
        ("config/arena.cfg", b"timelimit 15\n"),
        ("docs/changelog.txt", b"Raised the time limit to 15 minutes.\n"),
    ]
    for name, entries in (
        ("base.pak", base),
        ("patch.pak", patch),
        ("repacked.pak", [*patch, base[1]]),
    ):
        (output / name).write_bytes(pack(entries))


if __name__ == "__main__":
    make_demo(Path(sys.argv[1]) if len(sys.argv) > 1 else Path("examples"))

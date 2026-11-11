from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from make_demo import make_demo, pack


exe = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    make_demo(root)

    def run(*args, code=0):
        result = subprocess.run([exe, *args], cwd=root, capture_output=True, text=True)
        assert result.returncode == code, (args, result.returncode, result.stderr)
        return result.stdout if code == 0 else result.stderr

    assert "12\t13\tconfig/arena.cfg" in run("list", "base.pak")
    assert "0000000c" in run("inspect", "base.pak", "config/arena.cfg")
    assert "timelimit 10" in run("inspect", "base.pak", "config/arena.cfg")
    assert run("diff", "base.pak", "repacked.pak").splitlines() == [
        "CHANGED\tconfig/arena.cfg",
        "REMOVED\tdocs/old.txt",
        "ADDED\tdocs/changelog.txt",
    ]
    assert run("overlay", "config/arena.cfg", "base.pak", "patch.pak").splitlines() == [
        "SHADOWED\tbase.pak", "ACTIVE\tpatch.pak",
    ]
    assert "not found" in run("overlay", "missing", "base.pak", code=1)

    # A late byte differs even though the entry has the same length.
    (root / "left.pak").write_bytes(pack([("x", b"a" * 70000)]))
    (root / "right.pak").write_bytes(pack([("x", b"a" * 69999 + b"b")]))
    assert run("diff", "left.pak", "right.pak") == "CHANGED\tx\n"

    valid = bytearray(pack([("x", b"123")]))
    cases = {
        "short.pak": valid[:9],
        "magic.pak": b"NOPE" + valid[4:],
        "overflow.pak": valid[:4] + struct.pack("<I", 0x7fffffff) + valid[8:],
        "partial.pak": valid[:8] + struct.pack("<I", 63) + valid[12:],
        "overlap.pak": valid[:-8] + struct.pack("<II", 15, 2),
        "outside.pak": valid[:-8] + struct.pack("<II", len(valid), 1),
        "unterminated.pak": valid[:15] + b"x" * 56 + valid[-8:],
        "duplicate.pak": pack([("x", b"1"), ("x", b"2")]),
    }
    for name, contents in cases.items():
        (root / name).write_bytes(contents)
        assert run("list", name, code=1).startswith("error:")

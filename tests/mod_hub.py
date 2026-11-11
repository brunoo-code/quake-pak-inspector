from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from make_demo import pack

exe, rules = map(lambda arg: Path(arg).resolve(), sys.argv[1:3])
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    (root / "base.pak").write_bytes(pack([("config/arena.cfg", b"10"), ("docs/same.txt", b"a")]))
    (root / "patch.pak").write_bytes(pack([("config/arena.cfg", b"15"), ("docs/same.txt", b"a")]))
    (root / "mod.pak").write_bytes(pack([("config/arena.cfg", b"15"), ("maps/new.bsp", b"new")]))

    def audit(script):
        return subprocess.run([exe, script, "base.pak", "patch.pak", "mod.pak"],
                              cwd=root, capture_output=True, text=True)

    result = audit(rules)
    assert result.returncode == 0, result.stderr
    assert result.stdout.splitlines() == [
        "config\tOVERRIDE\tconfig/arena.cfg\tbase.pak -> mod.pak",
        "identical\tDUPLICATE\tconfig/arena.cfg\tpatch.pak -> mod.pak",
        "identical\tDUPLICATE\tdocs/same.txt\tbase.pak -> patch.pak",
        "3 indexed paths; 2 with multiple providers",
    ]

    bad = root / "loop.lua"
    bad.write_text("return function() while true do end end", encoding="ascii")
    result = audit(bad)
    assert result.returncode == 1 and "instruction limit exceeded" in result.stderr

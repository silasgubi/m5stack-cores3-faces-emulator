# PlatformIO pre-script: makes the embedded files optional.
# board_build.embed_files needs every listed file to exist, and objcopy rejects empty
# files, so a missing (or empty) data/game.nes, data/game.gb or data/logo.png becomes a
# single zero byte. The firmware treats anything that small as "not present": the menu
# hides the ROM slot and the boot screen falls back to text.
Import("env")  # noqa: F821 (provided by SCons)

from pathlib import Path

data = Path(env.subst("$PROJECT_DIR")) / "data"  # noqa: F821
for name in ("game.nes", "game.gb", "logo.png"):
    path = data / name
    if not path.exists() or path.stat().st_size == 0:
        data.mkdir(exist_ok=True)
        path.write_bytes(b"\0")
        print(f"rom_placeholders: data/{name} missing, created a 1-byte placeholder")

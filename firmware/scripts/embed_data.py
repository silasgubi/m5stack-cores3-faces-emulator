# PlatformIO pre-script: prepares the files embedded in the firmware (board_build.embed_files).
#
# data/roms/*.nes, *.gb, *.gbc -> data/roms.bin, a pack read by src/rom_source.cpp:
#   "ROMP", u32 count, then count entries of {char name[64], u32 offset, u32 size}
#   (little endian, offsets from the start of the pack), then the ROMs, each 4-byte aligned.
# The file name (without folder) is the menu name and also names the save file. With no
# ROMs the pack is just the header with count 0, so the build never needs a ROM.
#
# data/logo.png is optional: a missing (or empty) one becomes a single zero byte, since
# embed_files needs every listed file to exist and objcopy rejects empty files. The
# firmware treats anything that small as "not present" and shows text instead.
Import("env")  # noqa: F821 (provided by SCons)

import struct
from pathlib import Path

MAGIC = b"ROMP"
NAME_LEN = 64
EXTENSIONS = {".nes", ".gb", ".gbc"}

data = Path(env.subst("$PROJECT_DIR")) / "data"  # noqa: F821
data.mkdir(exist_ok=True)

roms = sorted(
    (p for p in (data / "roms").glob("*") if p.is_file() and p.suffix.lower() in EXTENSIONS),
    key=lambda p: p.name.lower(),
)
entries, blobs = [], []
offset = 8 + len(roms) * (NAME_LEN + 8)
for p in roms:
    name = p.name.encode("ascii", "replace")
    if len(name) >= NAME_LEN:
        raise SystemExit(f"embed_data: name too long (max {NAME_LEN - 1} chars): {p.name}")
    blob = p.read_bytes()
    blob += b"\0" * (-len(blob) % 4)
    entries.append(struct.pack(f"<{NAME_LEN}sII", name, offset, p.stat().st_size))
    blobs.append(blob)
    offset += len(blob)
pack = MAGIC + struct.pack("<I", len(roms)) + b"".join(entries) + b"".join(blobs)

out = data / "roms.bin"
if not out.exists() or out.read_bytes() != pack:  # rewrite only on change: no needless relink
    out.write_bytes(pack)
print(f"embed_data: {len(roms)} ROM(s) in data/roms.bin ({len(pack) // 1024} KB)")

logo = data / "logo.png"
if not logo.exists() or logo.stat().st_size == 0:
    logo.write_bytes(b"\0")
    print("embed_data: data/logo.png missing, created a 1-byte placeholder")

# 3DS template — DO NOT EDIT DIRECTLY

This is the template. For a new project:

```bash
./tools/new-app.sh <app-name> "Title" "Author"
cd project/<app-name>
source ../../tools/env.sh
make        # -> output/<name>.3dsx + .smdh + .elf
make cia    # -> output/<name>.cia (needs makerom+bannertool)
make 3ds    # -> output/<name>.3ds (same)
make clean
```

Layout:

- `source/` C/C++/asm code (entry: `main.c`)
- `include/` private headers
- `data/` binary files embedded via bin2o
- `gfx/` `.t3s` files converted with tex3ds
- `romfs/` RomFS contents of the `.3dsx`
- `resources/`:
  - `AppInfo` metadata (title/author/UniqueID/version)
  - `template.rsf` makerom spec for `.cia/.3ds`
  - `icon.png` 48x48 for smdh/icon.icn
  - `banner.png` + `audio.wav` OPTIONAL, for a custom CIA banner
    (without them bannertool uses the default; see notes/CIA-vs-3DSX.md)

UniqueID rules (`resources/AppInfo`):

- Must be UNIQUE for every app installed as `.cia`.
- Common homebrew range: `0x1B000`–`0x1BFFF`. `new-app.sh` generates one at random.
- Never reuse the same ID for two different apps.

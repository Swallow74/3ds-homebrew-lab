# 3DS development workflow

## 1. Setup (one-off)

```bash
cd 3ds-homebrew-lab
./tools/install-toolchain.sh
# reboot the Mac, then:
source tools/env.sh
```

## 2. New app

```bash
./tools/new-app.sh mygame "My Game" "Your Name"
cd project/mygame
```

## 3. Build

```bash
source ../../tools/env.sh
make        # .3dsx (fast development)
make cia    # HOME Menu installer (needs makerom+bannertool)
make 3ds    # card image (same)
make clean  # cleans build/ output/
```

Output in `output/`:

- `<app>.elf` intermediate (debug with gdb/`3dslink` if needed)
- `<app>.3dsx` + `<app>.smdh`
- `<app>.cia`, `<app>.3ds` (only with makerom)

## 4. Testing

- **Hardware (recommended)**: 3DS with Luma3DS + Homebrew Launcher + FBI
  - 3dsx: copy `output/*.3dsx`, `*.smdh` to `sd:/3ds/<app>/`
  - cia: copy `output/*.cia` to the SD card, install from FBI
- **Emulator**: azahar / lime3ds / legacy citra
  - `citra output/<app>.3dsx`
  - or `make citra` (needs citra in PATH)
- **Network (3dslink)**: with the 3DS on the network, `3dslink output/<app>.3dsx -a <3DS-IP>`

## 5. Typical debugging

| Symptom | Likely cause |
|---|---|
| `DEVKITARM not set` | forgot `source tools/env.sh` |
| `3ds.h not found` | `3ds-dev` not installed or env missing |
| `makerom not found` | normal without manual install → 3dsx only |
| red screen crash (Luma) | ARM11 exception: null address, stack, gfx not init/exit |
| CIA overwrites another app | duplicate UniqueID → change `resources/AppInfo` |
| folder with spaces | devkitPro Makefiles don't support them |

## 6. Resources

- libctru docs: https://libctru.devkitpro.org / https://devkitpro.org/wiki/Getting_Started
- Examples: `sudo dkp-pacman -S 3ds-examples` or https://github.com/devkitPro/3ds-examples
- Forums/support: https://gbatemp.net/forums/nintendo-3ds.201/ , devkitPro forums
- Emulators: azahar (active), lime3ds, citra (legacy)

# 3DS toolchain on macOS (Apple Silicon)

## Components

- `dkp-pacman` → devkitPro package manager (`/opt/devkitpro`)
- `3ds-dev` → devkitARM (gcc arm-none-eabi) + libctru + citro3d/citro2d + 3dstools
  - `3dsxtool` `.elf` → `.3dsx` (Homebrew Launcher)
  - `smdhtool` icon + metadata → `.smdh`
  - `tex3ds`, `picasso` (shaders/gfx), `3dslink`
- `3ds-portlibs` → ported zlib, png, jpeg, freetype, etc.
- `makerom` + `bannertool` → ONLY for `.cia` / `.3ds` (NOT in pacman, manual install)

## Installation (one-off, needs sudo)

```bash
cd 3ds-homebrew-lab
./tools/install-toolchain.sh
# if it asks for Rosetta 2 (x86_64 binaries): softwareupdate --install-rosetta
# reboot the Mac at the end (activates /etc/profile.d/devkit-env.sh)
```

What the script does:

1. Checks Xcode CLT (`xcode-select -p`)
2. Installs `dl/devkitpro-pacman-installer.pkg` (v6.0.2, expected in `dl/`)
3. `sudo dkp-pacman -Syu` + `sudo dkp-pacman -S 3ds-dev 3ds-portlibs 3dstools`
4. Checks `arm-none-eabi-gcc`, `3dsxtool`, `smdhtool`

Later updates: `sudo dkp-pacman -Syu`

## Shell environment

```bash
source tools/env.sh
echo $DEVKITPRO $DEVKITARM   # /opt/devkitpro /opt/devkitpro/devkitARM
```

The template Makefile aborts with a clear error if `DEVKITARM` is empty.

## makerom / bannertool (details in CIA-vs-3DSX.md)

- Sources: makerom (3DSGuy/Project_CTR), bannertool (titler/bannertool)
- Destination: `$DEVKITARM/bin` (already in PATH via env.sh) or `/usr/local/bin`
- Check: `which makerom bannertool`
- On Apple Silicon they may need Rosetta 2.

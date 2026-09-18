# Toolchain 3DS su macOS (Apple Silicon)

## Componenti

- `dkp-pacman` → package manager devkitPro (`/opt/devkitpro`)
- `3ds-dev` → devkitARM (gcc arm-none-eabi) + libctru + citro3d/citro2d + 3dstools
  - `3dsxtool` `.elf` → `.3dsx` (Homebrew Launcher)
  - `smdhtool` icona+metadati → `.smdh`
  - `tex3ds`, `picasso` (shader/gfx), `3dslink`
- `3ds-portlibs` → zlib, png, jpeg, freetype, ecc. portate
- `makerom` + `bannertool` → SOLO per `.cia` / `.3ds` (NON in pacman, installazione manuale)

## Installazione (una tantum, richiede sudo)

```bash
cd "/Volumes/Kingstone/Progetti/3ds-hb"
./tools/install-toolchain.sh
# se chiede Rosetta 2 (binari x86_64): softwareupdate --install-rosetta
# riavvia il Mac alla fine (attiva /etc/profile.d/devkit-env.sh)
```

Cosa fa lo script:

1. Verifica Xcode CLT (`xcode-select -p`)
2. Installa `dl/devkitpro-pacman-installer.pkg` (v6.0.2, già in `dl/`)
3. `sudo dkp-pacman -Syu` + `sudo dkp-pacman -S 3ds-dev 3ds-portlibs 3dstools`
4. Verifica `arm-none-eabi-gcc`, `3dsxtool`, `smdhtool`

Aggiornamenti successivi: `sudo dkp-pacman -Syu`

## Env per shell

```bash
source tools/env.sh
echo $DEVKITPRO $DEVKITARM   # /opt/devkitpro /opt/devkitpro/devkitARM
```

Il Makefile del template abortisce con errore chiaro se `DEVKITARM` è vuoto.

## makerom / bannertool (dettagli in CIA-vs-3DSX.md)

- Sorgenti: makerom (3DSGuy/Project_CTR), bannertool (titler/bannertool)
- Destinazione: `$DEVKITARM/bin` (già in PATH via env.sh) oppure `/usr/local/bin`
- Verifica: `which makerom bannertool`
- Su Apple Silicon possono richiedere Rosetta 2.

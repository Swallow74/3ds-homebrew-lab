# Workflow sviluppo 3DS

## 1. Setup (una tantum)

```bash
cd "/Volumes/Kingstone/Progetti/3ds-hb"
./tools/install-toolchain.sh
# riavvia il Mac, poi:
source tools/env.sh
```

## 2. Nuova app

```bash
./tools/new-app.sh minigioco "Mini Gioco" "Alessandro"
cd project/minigioco
```

## 3. Build

```bash
source ../../tools/env.sh
make        # .3dsx (sviluppo rapido)
make cia    # installer HOME Menu (serve makerom+bannertool)
make 3ds    # immagine scheda (idem)
make clean  # pulisce build/ output/
```

Output in `output/`:

- `<app>.elf` intermedio (debug con gdb/`3dslink` se serve)
- `<app>.3dsx` + `<app>.smdh`
- `<app>.cia`, `<app>.3ds` (solo con makerom)

## 4. Test

- **Hardware (consigliato)**: 3DS con Luma3DS + Homebrew Launcher + FBI
  - 3dsx: copia `output/*.3dsx`, `*.smdh` in `sd:/3ds/<app>/`
  - cia: copia `output/*.cia` sulla SD, installa da FBI
- **Emulatore**: azahar / lime3ds / citra legacy
  - `citra output/<app>.3dsx`
  - oppure `make citra` (richiede citra in PATH)
- **Rete (3dslink)**: con 3DS in rete, `3dslink output/<app>.3dsx -a <IP-3DS>`

## 5. Debug tipico

| Sintomo | Causa probabile |
|---|---|
| `DEVKITARM not set` | dimenticato `source tools/env.sh` |
| `3ds.h not found` | `3ds-dev` non installato o env mancante |
| `makerom not found` | normale senza installazione manuale → solo 3dsx |
| crash schermo rosso (Luma) | exception ARM11: indirizzo nullo, stack, gfx non init/exit |
| CIA sovrascrive altra app | UniqueID duplicato → cambia `resources/AppInfo` |
| cartella con spazi | i Makefile devkitPro non li supportano |

## 6. Risorse

- libctru docs: https://libctru.devkitpro.org / https://devkitpro.org/wiki/Getting_Started
- Esempi: `sudo dkp-pacman -S 3ds-examples` oppure https://github.com/devkitPro/3ds-examples
- Forum/supporto: https://gbatemp.net/forums/nintendo-3ds.201/ , devkitPro forums
- Emulatori: azahar (attivo), lime3ds, citra (legacy)

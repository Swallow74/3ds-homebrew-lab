# 3DS Homebrew — workspace macOS

Cartella pronta per sviluppare app Nintendo 3DS con output `.3dsx` (Homebrew Launcher)
e installer `.cia` / immagine `.3ds`.

```
3ds-hb/
├── project/
│   ├── _template/     ← NON modificare: base per new-app.sh (Makefile 3dsx+cia+3ds)
│   └── hello-3ds/     ← esempio minimo, compila per verificare la toolchain
├── tools/
│   ├── env.sh               ← source tools/env.sh (DEVKITPRO/DEVKITARM/PATH)
│   ├── install-toolchain.sh ← setup una tantum (richiede sudo)
│   └── new-app.sh           ← ./tools/new-app.sh <nome> "Titolo" "Autore"
├── sdk/     ← note versioni, NON il toolchain (/opt/devkitpro)
├── dl/      ← devkitpro-pacman-installer.pkg + prebuild manuali (makerom/...)
├── notes/   ← TOOLCHAIN.md · CIA-vs-3DSX.md · WORKFLOW.md
└── README.md
```

## Avvio rapido

```bash
cd "/Volumes/Kingstone/Progetti/3ds-hb"

# 1. Toolchain (una tantum, chiede password sudo + reboot alla fine)
./tools/install-toolchain.sh

# 2. Prova build
cd project/hello-3ds
source ../../tools/env.sh
make        # output/hello-3ds.3dsx
make cia    # solo con makerom+bannertool (vedi notes/CIA-vs-3DSX.md)

# 3. Nuova app
cd ../..
./tools/new-app.sh minigioco "Mini Gioco" "Alessandro"
```

## Requisiti

- macOS + Xcode Command Line Tools (`xcode-select -p` → `/Applications/Xcode.app/...` OK)
- `sudo` interattivo per installare dkp-pacman e i pacchetti `3ds-dev`
- 3DS con CFW (Luma3DS) + FBI per test `.cia`; oppure azahar/lime3ds/citra per test `.3dsx`
- Mai usare percorsi con spazi nei progetti (i Makefile devkitPro non li supportano)

Dettagli: `notes/TOOLCHAIN.md`, `notes/CIA-vs-3DSX.md`, `notes/WORKFLOW.md`.

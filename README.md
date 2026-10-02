# 3DS Homebrew Lab

Workspace for developing Nintendo 3DS apps and games, with `.3dsx` output (Homebrew Launcher)
and `.cia` installers / `.3ds` card images.

```
3ds-homebrew-lab/
├── project/
│   ├── _template/     ← DO NOT edit: base for new-app.sh (3dsx+cia+3ds Makefile)
│   ├── blockout-3ds/  ← BlockOut 3DS (derived from BlockOut II, GPL)
│   ├── blockfall-3ds/ ← Blockfall 3DS (falling-blocks puzzle)
│   └── runner-3ds/    ← Neon Rush 3DS (3D runner)
├── tools/
│   ├── env.sh               ← source tools/env.sh (DEVKITPRO/DEVKITARM/PATH)
│   ├── install-toolchain.sh ← one-off setup (needs sudo)
│   └── new-app.sh           ← ./tools/new-app.sh <name> "Title" "Author"
├── sdk/     ← version notes, NOT the toolchain (/opt/devkitpro)
├── dl/      ← devkitpro-pacman-installer.pkg + manual prebuilt tools (makerom/...)
├── notes/   ← TOOLCHAIN.md · CIA-vs-3DSX.md · WORKFLOW.md
└── README.md
```

## Quick start

```bash
cd 3ds-homebrew-lab

# 1. Toolchain (one-off, asks for the sudo password + reboot at the end)
./tools/install-toolchain.sh

# 2. Test build
cd project/blockfall-3ds
source ../../tools/env.sh
make        # output/blockfall-3ds.3dsx
make cia    # only with makerom+bannertool (see notes/CIA-vs-3DSX.md)

# 3. New app
cd ../..
./tools/new-app.sh mygame "My Game" "Your Name"
```

## Requirements

- macOS + Xcode Command Line Tools (`xcode-select -p` → `/Applications/Xcode.app/...` is fine)
- interactive `sudo` to install dkp-pacman and the `3ds-dev` packages
- a 3DS with CFW (Luma3DS) + FBI to test `.cia`; or azahar/lime3ds/citra to test `.3dsx`
- Never use paths with spaces in projects (devkitPro Makefiles don't support them)

Details: `notes/TOOLCHAIN.md`, `notes/CIA-vs-3DSX.md`, `notes/WORKFLOW.md`.

## License

All games are released under the **GNU GPL v2 or later** (text in `COPYING`).
BlockOut 3DS is a port loosely inspired by *BlockOut II 2.5* by Jean-Luc Pons (GPL),
available at https://www.blockout.net/blockout2/. "BlockOut" is a registered trademark of
Kadon Enterprises, Inc., used only to identify the original game.

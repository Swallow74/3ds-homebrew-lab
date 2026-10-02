# .3dsx vs .cia vs .3ds — which one to use

| Format | Launched from | Tools | Signing / installation |
|---|---|---|---|
| `.3dsx` (+`.smdh`) | Homebrew Launcher (`sd:/3ds/<app>/`) | `3dsxtool`, `smdhtool` (in 3ds-dev) | No installation, ideal for development/debugging |
| `.cia` | HOME Menu (installed with FBI) | `makerom` + `bannertool` + `template.rsf` | Needs a unique UniqueID; test on a 3DS with CFW (Luma3DS) |
| `.3ds` / `.cci` | Flashcard / Citra | `makerom` + `bannertool` | Like CIA but with `-DAPP_ENCRYPTED=true` |

## Always works (pacman only)

```bash
make        # = make 3dsx: output/<app>.elf + .3dsx + .smdh
```

## Needs makerom + bannertool

```bash
make cia
make 3ds
make release  # .zip (3dsx+smdh) + .cia + .3ds
```

The Makefile looks for `makerom` and `bannertool` in PATH. If they are missing, the error
only affects the cia/3ds targets — the 3dsx target keeps working.

## Manual makerom/bannertool installation (macOS)

1. Get the binaries:
   - makerom: https://github.com/3DSGuy/Project_CTR (`makerom/` folder)
   - bannertool: https://github.com/titler/bannertool (releases: `bannertool.zip`)
   - alternatively, the `buildtools` bundles linked from community templates.
2. Copy them into the toolchain's PATH:
   ```bash
   source tools/env.sh
   cp makerom bannertool "$DEVKITARM/bin/"
   chmod +x "$DEVKITARM/bin/makerom" "$DEVKITARM/bin/bannertool"
   which makerom bannertool
   ```
3. On Apple Silicon, if the binaries are Intel x86_64:
   ```bash
   softwareupdate --install-rosetta
   ```

## CIA banner and icon

- `resources/icon.png` (48x48) → `icon.icn` via `bannertool makesmdh`
- Custom banner (optional):
  - `resources/banner.png` (256x128) + `resources/audio.wav` → `banner.bnr`
  - advanced formats: `banner.cgfx` + `audio.cwav` (instead of png/wav)
- Without a custom banner the Makefile still uses icon + title.

## UniqueID

- `resources/AppInfo` → `APP_UNIQUE_ID` (e.g. `0x1B111`).
- Every installed `.cia` must have a different ID, otherwise it overwrites the other one.
- Safe homebrew range: `0x1B000`–`0x1BFFF`. `tools/new-app.sh` generates one at random.
- `APP_PRODUCT_CODE` is free (`FreeProductCode: true` in the rsf).

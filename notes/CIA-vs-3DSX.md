# .3dsx vs .cia vs .3ds — cosa usare

| Formato | Avvio da | Tool | Firma/Installazione |
|---|---|---|---|
| `.3dsx` (+`.smdh`) | Homebrew Launcher (`sd:/3ds/<app>/`) | `3dsxtool`, `smdhtool` (in 3ds-dev) | Nessuna installazione, ideale per sviluppo/debug |
| `.cia` | HOME Menu (installata con FBI) | `makerom` + `bannertool` + `template.rsf` | Richiede UniqueID univoco; test su 3DS con CFW (Luma3DS) |
| `.3ds` / `.cci` | Flashcard / Citra | `makerom` + `bannertool` | Come CIA ma `-DAPP_ENCRYPTED=true` |

## Sempre funziona (solo pacman)

```bash
make        # = make 3dsx: output/<app>.elf + .3dsx + .smdh
```

## Richiede makerom + bannertool

```bash
make cia
make 3ds
make release  # .zip (3dsx+smdh) + .cia + .3ds
```

Il Makefile cerca `makerom` e `bannertool` in PATH. Se mancano, l'errore è solo
sui target cia/3ds — il target 3dsx continua a funzionare.

## Installazione manuale makerom/bannertool (macOS)

1. Procurati i binari:
   - makerom: https://github.com/3DSGuy/Project_CTR (cartella `makerom/`)
   - bannertool: https://github.com/titler/bannertool (releases: `bannertool.zip`)
   - in alternativa i bundle `buildtools` linkati dai template comunitari.
2. Copiali in PATH della toolchain:
   ```bash
   source tools/env.sh
   cp makerom bannertool "$DEVKITARM/bin/"
   chmod +x "$DEVKITARM/bin/makerom" "$DEVKITARM/bin/bannertool"
   which makerom bannertool
   ```
3. Su Apple Silicon, se sono binari Intel x86_64:
   ```bash
   softwareupdate --install-rosetta
   ```

## Banner e icona CIA

- `resources/icon.png` (48x48) → `icon.icn` via `bannertool makesmdh`
- Banner personalizzato (opzionale):
  - `resources/banner.png` (256x128) + `resources/audio.wav` → `banner.bnr`
  - formati avanzati: `banner.cgfx` + `audio.cwav` (al posto di png/wav)
- Senza banner personalizzato il Makefile usa comunque icona+titolo.

## UniqueID

- `resources/AppInfo` → `APP_UNIQUE_ID` (es. `0x1B111`).
- Ogni `.cia` installata deve avere ID diverso, altrimenti sovrascrive l'altra.
- Range homebrew sicuro: `0x1B000`–`0x1BFFF`. `tools/new-app.sh` lo genera a caso.
- `APP_PRODUCT_CODE` libero (`FreeProductCode: true` nel rsf).

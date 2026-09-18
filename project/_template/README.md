# Template 3DS — NON MODIFICARE DIRETTAMENTE

Questo è il template. Per un nuovo progetto:

```bash
./tools/new-app.sh <nome-app> "Titolo" "Autore"
cd project/<nome-app>
source ../../tools/env.sh
make        # -> output/<nome>.3dsx + .smdh + .elf
make cia    # -> output/<nome>.cia (richiede makerom+bannertool)
make 3ds    # -> output/<nome>.3ds (idem)
make clean
```

Struttura:

- `source/` codice C/C++/asm (entry: `main.c`)
- `include/` header privati
- `data/` file binari inclusi via bin2o
- `gfx/` file `.t3s` convertiti con tex3ds
- `romfs/` contenuto RomFS del `.3dsx`
- `resources/`:
  - `AppInfo` metadati (titolo/autore/UniqueID/versione)
  - `template.rsf` spec makerom per `.cia/.3ds`
  - `icon.png` 48x48 per smdh/icon.icn
  - `banner.png` + `audio.wav` OPZIONALI per banner CIA personalizzato
    (senza: bannertool usa default; vedi notes/CIA-vs-3DSX.md)

Regole UniqueID (`resources/AppInfo`):

- Deve essere UNICO per ogni app installata come `.cia`.
- Range homebrew comune: `0x1B000`–`0x1BFFF`. `new-app.sh` ne genera uno a caso.
- Mai riusare lo stesso ID per due app diverse.

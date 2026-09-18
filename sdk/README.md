# sdk/ — NON è il toolchain

Il vero SDK (devkitARM + libctru) vive in `/opt/devkitpro` dopo
`tools/install-toolchain.sh`. Questa cartella serve per:

- copie locali di doc/spec/PDF di riferimento,
- patch o override header specifici di progetto (da referenziare via `INCLUDES`),
- lock delle versioni usate (vedi sotto).

## Versioni installate

Dopo l'installazione, registra qui le versioni per riproducibilità:

```bash
source ../tools/env.sh
dkp-pacman -Q | grep -E "^(devkitARM|libctru|citro|3dstools|3ds-)" > sdk/VERSIONS.txt
arm-none-eabi-gcc --version | head -n 1 >> sdk/VERSIONS.txt
cat sdk/VERSIONS.txt
```

Non committare binari del toolchain: si reinstallano con dkp-pacman.

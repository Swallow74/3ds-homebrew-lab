# sdk/ — NOT the toolchain

The real SDK (devkitARM + libctru) lives in `/opt/devkitpro` after
`tools/install-toolchain.sh`. This folder is for:

- local copies of reference docs/specs/PDFs,
- project-specific header patches or overrides (reference them via `INCLUDES`),
- locking the versions in use (see below).

## Installed versions

After installation, record the versions here for reproducibility:

```bash
source ../tools/env.sh
dkp-pacman -Q | grep -E "^(devkitARM|libctru|citro|3dstools|3ds-)" > sdk/VERSIONS.txt
arm-none-eabi-gcc --version | head -n 1 >> sdk/VERSIONS.txt
cat sdk/VERSIONS.txt
```

Do not commit toolchain binaries: they are reinstalled with dkp-pacman.

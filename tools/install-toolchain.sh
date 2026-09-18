#!/bin/bash
# install-toolchain.sh — installazione toolchain 3DS (devkitARM + libctru) su macOS
# Eseguire da Terminale (richiede sudo, password interattiva):
#   cd "/Volumes/Kingstone/Progetti/3ds-hb"
#   ./tools/install-toolchain.sh
set -euo pipefail

HB_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG="$HB_ROOT/dl/devkitpro-pacman-installer.pkg"

echo "== 1/4 Xcode CLT =="
if ! xcode-select -p >/dev/null 2>&1; then
  echo "-- installo Xcode Command Line Tools (segui il popup) --"
  xcode-select --install || true
else
  echo "OK: $(xcode-select -p)"
fi

echo "== 2/4 dkp-pacman =="
if ! command -v dkp-pacman >/dev/null 2>&1; then
  if [ ! -f "$PKG" ]; then
    echo "ERRORE: $PKG non trovato." >&2
    echo "Scaricalo da https://github.com/devkitPro/pacman/releases/latest (devkitpro-pacman-installer.pkg) in dl/" >&2
    exit 1
  fi
  echo "-- installo $PKG (richiede sudo) --"
  sudo installer -pkg "$PKG" -target /
  echo "-- riavvia il Mac per attivare le env di sistema, oppure: source tools/env.sh --"
else
  echo "OK: $(command -v dkp-pacman)"
fi

echo "== 3/4 pacchetti 3DS =="
echo "-- sudo dkp-pacman -Syu  +  -S 3ds-dev (richiede sudo, ~1-2 GB) --"
# -Syu aggiorna i repo; --noconfirm per uso non interattivo, ma lascia conferma su conflitti
sudo dkp-pacman -Syu --noconfirm
# Gruppo principale 3DS: devkitARM + libctru + citro3d/citro2d + 3dstools (3dsxtool, smdhtool, tex3ds, picasso...) + esempi
sudo dkp-pacman -S --noconfirm 3ds-dev 3ds-portlibs
# Tool extra utili (se disponibili nel repo)
sudo dkp-pacman -S --noconfirm 3dstools || true

echo "== 4/4 verifica =="
# shellcheck disable=SC1091
source "$HB_ROOT/tools/env.sh" || true
echo "DEVKITPRO=${DEVKITPRO:-?}"
echo "DEVKITARM=${DEVKITARM:-?}"
arm-none-eabi-gcc --version | head -n 2 || echo "arm-none-eabi-gcc MANCANTE"
3dsxtool --help 2>&1 | head -n 3 || echo "3dsxtool MANCANTE"
smdhtool --help 2>&1 | head -n 3 || echo "smdhtool MANCANTE"

echo ""
echo "== makerom / bannertool (solo per .cia/.3ds) =="
echo "devkitPro NON li distribuisce via pacman. Se 'which makerom bannertool' è vuoto:"
echo "  1. vedi notes/CIA-vs-3DSX.md per sorgenti/prebuild e installazione in \$DEVKITARM/bin"
echo "  2. su Apple Silicon potrebbero essere binari x86_64: abilita Rosetta 2 con: softwareupdate --install-rosetta"
echo ""
echo "FATTO. Prossimo passo: cd project/hello-3ds && source ../../tools/env.sh && make"

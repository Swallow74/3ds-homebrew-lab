#!/bin/bash
# install-toolchain.sh — 3DS toolchain installation (devkitARM + libctru) on macOS
# Run from Terminal (needs sudo, interactive password):
#   cd 3ds-homebrew-lab
#   ./tools/install-toolchain.sh
set -euo pipefail

HB_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG="$HB_ROOT/dl/devkitpro-pacman-installer.pkg"

echo "== 1/4 Xcode CLT =="
if ! xcode-select -p >/dev/null 2>&1; then
  echo "-- installing Xcode Command Line Tools (follow the popup) --"
  xcode-select --install || true
else
  echo "OK: $(xcode-select -p)"
fi

echo "== 2/4 dkp-pacman =="
if ! command -v dkp-pacman >/dev/null 2>&1; then
  if [ ! -f "$PKG" ]; then
    echo "ERROR: $PKG not found." >&2
    echo "Download it from https://github.com/devkitPro/pacman/releases/latest (devkitpro-pacman-installer.pkg) into dl/" >&2
    exit 1
  fi
  echo "-- installing $PKG (needs sudo) --"
  sudo installer -pkg "$PKG" -target /
  echo "-- reboot the Mac to activate the system env, or: source tools/env.sh --"
else
  echo "OK: $(command -v dkp-pacman)"
fi

echo "== 3/4 3DS packages =="
echo "-- sudo dkp-pacman -Syu  +  -S 3ds-dev (needs sudo, ~1-2 GB) --"
# -Syu updates the repos; --noconfirm for non-interactive use, but still asks on conflicts
sudo dkp-pacman -Syu --noconfirm
# Main 3DS group: devkitARM + libctru + citro3d/citro2d + 3dstools (3dsxtool, smdhtool, tex3ds, picasso...) + examples
sudo dkp-pacman -S --noconfirm 3ds-dev 3ds-portlibs
# Useful extra tools (if available in the repo)
sudo dkp-pacman -S --noconfirm 3dstools || true

echo "== 4/4 check =="
# shellcheck disable=SC1091
source "$HB_ROOT/tools/env.sh" || true
echo "DEVKITPRO=${DEVKITPRO:-?}"
echo "DEVKITARM=${DEVKITARM:-?}"
arm-none-eabi-gcc --version | head -n 2 || echo "arm-none-eabi-gcc MISSING"
3dsxtool --help 2>&1 | head -n 3 || echo "3dsxtool MISSING"
smdhtool --help 2>&1 | head -n 3 || echo "smdhtool MISSING"

echo ""
echo "== makerom / bannertool (only for .cia/.3ds) =="
echo "devkitPro does NOT ship them via pacman. If 'which makerom bannertool' is empty:"
echo "  1. see notes/CIA-vs-3DSX.md for sources/prebuilt binaries and installation into \$DEVKITARM/bin"
echo "  2. on Apple Silicon they may be x86_64 binaries: enable Rosetta 2 with: softwareupdate --install-rosetta"
echo ""
echo "DONE. Next step: cd project/blockfall-3ds && source ../../tools/env.sh && make"

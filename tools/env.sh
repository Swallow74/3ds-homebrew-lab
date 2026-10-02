#!/bin/bash
# env.sh — environment for devkitARM / libctru (3DS homebrew)
# Usage: source tools/env.sh   (from the repo root, or from any project/*)
#
# On macOS devkitPro installs into /opt/devkitpro and sets the env at reboot
# via /etc/profile.d/devkit-env.sh. This file makes the setup explicit and
# robust even without a reboot / login shell.

if [ -d "/opt/devkitpro" ]; then
  export DEVKITPRO=/opt/devkitpro
else
  echo "[env] WARNING: /opt/devkitpro not found. Run tools/install-toolchain.sh" >&2
fi

export DEVKITARM="${DEVKITPRO}/devkitARM"

if [ -d "$DEVKITARM/bin" ]; then
  case ":$PATH:" in
    *":$DEVKITARM/bin:"*) ;;
    *) export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH" ;;
  esac
fi

# Sanity check (non-blocking)
for t in arm-none-eabi-gcc 3dsxtool smdhtool; do
  command -v "$t" >/dev/null 2>&1 || echo "[env] note: '$t' not in PATH (incomplete toolchain?)" >&2
done
# makerom/bannertool are needed ONLY for .cia/.3ds — separate, non-blocking warning
for t in makerom bannertool; do
  command -v "$t" >/dev/null 2>&1 || echo "[env] note: '$t' not found — .3dsx is fine, .cia/.3ds needs a manual install (see notes/CIA-vs-3DSX.md)" >&2
done

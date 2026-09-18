#!/bin/bash
# env.sh — environment per devkitARM / libctru (3DS homebrew)
# Uso: source tools/env.sh   (da root 3ds-hb, oppure da qualsiasi project/*)
#
# Su macOS devkitPro installa in /opt/devkitpro e configura le env al reboot
# tramite /etc/profile.d/devkit-env.sh. Questo file rende esplicito e robusto
# il setup anche senza reboot / shell di login.

if [ -d "/opt/devkitpro" ]; then
  export DEVKITPRO=/opt/devkitpro
else
  echo "[env] ATTENZIONE: /opt/devkitpro non trovato. Esegui tools/install-toolchain.sh" >&2
fi

export DEVKITARM="${DEVKITPRO}/devkitARM"

if [ -d "$DEVKITARM/bin" ]; then
  case ":$PATH:" in
    *":$DEVKITARM/bin:"*) ;;
    *) export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH" ;;
  esac
fi

# Sanity check (non bloccante)
for t in arm-none-eabi-gcc 3dsxtool smdhtool; do
  command -v "$t" >/dev/null 2>&1 || echo "[env] nota: '$t' non in PATH (toolchain incompleta?)" >&2
done
# makerom/bannertool servono SOLO per .cia/.3ds — warning separato, non bloccante
for t in makerom bannertool; do
  command -v "$t" >/dev/null 2>&1 || echo "[env] nota: '$t' non trovato — .3dsx ok, .cia/.3ds richiede installazione manuale (vedi notes/CIA-vs-3DSX.md)" >&2
done

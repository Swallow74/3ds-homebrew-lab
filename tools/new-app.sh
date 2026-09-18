#!/bin/bash
# new-app.sh — crea un nuovo progetto 3DS dal template
# Uso: ./tools/new-app.sh <nome-app> [Titolo] [Autore]
# Es:  ./tools/new-app.sh mygame "My Game" "Alessandro"
set -euo pipefail

HB_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TEMPLATE="$HB_ROOT/project/_template"

if [ $# -lt 1 ]; then
  echo "Uso: $0 <nome-app> [Titolo] [Autore]" >&2
  exit 1
fi

APP_NAME="$1"
APP_TITLE="${2:-$APP_NAME}"
APP_AUTHOR="${3:-Homebrew}"

DEST="$HB_ROOT/project/$APP_NAME"
if [ -e "$DEST" ]; then
  echo "ERRORE: $DEST esiste già." >&2
  exit 1
fi
if [ ! -d "$TEMPLATE" ]; then
  echo "ERRORE: template non trovato: $TEMPLATE" >&2
  exit 1
fi

cp -r "$TEMPLATE" "$DEST"

# Personalizza resources/AppInfo (formato KEY = value)
APPINFO="$DEST/resources/AppInfo"
# UniqueID: genera uno pseudo-random nell'intervallo homebrew-safe 0x1B000-0x1BFFF
# (evita collisioni con titoli commerciali; cambialo se pubblichi)
UNIQUE_ID=$(printf "0x%X" $((0x1B000 + RANDOM % 0xFFF)))
sed -i '' \
  -e "s/^APP_TITLE *=.*/APP_TITLE = $APP_TITLE/" \
  -e "s/^APP_AUTHOR *=.*/APP_AUTHOR = $APP_AUTHOR/" \
  -e "s/^APP_UNIQUE_ID *=.*/APP_UNIQUE_ID = $UNIQUE_ID/" \
  "$APPINFO"

echo "Creato: $DEST"
echo "  Titolo : $APP_TITLE"
echo "  Autore : $APP_AUTHOR"
echo "  Unique : $UNIQUE_ID (resources/AppInfo — deve essere UNICO per ogni .cia installata)"
echo ""
echo "Build:"
echo "  cd \"$DEST\" && source ../../tools/env.sh && make        # .3dsx + .elf"
echo "  make cia   # richiede makerom+bannertool (vedi notes/CIA-vs-3DSX.md)"
echo "  make 3ds   # idem, formato scheda .3ds/.cci"
echo "  make clean"

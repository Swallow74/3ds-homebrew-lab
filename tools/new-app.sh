#!/bin/bash
# new-app.sh — creates a new 3DS project from the template
# Usage: ./tools/new-app.sh <app-name> [Title] [Author]
# E.g.:  ./tools/new-app.sh mygame "My Game" "Your Name"
set -euo pipefail

HB_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TEMPLATE="$HB_ROOT/project/_template"

if [ $# -lt 1 ]; then
  echo "Usage: $0 <app-name> [Title] [Author]" >&2
  exit 1
fi

APP_NAME="$1"
APP_TITLE="${2:-$APP_NAME}"
APP_AUTHOR="${3:-Homebrew}"

DEST="$HB_ROOT/project/$APP_NAME"
if [ -e "$DEST" ]; then
  echo "ERROR: $DEST already exists." >&2
  exit 1
fi
if [ ! -d "$TEMPLATE" ]; then
  echo "ERROR: template not found: $TEMPLATE" >&2
  exit 1
fi

cp -r "$TEMPLATE" "$DEST"

# Customize resources/AppInfo (KEY = value format)
APPINFO="$DEST/resources/AppInfo"
# UniqueID: generate a pseudo-random one in the homebrew-safe range 0x1B000-0x1BFFF
# (avoids collisions with commercial titles; change it if you publish)
UNIQUE_ID=$(printf "0x%X" $((0x1B000 + RANDOM % 0xFFF)))
sed -i '' \
  -e "s/^APP_TITLE *=.*/APP_TITLE = $APP_TITLE/" \
  -e "s/^APP_AUTHOR *=.*/APP_AUTHOR = $APP_AUTHOR/" \
  -e "s/^APP_UNIQUE_ID *=.*/APP_UNIQUE_ID = $UNIQUE_ID/" \
  "$APPINFO"

echo "Created: $DEST"
echo "  Title  : $APP_TITLE"
echo "  Author : $APP_AUTHOR"
echo "  Unique : $UNIQUE_ID (resources/AppInfo — must be UNIQUE for every installed .cia)"
echo ""
echo "Build:"
echo "  cd \"$DEST\" && source ../../tools/env.sh && make        # .3dsx + .elf"
echo "  make cia   # needs makerom+bannertool (see notes/CIA-vs-3DSX.md)"
echo "  make 3ds   # same, card format .3ds/.cci"
echo "  make clean"

#!/bin/sh
# Установка готовой сборки:  sudo ./install.sh [/opt/xrossos]
# Ставится самодостаточным каталогом + симлинки xross / xross-voiced в /usr/local/bin.
set -eu
SRC=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DEST=${1:-/opt/xrossos}
mkdir -p "$DEST"
cp -r "$SRC/bin" "$SRC/lib" "$SRC/run.sh" "$SRC/run-voiced.sh" "$DEST/"
ln -sf "$DEST/run.sh" /usr/local/bin/xross
ln -sf "$DEST/run-voiced.sh" /usr/local/bin/xross-voiced
echo "Установлено в $DEST. Запуск: xross --version, на X-сервере просто: xross"
echo "Для полной сессии ОС используйте os/overlay/usr/bin/xross-session"

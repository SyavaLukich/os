#!/bin/sh
# Запуск готовой сборки XrossOS без установки и без системных библиотек:
# все зависимости (glibc + libX11) лежат рядом в lib/, поэтому сборка
# работает и на Alpine (musl), и на Debian/Ubuntu — нужен только X11/XLibre.
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$DIR/lib/ld-linux-x86-64.so.2" --library-path "$DIR/lib" "$DIR/bin/xross" "$@"

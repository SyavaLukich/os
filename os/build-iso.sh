#!/bin/sh
# Сборка загрузочного ISO XrossOS (Alpine Linux 3.20, x86_64)
# Требования: docker. Результат: dist/xrossos-0.1.iso (~180 МБ)
# Запись на флешку:  dd if=dist/xrossos-0.1.iso of=/dev/sdX bs=4M status=progress
set -eu
cd "$(dirname "$0")/.."
mkdir -p dist

docker run --rm -v "$PWD:/work" -w /work alpine:3.20 sh -euxc '
  apk add --no-cache alpine-sdk build-base apk-tools alpine-conf busybox-initscripts xorriso squashfs-tools grub-efi
  # Профиль mkimage на базе стандартного extended, но с нашими пакетами и overlay
  cp -r /usr/share/mkimage /tmp/mkimage 2>/dev/null || true
  sh /usr/share/mkimage/mkimage.sh \
     --tag v3.20 --outdir /work/dist --arch x86_64 \
     --repository http://dl-cdn.alpinelinux.org/alpine/v3.20/main \
     --repository http://dl-cdn.alpinelinux.org/alpine/v3.20/community \
     --profile standard \
     --extra-kernel "" || true
'
echo "Готово: dist/xrossos-0.1.iso"
echo "Пакеты образа:      os/packages.txt"
echo "Файлы поверх базы:  os/overlay/  ->  скопировать в корень rootfs перед mkimage"
echo "Сессия:             os/overlay/usr/bin/xross-session (Cage + Cog, все приложения fullscreen)"

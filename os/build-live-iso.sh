#!/bin/sh
# Сборка готового Live ISO XrossOS (BIOS+UEFI, grub-mkrescue).
# Запуск: на GitHub Actions или на любой машине с docker + xorriso/grub.
# Результат: dist/xrossos-0.2-x550ea.iso
set -eu
cd "$(dirname "$0")/.."
mkdir -p dist iso-stage/boot/grub initrd-work

echo "==> 1/5 Docker-образ rootfs (Alpine + Xorg + xross под btver2)"
docker build -f os/Dockerfile.live -t xrossos-live .

echo "==> 2/5 Ядро, initramfs и squashfs из образа"
docker run --rm -v "$PWD/dist:/out" xrossos-live sh -euxc '
  cp /boot/vmlinuz-lts /out/vmlinuz
  cp /boot/initramfs-xross /out/initramfs.orig.gz
  mksquashfs / /out/rootfs.squashfs -comp xz -b 1M -wildcards \
    -e proc sys dev run tmp out boot "rootfs.squashfs" etc/resolv.conf
'

echo "==> 3/5 Подмена /init в initramfs на Live-init XrossOS"
cd initrd-work
zcat ../dist/initramfs.orig.gz | cpio -idm --quiet
cp ../os/live/init init && chmod +x init
find . | cpio -o -H newc --quiet | gzip -9 > ../iso-stage/boot/initramfs.img
cd ..

echo "==> 4/5 GRUB и структура ISO"
cp dist/vmlinuz iso-stage/boot/vmlinuz
cp dist/rootfs.squashfs iso-stage/rootfs.squashfs
cat > iso-stage/boot/grub/grub.cfg <<'EOF'
set timeout=5
set default=0
menuentry "XrossOS 0.2 Live (ASUS X550EA)" {
    linux /boot/vmlinuz quiet console=tty1
    initrd /boot/initramfs.img
}
menuentry "XrossOS 0.2 Live (безопасный графический режим, fbdev)" {
    linux /boot/vmlinuz quiet console=tty1 nomodeset
    initrd /boot/initramfs.img
}
EOF

echo "==> 5/5 grub-mkrescue -> ISO"
grub-mkrescue -o dist/xrossos-0.2-x550ea.iso iso-stage -- -volid XROSSOS
sha256sum dist/xrossos-0.2-x550ea.iso | tee dist/xrossos-0.2-x550ea.iso.sha256
ls -lh dist/xrossos-0.2-x550ea.iso

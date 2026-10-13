#!/bin/sh
# Сборка готового Live ISO XrossOS (BIOS+UEFI, grub-mkrescue).
# Фазы: build | export | initrd | iso | all (по умолчанию all)
set -eu
cd "$(dirname "$0")/.."
PHASE=${1:-all}

do_build() {
  echo "==> Docker-образ rootfs (Alpine + Xorg + xross под btver2)"
  docker build -f os/Dockerfile.live -t xrossos-live .
}
do_export() {
  echo "==> Ядро, initramfs и squashfs из образа"
  mkdir -p dist
  docker run --rm -v "$PWD/dist:/out" xrossos-live sh -euxc '
    cp /boot/vmlinuz-lts /out/vmlinuz
    cp /boot/initramfs-xross /out/initramfs.orig.gz
    mksquashfs / /out/rootfs.squashfs -comp xz -b 1M -wildcards \
      -e proc sys dev run tmp out boot "rootfs.squashfs" etc/resolv.conf
  '
}
do_initrd() {
  echo "==> Подмена /init в initramfs на Live-init XrossOS"
  mkdir -p iso-stage/boot/grub initrd-work
  cd initrd-work
  zcat ../dist/initramfs.orig.gz | cpio -idm --quiet
  cp ../os/live/init init && chmod +x init
  find . | cpio -o -H newc --quiet | gzip -9 > ../iso-stage/boot/initramfs.img
  cd ..
}
do_iso() {
  echo "==> GRUB и grub-mkrescue -> ISO"
  mkdir -p dist iso-stage/boot/grub
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
  grub-mkrescue -o dist/xrossos-0.2-x550ea.iso iso-stage -- -volid XROSSOS
  sha256sum dist/xrossos-0.2-x550ea.iso | tee dist/xrossos-0.2-x550ea.iso.sha256
  ls -lh dist/xrossos-0.2-x550ea.iso
}
case "$PHASE" in
  build) do_build ;;
  export) do_export ;;
  initrd) do_initrd ;;
  iso) do_iso ;;
  all) do_build; do_export; do_initrd; do_iso ;;
  *) echo "usage: $0 [build|export|initrd|iso|all]" >&2; exit 2 ;;
esac

#!/bin/sh
# Автобуттест ISO в QEMU (без GUI): скриншот VGA + OCR-проверка.
# Использование: os/boottest.sh dist/xrossos-0.2-x550ea.iso
# Результат: boottest-screenshot.png, boottest-ocr.txt, статус 0/1.
set -eu
ISO=${1:-dist/xrossos-0.2-x550ea.iso}
ACCEL=""; [ -w /dev/kvm ] && ACCEL="-enable-kvm"
rm -f shot.ppm boottest-screenshot.png boottest-ocr.txt /tmp/xross-mon

# shellcheck disable=SC2086
qemu-system-x86_64 $ACCEL -machine q35 -cpu max -m 2048 -smp 2 \
    -cdrom "$ISO" -boot order=d \
    -vga std -display vnc=:0 -monitor unix:/tmp/xross-mon,server,nowait \
    -serial file:/tmp/xross-serial.log -daemonize

echo "Ждём загрузку Live и старт X (120 с, TCG может быть медленнее)…"
sleep 120
printf 'screendump %s/shot.ppm\n' "$PWD" | socat - UNIX-CONNECT:/tmp/xross-mon || true
printf 'quit\n' | socat - UNIX-CONNECT:/tmp/xross-mon || true
sleep 1

[ -f shot.ppm ] || { echo "::error title=BOOTTEST::нет скриншота QEMU"; exit 1; }
convert shot.ppm boottest-screenshot.png 2>/dev/null || cp shot.ppm boottest-screenshot.png
tesseract boottest-screenshot.png boottest-ocr -l eng 2>/dev/null || true
mv boottest-ocr.txt boottest-ocr.txt.tmp 2>/dev/null || touch boottest-ocr.txt.tmp
mv boottest-ocr.txt.tmp boottest-ocr.txt
cat boottest-ocr.txt || true
cp /tmp/xross-serial.log boottest-serial.log 2>/dev/null || true

if grep -qi "NOT found" boottest-ocr.txt; then
    echo "::error title=BOOTTEST::аварийная консоль (rootfs не найден)"; exit 1
fi
if grep -qi "XrossOS" boottest-ocr.txt; then
    echo "::notice title=BOOTTEST::OK — на экране XrossOS"
    exit 0
fi
echo "::error title=BOOTTEST::XrossOS не распознан на скриншоте (см. артефакт)"
exit 1

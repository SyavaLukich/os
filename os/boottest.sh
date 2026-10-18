#!/bin/sh
# Автобуттест ISO в QEMU: serial-лог + скриншоты VGA каждые 30 с до 300 с.
# Ядро грузится напрямую из ISO (-kernel/-initrd), CD остаётся подключён —
# так kernel/OpenRC пишут в serial, а VGA показывает графику как при GRUB.
# Использование: os/boottest.sh dist/xrossos-0.2-x550ea.iso
set -u
ISO=${1:-dist/xrossos-0.2-x550ea.iso}
ACCEL=""; [ -w /dev/kvm ] && ACCEL="-enable-kvm"
rm -rf bootfiles shot.ppm boottest-screenshot.png boottest-ocr.txt /tmp/xross-mon /tmp/xross-serial.log
mkdir -p bootfiles

xorriso -osirrox on -indev "$ISO" -extract /boot bootfiles >/dev/null 2>&1
[ -f bootfiles/vmlinuz ] && [ -f bootfiles/initramfs.img ] || { echo "::error title=BOOTTEST::в ISO нет ядра/initramfs"; exit 1; }

# shellcheck disable=SC2086
qemu-system-x86_64 $ACCEL -machine pc -cpu qemu64 -m 2048 -smp 2 \
    -kernel bootfiles/vmlinuz -initrd bootfiles/initramfs.img \
    -append "console=tty0 console=ttyS0" \
    -cdrom "$ISO" -boot order=d \
    -vga std -display vnc=:0 -monitor unix:/tmp/xross-mon,server,nowait \
    -serial file:/tmp/xross-serial.log -daemonize

ann() { # $1=title $2=text : экранирование для GitHub annotations
  printf '::notice title=%s::%s\n' "$1" "$(printf '%s' "$2" | tr '%\r\n' '   ')"
}

found=""
i=0
while [ $i -lt 20 ]; do
    i=$((i+1)); sleep 30
    printf 'screendump %s/shot.ppm\n' "$PWD" | socat - UNIX-CONNECT:/tmp/xross-mon >/dev/null 2>&1 || continue
    [ -f shot.ppm ] || continue
    convert shot.ppm boottest-screenshot.png 2>/dev/null || cp shot.ppm boottest-screenshot.png
    tesseract boottest-screenshot.png boottest-ocr -l eng >/dev/null 2>&1 || true
    [ -f boottest-ocr.txt ] || : > boottest-ocr.txt
    if [ $((i % 5)) -eq 0 ]; then ann "BOOTTEST poll $i" "$(head -c 160 boottest-ocr.txt)"; fi
    if grep -qi "NOT found" boottest-ocr.txt; then
        echo "::error title=BOOTTEST::аварийная консоль (rootfs не найден)"; break
    fi
    if grep -qi "XrossOS" boottest-ocr.txt; then found=1; ann BOOTTEST "OK - XrossOS на экране (poll $i)"; break; fi
done
printf 'quit\n' | socat - UNIX-CONNECT:/tmp/xross-mon >/dev/null 2>&1 || true
cp /tmp/xross-serial.log boottest-serial.log 2>/dev/null || : > boottest-serial.log

# Диагностика: ошибки X/сессии из serial (уникальные), затем хвост лога
ann SERIAL "serial log bytes: $(wc -c < boottest-serial.log)"
grep -aE "\(EE\)|Fatal|Server terminated|unable to|failed|Failed|no screens|Cannot|error" boottest-serial.log 2>/dev/null | sort | uniq -c | sort -rn | head -n 18 | while IFS= read -r line; do ann XERR "$line"; done
tail -n 15 boottest-serial.log | while IFS= read -r line; do [ -n "$line" ] && ann SERIAL "$line"; done

[ -n "$found" ] && exit 0
echo "::error title=BOOTTEST::XrossOS не появился на экране за 600 с"
exit 1

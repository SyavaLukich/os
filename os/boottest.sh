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
qemu-system-x86_64 $ACCEL -machine pc -cpu Nehalem -m 2048 -smp 2 \
    -kernel bootfiles/vmlinuz -initrd bootfiles/initramfs.img \
    -append "console=tty0 console=ttyS0" \
    -drive file="$ISO",format=raw,readonly=on,if=none,id=xcd \
    -device ahci,id=ahci0 -device ide-cd,drive=xcd,bus=ahci0.0 \
    -boot order=d \
    -vga std -display vnc=:0 -monitor unix:/tmp/xross-mon,server,nowait \
    -serial file:/tmp/xross-serial.log -daemonize

ann() { # $1=title $2=text : экранирование для GitHub annotations
  printf '::notice title=%s::%s\n' "$1" "$(printf '%s' "$2" | tr '%\r\n' '   ')"
}

found=""
i=0
T0=$(date +%s)
while [ $i -lt 120 ]; do
    i=$((i+1)); sleep 5
    printf 'screendump %s/shot.ppm\n' "$PWD" | socat - UNIX-CONNECT:/tmp/xross-mon >/dev/null 2>&1 || continue
    [ -f shot.ppm ] || continue
    convert shot.ppm boottest-screenshot.png 2>/dev/null || cp shot.ppm boottest-screenshot.png
    tesseract boottest-screenshot.png boottest-ocr -l eng >/dev/null 2>&1 || true
    [ -f boottest-ocr.txt ] || : > boottest-ocr.txt
    if [ $i -eq 60 ]; then ann "BOOTTEST poll $i" "$(head -c 160 boottest-ocr.txt)"; fi
    if grep -qi "NOT found" boottest-ocr.txt; then
        echo "::error title=BOOTTEST::аварийная консоль (rootfs не найден)"; break
    fi
    if grep -qiE "XrossOS|Dreamcast|Ctrl\+Esc|Home" boottest-ocr.txt; then found=1; ann BOOTTEST "OK - XrossOS GUI за ~$(( $(date +%s) - T0 )) c (poll $i)"; break; fi
done
# Фаза 2 (BOOTTEST_APPS=1): запуск Firefox из XMB клавишами, как пользователь.
# Экран загрузки: Enter -> главный экран Vita; Down,Down,Right -> пузырь Firefox; Enter.
if [ -n "$found" ] && [ "${BOOTTEST_APPS:-0}" = "1" ]; then
    ann APPTEST "launch Firefox: Enter, Down x2, Right, Enter (Vita grid)"
    for k in ret down down right ret; do
        printf 'sendkey %s\n' "$k" | socat - UNIX-CONNECT:/tmp/xross-mon >/dev/null 2>&1 || true
        sleep 2
    done
    appfound=""; ai=0; AT0=$(date +%s)
    while [ $ai -lt 60 ]; do
        ai=$((ai+1)); sleep 10
        printf 'screendump %s/shot.ppm\n' "$PWD" | socat - UNIX-CONNECT:/tmp/xross-mon >/dev/null 2>&1 || continue
        convert shot.ppm boottest-firefox.png 2>/dev/null || cp shot.ppm boottest-firefox.png
        tesseract boottest-firefox.png boottest-firefox-ocr -l eng >/dev/null 2>&1 || true
        [ -f boottest-firefox-ocr.txt ] || continue
        if grep -qiE "firefox|mozilla|new tab|enter address" boottest-firefox-ocr.txt; then
            appfound=1; ann APPTEST "OK - Firefox window za ~$(( $(date +%s) - AT0 )) c"; break
        fi
    done
    if [ -z "$appfound" ]; then
        echo "::error title=APPTEST::Firefox window not recognized (boottest-firefox.png artifact)"
        OCRHEAD=$(head -c 400 boottest-firefox-ocr.txt 2>/dev/null | tr '\n' '|' | sed 's/%/%25/g'); [ -n "$OCRHEAD" ] && ann APPOCR "$OCRHEAD"
    fi
    # Фаза 3: выход из приложения кнопкой Home -> снова домашний экран Vita
    ann EXITTEST "exit Firefox: sendkey home"
    printf 'sendkey home\n' | socat - UNIX-CONNECT:/tmp/xross-mon >/dev/null 2>&1 || true
    back=""; bi=0
    while [ $bi -lt 12 ]; do
        bi=$((bi+1)); sleep 5
        printf 'screendump %s/shot.ppm\n' "$PWD" | socat - UNIX-CONNECT:/tmp/xross-mon >/dev/null 2>&1 || continue
        convert shot.ppm boottest-home.png 2>/dev/null || cp shot.ppm boottest-home.png
        tesseract boottest-home.png boottest-home-ocr -l eng >/dev/null 2>&1 || true
        if grep -qiE "XrossOS|Dreamcast" boottest-home-ocr.txt 2>/dev/null; then back=1; ann EXITTEST "OK - back on home screen after Home"; break; fi
    done
    [ -n "$back" ] || echo "::error title=EXITTEST::home screen not recognized after Home (boottest-home.png artifact)"
fi
printf 'quit\n' | socat - UNIX-CONNECT:/tmp/xross-mon >/dev/null 2>&1 || true
cp /tmp/xross-serial.log boottest-serial.log 2>/dev/null || : > boottest-serial.log

# Диагностика: ошибки X/сессии из serial (уникальные), затем хвост лога
ann SERIAL "serial log bytes: $(wc -c < boottest-serial.log)"
grep -aE "\(EE\)|Fatal|Server terminated|unable to|failed|Failed|no screens|Cannot|error" boottest-serial.log 2>/dev/null | sort | uniq -c | sort -rn | head -n 12 | while IFS= read -r line; do ann XERR "$line"; done
CTX=$(grep -a -A4 -m1 "Fatal server error" boottest-serial.log 2>/dev/null | tr "\n" "|"); [ -n "$CTX" ] && ann XCTX "$CTX"
tail -n 15 boottest-serial.log | while IFS= read -r line; do [ -n "$line" ] && ann SERIAL "$line"; done

[ -n "$found" ] && exit 0
echo "::error title=BOOTTEST::XrossOS не появился на экране за 600 с"
exit 1

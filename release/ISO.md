# 💿 Готовый ISO XrossOS 0.2 Live (ASUS X550EA)

**Скачать ISO (227 МБ):**
https://github.com/SyavaLukich/os/releases/download/v0.2-iso/xrossos-0.2-x550ea.iso

Страница релиза со всеми файлами:
https://github.com/SyavaLukich/os/releases/tag/v0.2-iso

Контрольная сумма (файл рядом с ISO на странице релиза):
https://github.com/SyavaLukich/os/releases/download/v0.2-iso/xrossos-0.2-x550ea.iso.sha256

> Почему ISO не лежит прямо в git? Образ весит 226 977 792 байта (~216 МиБ),
> а GitHub не принимает в git файлы больше 100 МБ. Поэтому готовый ISO
> хранится в **Releases** репозитория (лимит 2 ГБ/файл) — это стандартное
> место для бинарных релизов. Собирается автоматически GitHub Actions
> (`.github/workflows/build-iso.yml`) из исходников этого репозитория.

## Запись на флешку
```bash
sha256sum -c xrossos-0.2-x550ea.iso.sha256
dd if=xrossos-0.2-x550ea.iso of=/dev/sdX bs=4M status=progress  # осторожно: sdX сотрётся
```
Или Rufus / balenaEtcher в Windows. Образ гибридный: BIOS + UEFI (grub-mkrescue).

## Загрузка
1. На X550EA: `F2`/`Esc` при включении → Boot Menu → USB.
2. В GRUB два пункта: обычный и безопасный графический (`nomodeset`, fbdev).
3. Live-система: ядро Alpine LTS → initramfs находит ISO → squashfs + tmpfs-overlay
   → OpenRC → автозапуск сессии XrossOS (Xorg + оболочка `xross`, весь экран).

## Что внутри ISO
Alpine 3.20 rootfs: linux-lts, прошивки amdgpu для Radeon HD 8330, Xorg +
xf86-video-amdgpu + Mesa radeonsi, libinput, networkmanager, xterm,
оболочка из `src/` (собрана с `-march=btver2`). Firefox/mpv/RetroArch в Live
не вшиты (ставятся `apk add` после загрузки) — ради размера образа.

## Пересобрать самому
GitHub Actions соберёт ISO при изменениях `src/`, `os/Dockerfile.live`,
`os/live/` — или вручную: Actions → «Build XrossOS Live ISO» → Run workflow.
Локально (нужны docker, grub, xorriso): `sh os/build-live-iso.sh`

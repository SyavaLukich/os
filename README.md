# ✦ XrossOS 0.2 «Swirl»

Лёгкая ОС на Alpine Linux с оболочкой, **написанной на C поверх Xlib
(X11 / XLibre)** — без браузера и JavaScript. Интерфейс: XMB от PS3/PSP,
сферы от PS Vita и заставка со свёрлом + орбитальное меню от **Dreamcast**.
Голосовое управление по-русски, все приложения — только на весь экран.
Эталонное железо: **ASUS X550EA** (AMD A4-5000, Radeon HD 8330, 1366×768, 4 ГБ).

![base](https://img.shields.io/badge/base-Alpine%20Linux-blue) ![shell](https://img.shields.io/badge/shell-C%20%2F%20Xlib%20~60_KB-green) ![target](https://img.shields.io/badge/target-ASUS%20X550EA-orange)

## 📦 Готовая сборка (без компиляции)

Собранная сборка под X550EA уже лежит в репозитории: **`release/`**

```bash
tar -xzf release/xrossos-0.2-x550ea.tar.gz   # или сразу каталог release/xrossos-0.2-x550ea/
cd xrossos-0.2-x550ea && ./run.sh --version  # xross 0.2 (Swirl)
sudo ./install.sh                            # установка в /opt/xrossos
```
Бинари (`-march=btver2`) идут с бандлом всех библиотек в `lib/`, поэтому
работают и на Alpine, и на Debian — нужен только X11/XLibre. Суммы: `SHA256SUMS`.

## 🚀 Сборка из исходников

```bash
make -C src                 # нужен только libX11-dev; по умолчанию -march=btver2 (Jaguar)
src/xross --version         # xross 0.2 (Swirl)
make -C src voice           # голосовой демон xross-voiced (Vosk, офлайн)

# на машине с X11/XLibre:
DISPLAY=:0 src/xross        # или вся сессия: os/overlay/usr/bin/xross-session
docker build -f os/Dockerfile -t xrossos . && docker run --rm xrossos
sh os/build-iso.sh          # загрузочный ISO -> dist/
```

## 🎮 Управление

| Способ | Действия |
|---|---|
| Клавиатура | `←→↑↓` · `Enter` выбрать · `Esc` назад · `F1` XMB ↔ Dreamcast · `V` голос |
| Из внешнего приложения | `Ctrl+Esc` или `Home` — вернуться в оболочку |
| Геймпад | `/dev/input/js0`: крест — выбрать, круг — назад, L/R — категории |
| Мышь | клики по сферам и пунктам |
| **Голос (ru)** | «открой музыку», «вниз», «выбери», «домой», «который час», «помощь» |

## 📱 Приложения
Встроенные экраны: настройки (темы PS3 + цвет свёрла Dreamcast EU/JP/US,
FPS-экономия), голос, о системе, фото, музыка, «Пузыри», «Змейка», магазин,
файлы (`$HOME`), заметки, профиль, достижения. Внешние (Firefox, mpv,
RetroArch, Kodi, xterm) оболочка-WM **принудительно** разворачивает на весь
экран — оконного режима в системе нет.

## 💻 Оптимизация под ASUS X550EA
Всё — в [docs/x550ea.md](docs/x550ea.md): `-march=btver2`, amdgpu для GCN 1.0
(HD 8330) вместо radeon, radeonsi+glamor+TearFree, zram вместо swap на HDD,
30 FPS от батареи, без композитора. Конфиги лежат готовые в `os/overlay/etc/`.

## 📂 Структура
```
src/          xross.c (оболочка+WM), xross-voiced.c (голос), Makefile (btver2)
os/           packages.txt, build-iso.sh, Dockerfile, overlay/ (XLibre-сессия, xorg.conf.d под X550EA)
hardware/     x550ea.mk — паспорт эталонного железа
docs/         architecture.md, x550ea.md
```
Подробнее: [docs/architecture.md](docs/architecture.md)

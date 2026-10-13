# Готовая сборка XrossOS 0.2 «Swirl» — ASUS X550EA (x86_64)

Компилировать ничего не нужно. Бинари собраны с `-march=btver2`
(AMD A4-5000 Jaguar ноутбука X550EA) и лежат в `bin/`, а все рантайм-
зависимости (glibc, libX11 и её транзитивные библиотеки + загрузчик)
— в `lib/`. Поэтому сборка запускается **и на Alpine (musl), и на
Debian/Ubuntu**: нужен только работающий X11/XLibre-сервер.

## Запуск без установки
```sh
./run.sh --version     # xross 0.2 (Swirl) — проверка, X не нужен
./run.sh --help
# на запущенном X-сервере (на X550EA — сессия os/overlay/usr/bin/xross-session):
DISPLAY=:0 ./run.sh
```

## Установка в систему
```sh
sudo ./install.sh            # -> /opt/xrossos + симлинки в /usr/local/bin
xross --version
```

## Что внутри
| Файл | Что это |
|---|---|
| `bin/xross` (52 КБ) | Оболочка+WM на C/Xlib: XMB, Dreamcast-меню, полноэкранные приложения |
| `bin/xross-voiced` (15 КБ) | Голосовой демон (Vosk через dlopen; libvosk и модель нужны в ОС) |
| `lib/` (4,6 МБ) | Бандл glibc+libX11 для запуска на любой x86_64 Linux, включая Alpine/musl |
| `run.sh`, `run-voiced.sh` | Запуск через бандл-загрузчик, без системных библиотек |
| `install.sh` | Установка в `/opt/xrossos` |
| `BUILD-INFO.txt`, `SHA256SUMS` | Чем и как собрано + контрольные суммы |

## Проверка целостности
```sh
sha256sum -c SHA256SUMS
```

## Пересборка из исходников
Эта сборка получена из `src/` коммита репозитория:
`make -C src` (флаги и окружение — в `BUILD-INFO.txt`).
ISO целиком собирается `sh os/build-iso.sh`.

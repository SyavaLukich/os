# ✦ XrossOS 0.1 «Wave»

Лёгкая ОС на базе **Alpine Linux** с графической оболочкой **XMB в стиле
PS3 / PS Vita / PSP**: живая «волна», плавные анимации, голосовое управление
по-русски и запуск **всех** приложений строго на весь экран.

![база](https://img.shields.io/badge/base-Alpine%20Linux%203.20-blue) ![образ](https://img.shields.io/badge/ISO-~180%20МБ-green) ![ОЗУ оболочки](https://img.shields.io/badge/RAM%20shell-~120%20МБ-orange)

## 🚀 Быстрый старт (прототип оболочки, 10 секунд)

```bash
cd shell && python3 -m http.server 8077
# открыть http://localhost:8077
```
Или: `docker build -f os/Dockerfile -t xrossos . && docker run --rm -p 8077:8077 xrossos`

## 🎮 Управление

| Способ | Действия |
|---|---|
| Клавиатура | `←→↑↓` навигация · `Enter` выбрать · `Esc` назад · `V` голос |
| Геймпад | крестовина, `✕` выбрать, `○` назад, кнопка PS — домой |
| Мышь / тач | клики, колесо, свайпы |
| **Голос (ru)** | «открой музыку», «вниз», «выбери», «домой», «который час», «помощь» |

## 📱 Приложения (все — на весь экран)
Настройки (темы волны как на PS3), голос, фото, музыка (синтезатор WebAudio),
видео, игры «Пузыри» (PS Vita) и «Змейка», браузер, магазин, файлы, терминал
(`neofetch` попробуйте), заметки, профиль и трофеи.

## 🏗 Собрать настоящий ISO

```bash
sh os/build-iso.sh      # нужен docker -> dist/xrossos-0.1.iso
dd if=dist/xrossos-0.1.iso of=/dev/sdX bs=4M status=progress
```
Как устроено и почему полноэкранность гарантирует сам композитор Cage —
в [docs/architecture.md](docs/architecture.md).

## 📂 Структура
```
shell/            оболочка XMB (ноль зависимостей, без сборки)
  index.html  css/xmb.css  js/{apps,wave,xmb,voice}.js
os/
  build-iso.sh  packages.txt  Dockerfile
  overlay/        файлы поверх Alpine: xross-session, xross-launch, OpenRC-сервис
docs/architecture.md
```

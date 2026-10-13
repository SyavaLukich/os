/* XrossOS — нативная оболочка на C / Xlib (X11 / XLibre).
 * Зависимости рантайма: только libX11 и libm. Никакого браузера и JS.
 * Целевое железо по умолчанию: ASUS X550EA (AMD A4-5000 Jaguar,
 * Radeon HD 8330, 1366x768, 4 ГБ) — см. docs/x550ea.md. */
#ifndef XROSS_H
#define XROSS_H

#define XROSS_VERSION "0.2"
#define XROSS_CODENAME "Swirl"   /* Dreamcast-референс */

/* Родное разрешение панели X550EA; на другом железе размер берётся
 * из фактического размера root-окна X-сервера. */
#define X550EA_WIDTH  1366
#define X550EA_HEIGHT 768

/* Кадровый бюджет: 60 FPS от сети, 30 FPS от батареи (Jaguar слаб). */
#define FPS_AC      60
#define FPS_BATTERY 30

/* Голосовой демон (xross-voiced, Vosk) пишет распознанные фразы сюда. */
#define VOICE_FIFO_PATH "/tmp/xross-voice.fifo"
#define NOTES_PATH      "/.xross-notes"        /* + $HOME */
#define CONFIG_PATH     "/.config/xross/xross.conf" /* + $HOME */

#endif

# XrossOS — сборка нативной оболочки (C/Xlib, без JS)
# Тюнинг под ASUS X550EA: планировщик Jaguar (mtune=btver2), но ISA -
# x86-64-v2 БЕЗ AVX: -march=btver2 давал AVX-векторизацию, и оболочка
# падала с invalid opcode на CPU без AVX (QEMU qemu64, старые хосты VM)
# именно при открытии приложений. Для Xlib-оболочки AVX ничего не даёт.
# На другой машине: make MTUNE=generic
CC      ?= cc
MARCH   ?= x86-64-v2
MTUNE   ?= btver2
CFLAGS  ?= -O2 -march=$(MARCH) -mtune=$(MTUNE) -flto -ffunction-sections -fdata-sections \
           -std=c99 -Wall -Wextra -Wno-unused-parameter -D_POSIX_C_SOURCE=200809L
LDFLAGS ?= -flto -Wl,--gc-sections -s
LDLIBS   = -lX11 -lm

PREFIX  ?= /usr
BINDIR   = $(PREFIX)/bin

all: xross

xross: xross.c xross.h
	$(CC) $(CFLAGS) -o $@ xross.c $(LDFLAGS) $(LDLIBS)

# Голосовой демон (Vosk подгружается через dlopen, сборка без vosk в системе)
voice: xross-voiced
xross-voiced: xross-voiced.c
	$(CC) $(CFLAGS) -o $@ xross-voiced.c $(LDFLAGS) -ldl

install: xross
	install -Dm755 xross $(DESTDIR)$(BINDIR)/xross
	# install -Dm755 xross-voiced $(DESTDIR)$(BINDIR)/xross-voiced

clean:
	rm -f xross xross-voiced

.PHONY: all voice install clean

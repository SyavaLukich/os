# XrossOS — сборка нативной оболочки (C/Xlib, без JS)
# По умолчанию тюнинг под ASUS X550EA: AMD Jaguar (btver2), -O2, LTO.
# На другой машине: make TARGET_CPU=generic
CC      ?= cc
TARGET_CPU ?= btver2
CFLAGS  ?= -O2 -march=$(TARGET_CPU) -mtune=$(TARGET_CPU) -flto -ffunction-sections -fdata-sections \
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

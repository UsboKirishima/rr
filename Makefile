# rr - Lightweight terminal user interface EPUB reader
# Pure C implementation with libzip, libxml2, and ncursesw

CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2
CPPFLAGS ?= -I.

# Package dependencies
PKGS = libxml-2.0 libzip ncursesw
PKG_CFLAGS = $(shell pkg-config --cflags $(PKGS))
PKG_LIBS = $(shell pkg-config --libs $(PKGS))

ALL_CFLAGS = $(CFLAGS) $(CPPFLAGS) $(PKG_CFLAGS)
ALL_LIBS = $(PKG_LIBS) -lm

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

SRCS = rr.c \
       src/util.c \
       src/epub.c \
       src/html.c \
       src/layout.c \
       src/state.c \
       src/ui.c

OBJS = $(SRCS:.c=.o)
TARGET = rr

.PHONY: all debug clean install uninstall test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(ALL_CFLAGS) -o $@ $(OBJS) $(ALL_LIBS)

%.o: %.c
	$(CC) $(ALL_CFLAGS) -c $< -o $@

debug: CFLAGS = -std=c99 -Wall -Wextra -pedantic -g3 -O0 -fsanitize=address,undefined
debug: $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET) a.out

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

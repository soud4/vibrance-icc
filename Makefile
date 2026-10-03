# vibrance-icc - Makefile
# Copyright (C) 2026 soud4. GPL-3.0-or-later; see LICENSE.

CC      ?= cc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra -pedantic
LDLIBS  := -lm
PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

BIN := vibrance-icc
OBJ := src/main.o src/color.o src/icc.o

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/main.o: src/main.c src/color.h src/icc.h
src/color.o: src/color.c src/color.h
src/icc.o: src/icc.c src/icc.h src/color.h

check: $(BIN)
	./$(BIN) --selftest 1.0
	./$(BIN) --selftest 1.4

clean:
	rm -f $(BIN) $(OBJ)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

.PHONY: all check clean install uninstall

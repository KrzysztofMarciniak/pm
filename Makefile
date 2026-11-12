# pm - C99 project scaffolder + libpm
#
#   make              build build/pm and build/libpm.a
#   make test         unit tests + scaffold tests for every build system
#   make add-licenses   download licenses from the net into the pack (LICENSES="MIT ISC ...", default: refresh all)
#   make test-licenses  fetch SPDX license texts from the net, compare to the pack (needs network)
#   make install      into $(PREFIX), default /usr/local
#   make CC=clang    optional: choose another system C compiler
-include config.mk

ifeq ($(origin CC),default)
CC := cc
endif
AR       ?= ar
PREFIX   ?= /usr/local
CFLAGS   ?= -O2
WARN     ?= -Wall -Wextra
STD      ?= -std=c99

CPPFLAGS += -Iinclude -Ibuild/gen
ALL_C     = $(STD) $(WARN) $(CPPFLAGS) $(CFLAGS)

LIB_SRC := src/pm.c src/toolchain.c src/gen.c
LIB_OBJ := $(LIB_SRC:src/%.c=build/%.o) build/gen/default_pack.o
LIB     := build/libpm.a
BIN     := build/pm

.DEFAULT_GOAL := all
.PHONY: all test test-licenses add-licenses install uninstall clean help

all: $(BIN) $(LIB)

build/embed: tools/embed.c
	@mkdir -p build
	$(CC) $(STD) -O0 -o $@ $<

build/gen/default_pack.c: templates/default.pack build/embed
	@mkdir -p build/gen
	build/embed templates/default.pack pm_default_pack > $@

build/gen/default_pack.o: build/gen/default_pack.c
	$(CC) $(STD) -O2 -c $< -o $@

build/%.o: src/%.c src/pm_int.h include/pm/pm.h
	@mkdir -p build
	$(CC) $(ALL_C) -D_POSIX_C_SOURCE=200809L -c $< -o $@

$(LIB): $(LIB_OBJ)
	$(AR) rcs $@ $^

$(BIN): build/main.o $(LIB)
	$(CC) $(ALL_C) -o $@ build/main.o $(LIB) $(LDFLAGS)

build/test_lib: tests/test_lib.c $(LIB)
	$(CC) $(ALL_C) -o $@ tests/test_lib.c $(LIB) $(LDFLAGS)

test: $(BIN) build/test_lib
	./build/test_lib
	sh tests/scaffold.sh ./$(BIN)

add-licenses:
	sh tools/add_licenses.sh templates/default.pack $(LICENSES)

test-licenses:
	sh tests/test_licenses.sh templates/default.pack

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib/pkgconfig \
	  $(DESTDIR)$(PREFIX)/include/pm $(DESTDIR)$(PREFIX)/share/pm
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/pm
	install -m 644 $(LIB) $(DESTDIR)$(PREFIX)/lib/libpm.a
	install -m 644 include/pm/pm.h $(DESTDIR)$(PREFIX)/include/pm/pm.h
	install -m 644 templates/default.pack $(DESTDIR)$(PREFIX)/share/pm/default.pack
	printf 'prefix=%s\nName: pm\nDescription: C/C++ project scaffolding library\nVersion: 2.0.1\nCflags: -I$${prefix}/include\nLibs: -L$${prefix}/lib -lpm\n' '$(PREFIX)' \
	  > $(DESTDIR)$(PREFIX)/lib/pkgconfig/pm.pc

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/pm $(DESTDIR)$(PREFIX)/lib/libpm.a \
	  $(DESTDIR)$(PREFIX)/lib/pkgconfig/pm.pc
	rm -rf $(DESTDIR)$(PREFIX)/include/pm $(DESTDIR)$(PREFIX)/share/pm

clean:
	rm -rf build

help:
	@echo "targets: all test install uninstall clean   (PREFIX=$(PREFIX))"

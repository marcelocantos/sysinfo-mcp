# Copyright 2026 Marcelo Cantos
# SPDX-License-Identifier: Apache-2.0

CC      ?= clang
CFLAGS  := -Wall -Wextra -Werror -O2 -std=c17
LDFLAGS := -framework IOKit -framework CoreFoundation

PREFIX  ?= /usr/local
BINDIR  := $(PREFIX)/bin

SRC     := main.c vendor/cjson/cJSON.c
BIN     := sysinfo-mcp

.PHONY: all clean install

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(BIN)

install: $(BIN)
	install -d $(BINDIR)
	install -m 755 $(BIN) $(BINDIR)/$(BIN)

# Copyright 2026 Marcelo Cantos
# SPDX-License-Identifier: Apache-2.0

CC      ?= clang
CFLAGS  := -Wall -Wextra -Werror -O2 -std=c17
LDFLAGS := -framework IOKit -framework CoreFoundation \
           -framework SystemConfiguration \
           -framework ApplicationServices -framework CoreVideo

# Auto-wrap CC with ccache if installed. Disable with CCACHE=no.
CCACHE ?= $(shell command -v ccache 2>/dev/null)
ifneq ($(CCACHE),)
ifneq ($(CCACHE),no)
CC := $(CCACHE) $(CC)
endif
endif

PREFIX  ?= /usr/local
BINDIR  := $(PREFIX)/bin

SRC     := main.c vendor/cjson/cJSON.c
BIN     := sysinfo-mcp

.PHONY: all clean install test bullseye

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(BIN)

install: $(BIN)
	install -d $(BINDIR)
	install -m 755 $(BIN) $(BINDIR)/$(BIN)

# Smoke-test the binary by driving it with JSON-RPC over stdio and
# checking the display category contract: ≥1 entry, exactly one main,
# positive refresh rates, stable shape. The display checks need a
# window-server session; without one CoreGraphics reports no displays.
# check-agent-guide asserts the binary's --help-agent text still matches
# docs/agents-guide.md.
test: $(BIN) tests/run.sh tests/check-agent-guide.sh
	@./tests/check-agent-guide.sh
	@./tests/run.sh

bullseye: $(BIN) test
	@echo "✓ build"
	@echo "✓ tests"
	@test -z "$$(git status --porcelain)" && echo "✓ clean tree" || \
	 (echo "✗ dirty tree"; git status --short; exit 1)

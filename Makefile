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

.PHONY: all clean install test bullseye analyze

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
# positive refresh rates, stable shape. Also the power category: when
# has_battery is true, battery_health_percent is present and in (0, 200].
test: $(BIN) tests/run.sh
	@./tests/run.sh

# clang --analyze exits 0 even when it emits RetainCount warnings, so the
# recipe greps the text output and fails on any match.
analyze:
	@out=$$(clang --analyze -Xanalyzer -analyzer-output=text main.c 2>&1); \
	printf '%s\n' "$$out"; \
	rm -f main.plist; \
	if printf '%s\n' "$$out" | grep -q RetainCount; then \
		echo "FAIL: RetainCount warning(s) from clang --analyze" >&2; \
		exit 1; \
	fi

bullseye: $(BIN) test analyze
	@echo "✓ build"
	@echo "✓ tests"
	@test -z "$$(git status --porcelain)" && echo "✓ clean tree" || \
	 (echo "✗ dirty tree"; git status --short; exit 1)

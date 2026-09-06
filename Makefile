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

.PHONY: all clean install test bullseye gate hooks

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(BIN)

install: $(BIN)
	install -d $(BINDIR)
	install -m 755 $(BIN) $(BINDIR)/$(BIN)

# Smoke-test the binary by driving it with JSON-RPC over stdio and checking
# the display category's INVARIANTS: the call succeeds, `display` is an array,
# and every entry present is well-formed. "At least one display" is host state,
# not an invariant — CGGetActiveDisplayList reports nothing while the screens
# are asleep — so it is opt-in via SYSINFO_REQUIRE_DISPLAY=1 and declared as
# residue otherwise. See docs/gate.md.
test: $(BIN) tests/run.sh
	@./tests/run.sh

# The pre-push gate (scripts/hooks/pre-push runs this; CI runs the same
# recipe). Existing oracles only — the -Werror build and the stdio smoke
# suite. Exit code is the signal: 0 means all green; the residue ledger the
# suite prints says what that green did not cover.
gate: $(BIN) test
	@echo "✓ build"
	@echo "✓ tests (residue ledger above)"

# Wire the pre-push gate into this clone. A relative core.hooksPath
# resolves against whichever worktree the push runs from, so one setting
# covers every `git worktree`.
hooks:
	git config core.hooksPath scripts/hooks
	@echo "✓ core.hooksPath = scripts/hooks"

# Standing invariants for bullseye_convergence: the gate plus a clean-tree
# check that only makes sense interactively.
bullseye: gate
	@test -z "$$(git status --porcelain)" && echo "✓ clean tree" || \
	 (echo "✗ dirty tree"; git status --short; exit 1)

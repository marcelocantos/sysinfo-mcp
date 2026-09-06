#!/bin/bash
# Copyright 2026 Marcelo Cantos
# SPDX-License-Identifier: Apache-2.0
#
# The agent guide exists twice: docs/agents-guide.md, which people and agents
# read in the repo, and AGENT_GUIDE[] in main.c, which the shipped binary
# prints for --help-agent. They drifted once already (frameworks list, display
# wording, stale line numbers), so this asserts they are the same text.
#
# docs/agents-guide.md is the source. To update the copy, regenerate the
# AGENT_GUIDE[] literal in main.c from it, rebuild, and run this.

set -euo pipefail

cd "$(dirname "$0")/.."
BIN=./sysinfo-mcp
GUIDE=docs/agents-guide.md

if [[ ! -x "$BIN" ]]; then
    echo "FAIL: $BIN not built; run make first" >&2
    exit 1
fi

embedded="$(mktemp)"
trap 'rm -f "$embedded"' EXIT

# --help-agent prints the usage summary, a blank line, then the guide.
"$BIN" --help-agent | sed -n "/^# Agent's Guide: sysinfo-mcp\$/,\$p" > "$embedded"

if ! diff -u "$GUIDE" "$embedded"; then
    echo "FAIL: --help-agent output differs from $GUIDE" >&2
    exit 1
fi

echo "ok: --help-agent matches $GUIDE"

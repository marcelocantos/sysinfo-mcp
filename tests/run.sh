#!/bin/bash
# Copyright 2026 Marcelo Cantos
# SPDX-License-Identifier: Apache-2.0
#
# Smoke test: drive sysinfo-mcp over stdio with JSON-RPC, validate
# the display category honours its contract (≥1 entry, exactly one
# main, positive refresh rates, stable shape).

set -euo pipefail

cd "$(dirname "$0")/.."
BIN=./sysinfo-mcp

if ! command -v jq >/dev/null; then
    echo "FAIL: jq is required for tests" >&2
    exit 1
fi

raw=$(printf '%s\n' \
    '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
    '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"system_info","arguments":{"categories":["display"]}}}' \
    | "$BIN" | tail -1)

displays=$(printf '%s' "$raw" | jq -r '.result.content[0].text' | jq '.display')

count=$(printf '%s' "$displays" | jq 'length')
if [[ "$count" -lt 1 ]]; then
    echo "FAIL: expected ≥1 display, got $count" >&2
    echo "$raw" >&2
    exit 1
fi

mains=$(printf '%s' "$displays" | jq '[.[] | select(.main == true)] | length')
if [[ "$mains" -ne 1 ]]; then
    echo "FAIL: expected exactly one main display, got $mains" >&2
    exit 1
fi

bad_hz=$(printf '%s' "$displays" | jq '[.[] | select(.refresh_hz != null and .refresh_hz <= 0)] | length')
if [[ "$bad_hz" -ne 0 ]]; then
    echo "FAIL: $bad_hz display(s) reported non-positive refresh_hz" >&2
    exit 1
fi

# Shape check: every entry must have id, main, connection, resolution_pixels,
# resolution_logical, scale.
missing=$(printf '%s' "$displays" | jq '
    [.[] | select(
        (.id          | type) != "number" or
        (.main        | type) != "boolean" or
        (.connection  | type) != "string" or
        (.resolution_pixels  | type) != "array" or (.resolution_pixels  | length) != 2 or
        (.resolution_logical | type) != "array" or (.resolution_logical | length) != 2 or
        (.scale       | type) != "number"
    )] | length')
if [[ "$missing" -ne 0 ]]; then
    echo "FAIL: $missing display entr(y/ies) have an invalid shape" >&2
    printf '%s' "$displays" | jq . >&2
    exit 1
fi

echo "ok: $count display(s), $mains main, all shapes valid"

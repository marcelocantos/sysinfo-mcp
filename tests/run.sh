#!/bin/bash
# Copyright 2026 Marcelo Cantos
# SPDX-License-Identifier: Apache-2.0
#
# Smoke test: drive sysinfo-mcp over stdio with JSON-RPC and validate the
# display category's contract.
#
# The contract splits in two, and the split is the whole point of this file.
#
#   Invariants — true on any host, in any state. The call succeeds, `display`
#   is an array, and every element that IS present is well-formed. These are
#   asserted unconditionally, and they are vacuously satisfied by an empty
#   array, which is exactly why the ledger below has to say so out loud.
#
#   Host state — "there is at least one display". CGGetActiveDisplayList
#   reports nothing while the screens are asleep, so the same unchanged binary
#   returns two displays at 2pm and zero at 2am. Asserting it unconditionally
#   made the pre-push gate refuse pushes for a reason that had nothing to do
#   with the commit, and a gate that fails environmentally is a gate people
#   learn to bypass. It now runs only under SYSINFO_REQUIRE_DISPLAY=1, and
#   when that is absent the run PRINTS the assertion it did not exercise.
#
# Usage:
#     tests/run.sh                          invariants only, residue declared
#     SYSINFO_REQUIRE_DISPLAY=1 tests/run.sh   also require ≥1 display

set -euo pipefail

cd "$(dirname "$0")/.."
BIN=./sysinfo-mcp

if ! command -v jq >/dev/null; then
    echo "FAIL: jq is required for tests" >&2
    exit 1
fi

require_display="${SYSINFO_REQUIRE_DISPLAY:-0}"

# Residue accumulates as "<family>|<detail>" lines in a temp file. macOS bash
# is 3.2: no associative arrays, and appending to a plain array under `set -u`
# is fine but a file keeps the ledger printable in one pass.
residue="$(mktemp)"
trap 'rm -f "$residue"' EXIT
declare_residue() { printf '%s|%s\n' "$1" "$2" >> "$residue"; }

raw=$(printf '%s\n' \
    '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' \
    '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"system_info","arguments":{"categories":["display"]}}}' \
    | "$BIN" | tail -1)

# Invariant 1: the JSON-RPC call succeeded. Checked explicitly, because with
# the presence assertion gone a server that returns an error would otherwise
# reach the array checks as an empty list and pass them all.
if ! printf '%s' "$raw" | jq -e '.result.content[0].text' >/dev/null 2>&1; then
    echo "FAIL: tools/call returned no result.content[0].text" >&2
    echo "$raw" >&2
    exit 1
fi

payload=$(printf '%s' "$raw" | jq -r '.result.content[0].text')
if ! printf '%s' "$payload" | jq -e . >/dev/null 2>&1; then
    echo "FAIL: result text is not valid JSON" >&2
    echo "$payload" >&2
    exit 1
fi

displays=$(printf '%s' "$payload" | jq '.display')

# Invariant 2: `display` is an array. `jq length` answers 0 for null as
# happily as for [], so the type has to be asserted, not inferred.
kind=$(printf '%s' "$displays" | jq -r 'type')
if [[ "$kind" != "array" ]]; then
    echo "FAIL: .display is $kind, expected array" >&2
    echo "$payload" >&2
    exit 1
fi

count=$(printf '%s' "$displays" | jq 'length')

# Host state: at least one display. Opt-in only.
if [[ "$require_display" == "1" ]]; then
    if [[ "$count" -lt 1 ]]; then
        echo "FAIL: SYSINFO_REQUIRE_DISPLAY=1 but got $count display(s)." >&2
        echo "      CGGetActiveDisplayList reports nothing while the screens" >&2
        echo "      are asleep — wake them, or run without the opt-in." >&2
        echo "$raw" >&2
        exit 1
    fi
else
    declare_residue "display presence" \
        "\"≥1 display\" not asserted (host state, not a code invariant); set SYSINFO_REQUIRE_DISPLAY=1 with the screens awake to exercise it"
fi

# Invariant 3: at most one display is main, and exactly one when any display
# is present. The "exactly one" half is host-state-dependent for the same
# reason as the count: zero displays means zero mains.
mains=$(printf '%s' "$displays" | jq '[.[] | select(.main == true)] | length')
if [[ "$count" -gt 0 ]]; then
    if [[ "$mains" -ne 1 ]]; then
        echo "FAIL: expected exactly one main display among $count, got $mains" >&2
        exit 1
    fi
elif [[ "$mains" -ne 0 ]]; then
    echo "FAIL: $mains main display(s) in an empty display array" >&2
    exit 1
fi

# Invariant 4: no element reports a non-positive refresh rate.
bad_hz=$(printf '%s' "$displays" | jq '[.[] | select(.refresh_hz != null and .refresh_hz <= 0)] | length')
if [[ "$bad_hz" -ne 0 ]]; then
    echo "FAIL: $bad_hz display(s) reported non-positive refresh_hz" >&2
    exit 1
fi

# Invariant 5: every element carries id, main, connection, resolution_pixels,
# resolution_logical, scale, with the right types.
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

# Invariants 3-5 quantify over the array. An empty array satisfies all of them
# without testing a line of the per-display collector, so an empty run is a
# far weaker green than a populated one and must not read the same.
if [[ "$count" -eq 0 ]]; then
    declare_residue "per-display invariants" \
        "main-display flag, refresh_hz and entry shape were vacuously true — the array was empty, so collect_display()'s per-display body did not run"
fi

# Categories the binary serves that this suite never calls. Naming them here
# is what stops the ledger from being a display-only formality: each is a
# would-be host-state trap of the same shape (a battery-less Mac mini, a
# machine with no active network interface, thermal sensors that report
# nothing under Rosetta), and the fix for each is this same split rather than
# an unconditional presence assertion.
declare_residue "untested categories" \
    "cpu, memory, gpu, disk, os, network, power, thermal are collected by the server and asserted by nothing here"

echo "ok: $count display(s), $mains main, all present entries well-formed"

if [[ -s "$residue" ]]; then
    echo "residue (declared, not exercised on this host):"
    while IFS='|' read -r family detail; do
        printf '  %-22s %s\n' "$family" "$detail"
    done < "$residue"
fi

exercised="invariants"
[[ "$require_display" == "1" ]] && exercised="invariants + display presence"
printf '✓ display contract: %s (%s residue line(s) declared)\n' \
    "$exercised" "$(wc -l < "$residue" | tr -d ' ')"

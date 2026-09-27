#!/bin/bash
# Copyright 2026 Marcelo Cantos
# SPDX-License-Identifier: Apache-2.0
#
# Smoke test: drive sysinfo-mcp over stdio with JSON-RPC and check each
# system_info category. Fields this host cannot expose are skipped.
# Values are never invented.

set -euo pipefail

cd "$(dirname "$0")/.."
BIN=./sysinfo-mcp

if ! command -v jq >/dev/null; then
    echo "FAIL: jq is required for tests" >&2
    exit 1
fi

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

raw=$(
    {
        printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
        printf '%s\n' \
            '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"system_info","arguments":{"categories":["cpu"]}}}' \
            '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"system_info","arguments":{"categories":["memory"]}}}' \
            '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"system_info","arguments":{"categories":["gpu"]}}}' \
            '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"system_info","arguments":{"categories":["disk"]}}}' \
            '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"system_info","arguments":{"categories":["os"]}}}' \
            '{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"system_info","arguments":{"categories":["network"]}}}' \
            '{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"system_info","arguments":{"categories":["power"]}}}' \
            '{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"system_info","arguments":{"categories":["thermal"]}}}' \
            '{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"system_info","arguments":{"categories":["display"]}}}'
    } | "$BIN"
)

text_of() {
    local id="$1"
    local line
    line=$(printf '%s\n' "$raw" | jq -c --argjson id "$id" 'select(.id == $id)')
    if [[ -z "$line" ]]; then
        fail "no JSON-RPC response for id $id"
    fi
    if printf '%s' "$line" | jq -e '.error != null' >/dev/null; then
        fail "rpc id $id returned an error: $line"
    fi
    printf '%s' "$line" | jq -er '.result.content[0].text'
}

# require <json> <label> [jq args...] <filter>
require() {
    local json="$1"
    local label="$2"
    shift 2
    if ! printf '%s' "$json" | jq -e "$@" >/dev/null; then
        echo "FAIL: $label" >&2
        printf '%s' "$json" | jq . >&2
        exit 1
    fi
}

# --- cpu ---
cpu=$(text_of 2)
require "$cpu" "cpu shape" '
    (.cpu.physical_cores | type) == "number" and .cpu.physical_cores > 0 and
    (.cpu.logical_cores | type) == "number" and .cpu.logical_cores >= .cpu.physical_cores and
    (
        (.cpu.performance_cores == null and .cpu.efficiency_cores == null) or
        (
            (.cpu.performance_cores | type) == "number" and
            (.cpu.efficiency_cores | type) == "number" and
            .cpu.performance_cores >= 0 and .cpu.efficiency_cores >= 0 and
            (.cpu.performance_cores + .cpu.efficiency_cores) == .cpu.logical_cores
        )
    ) and
    (.cpu.brand == null or ((.cpu.brand | type) == "string" and (.cpu.brand | length) > 0)) and
    (.cpu.frequency_hz == null or ((.cpu.frequency_hz | type) == "number" and .cpu.frequency_hz > 0))
'
cores=$(printf '%s' "$cpu" | jq -r '.cpu | "\(.physical_cores)p/\(.logical_cores)l"')
echo "ok: cpu $cores"

# --- memory ---
mem=$(text_of 3)
require "$mem" "memory shape" '
    (.memory.total_bytes | type) == "number" and .memory.total_bytes > 0 and
    (.memory.free_bytes | type) == "number" and .memory.free_bytes >= 0 and
    (.memory.used_bytes | type) == "number" and .memory.used_bytes >= 0 and
    (.memory.compressed_bytes | type) == "number" and .memory.compressed_bytes >= 0
'
echo "ok: memory total_bytes=$(printf '%s' "$mem" | jq -r '.memory.total_bytes')"

# --- gpu ---
gpu=$(text_of 4)
require "$gpu" "gpu shape" '
    (.gpu | type) == "array" and
    all(.gpu[];
        ((.model == null) or ((.model | type) == "string" and (.model | length) > 0)) and
        ((.vram_mb == null) or ((.vram_mb | type) == "number" and .vram_mb > 0)) and
        ((.core_count == null) or ((.core_count | type) == "number" and .core_count > 0)) and
        (.model != null or .vram_mb != null or .core_count != null)
    )
'
gpu_n=$(printf '%s' "$gpu" | jq '.gpu | length')
if [[ "$gpu_n" -eq 0 ]]; then
    echo "skip: gpu returned no entries with recognized fields"
else
    echo "ok: gpu $gpu_n entr$([[ "$gpu_n" -eq 1 ]] && echo y || echo ies)"
fi

# --- disk ---
disk=$(text_of 5)
require "$disk" "disk shape" '
    (.disk | type) == "array" and
    any(.disk[]; .mount == "/") and
    all(.disk[] | select(.mount == "/");
        (.total_bytes | type) == "number" and .total_bytes > 0 and
        (.free_bytes | type) == "number" and .free_bytes >= 0 and
        (.used_bytes | type) == "number" and .used_bytes >= 0 and
        ((.used_bytes + .free_bytes - .total_bytes) | fabs) < 1
    )
'
echo "ok: disk / present"

# --- os ---
os=$(text_of 6)
require "$os" "os shape" '
    .os.sysname == "Darwin" and
    (.os.release | type) == "string" and (.os.release | length) > 0 and
    (.os.version | type) == "string" and (.os.version | length) > 0 and
    (.os.machine | type) == "string" and (.os.machine | length) > 0 and
    (.os.macos_version | type) == "string" and (.os.macos_version | test("^[0-9]+(\\.[0-9]+)*$")) and
    (.os.hostname | type) == "string" and (.os.hostname | length) > 0 and
    (.os.boot_time_unix | type) == "number" and .os.boot_time_unix > 0
'
echo "ok: os $(printf '%s' "$os" | jq -r '.os | "\(.sysname) \(.macos_version) \(.machine)"')"

# --- network ---
net=$(text_of 7)
require "$net" "network shape" '
    (.network | type) == "array" and (.network | length) >= 1 and
    all(.network[];
        (.name | type) == "string" and (.name | length) > 0 and
        (.ipv4 == null or (.ipv4 | type) == "string") and
        (.ipv6 == null or ((.ipv6 | type) == "string" and (.ipv6 | startswith("fe80:") | not))) and
        (.mac == null or (.mac | type) == "string") and
        (.primary == null or .primary == true) and
        (.router == null or ((.router | type) == "string" and (.router | length) > 0))
    )
'

scutil_ipv4=$(echo "show State:/Network/Global/IPv4" | scutil || true)
primary_if=$(printf '%s\n' "$scutil_ipv4" | sed -n 's/^ *PrimaryInterface : *//p' | head -1)
expected_router=$(printf '%s\n' "$scutil_ipv4" | sed -n 's/^ *Router : *//p' | head -1)

if [[ -z "$primary_if" ]]; then
    echo "skip: network has no SCDynamicStore primary interface"
else
    primary_n=$(printf '%s' "$net" | jq --arg n "$primary_if" '[.network[] | select(.name == $n)] | length')
    if [[ "$primary_n" -eq 0 ]]; then
        echo "skip: primary interface $primary_if is not in the reported set"
    else
        require "$net" "network primary flag on $primary_if" --arg n "$primary_if" '
            [.network[] | select(.name == $n and .primary == true)] | length == 1
        '
        if [[ -z "$expected_router" ]]; then
            echo "skip: primary $primary_if has no Router string"
        else
            require "$net" "network router for $primary_if (expected $expected_router)" \
                --arg n "$primary_if" --arg r "$expected_router" '
                [.network[] | select(.name == $n and .router == $r)] | length == 1
            '
            echo "ok: network primary $primary_if router $expected_router"
        fi
    fi
fi

# --- power ---
power=$(text_of 8)
require "$power" "power shape" '
    (.power.has_battery | type) == "boolean" and
    (.power.power_source == "ac" or .power.power_source == "battery") and
    (.power.battery_percent == null or (
        (.power.battery_percent | type) == "number" and
        .power.battery_percent >= 0 and .power.battery_percent <= 100
    )) and
    (.power.battery_temperature_c == null or (
        (.power.battery_temperature_c | type) == "number" and
        .power.battery_temperature_c > 0 and .power.battery_temperature_c < 80
    ))
'

has_battery=$(printf '%s' "$power" | jq -r '.power.has_battery')
# Top-level AppleSmartBattery Temperature only. Nested lifetime stats use
# different keys (MaximumTemperature, …) and are not the live reading.
temp_raw=$(ioreg -r -n AppleSmartBattery -d 1 2>/dev/null | sed -n 's/.*"Temperature" = \([0-9][0-9]*\).*/\1/p' | head -1 || true)
if [[ "$has_battery" != "true" ]]; then
    require "$power" "battery_temperature_c present without a battery" \
        '.power.battery_temperature_c == null'
    echo "skip: power has no battery; battery_temperature_c omitted"
elif [[ -z "$temp_raw" ]]; then
    require "$power" "battery_temperature_c present but IOKit Temperature is absent" \
        '.power.battery_temperature_c == null'
    echo "skip: AppleSmartBattery Temperature key is absent"
else
    expected_c=$(awk -v d="$temp_raw" 'BEGIN { printf "%.2f", d / 10.0 - 273.15 }')
    require "$power" "battery_temperature_c near IOKit Temperature $temp_raw ($expected_c °C)" \
        --argjson expected "$expected_c" '
        (.power.battery_temperature_c | type) == "number" and
        ((.power.battery_temperature_c - $expected) | fabs) < 1.5
    '
    echo "ok: power battery_temperature_c=$(printf '%s' "$power" | jq -r '.power.battery_temperature_c')"
fi

# --- thermal ---
thermal=$(text_of 9)
require "$thermal" "thermal object" '(.thermal | type) == "object"'
if sysctl -n kern.thermalpressure >/dev/null 2>&1; then
    require "$thermal" "thermal pressure" '
        .thermal.pressure == "nominal" or .thermal.pressure == "moderate" or
        .thermal.pressure == "heavy" or .thermal.pressure == "critical" or
        .thermal.pressure == "unknown"
    '
    echo "ok: thermal pressure=$(printf '%s' "$thermal" | jq -r '.thermal.pressure')"
else
    require "$thermal" "thermal pressure absent when sysctl is missing" \
        '.thermal.pressure == null'
    echo "skip: kern.thermalpressure unavailable"
fi

# --- display ---
displays=$(text_of 10 | jq '.display')
count=$(printf '%s' "$displays" | jq 'length')
if [[ "$count" -lt 1 ]]; then
    fail "expected ≥1 display, got $count"
fi

mains=$(printf '%s' "$displays" | jq '[.[] | select(.main == true)] | length')
if [[ "$mains" -ne 1 ]]; then
    fail "expected exactly one main display, got $mains"
fi

bad_hz=$(printf '%s' "$displays" | jq '[.[] | select(.refresh_hz != null and .refresh_hz <= 0)] | length')
if [[ "$bad_hz" -ne 0 ]]; then
    fail "$bad_hz display(s) reported non-positive refresh_hz"
fi

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
    fail "$missing display entr(y/ies) have an invalid shape"
    printf '%s' "$displays" | jq . >&2
fi

echo "ok: $count display(s), $mains main, all shapes valid"

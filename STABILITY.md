# Stability

## Commitment

Version 1.0.0 marks the backwards-compatibility contract. After 1.0, no
breaking changes will be made to the public interaction surface. Changes that
would break existing integrations require forking to a new product (new binary
name, new server name, new repository). Additive changes (new categories, new
optional fields) are not breaking.

---

## Interaction surface catalogue

### MCP protocol

| Item | Value | Status |
|---|---|---|
| Transport | stdio, line-delimited JSON-RPC | Stable |
| Protocol version | `2024-11-05` | Stable |
| Method: `initialize` | Returns `serverInfo`, `capabilities`, `protocolVersion` | Stable |
| Method: `notifications/initialized` | Accepted; no response | Stable |
| Method: `tools/list` | Returns tool schema array | Stable |
| Method: `tools/call` | Dispatches named tool | Stable |
| Server name | `sysinfo` | Stable |
| Server version | `0.1.0` | Fluid (increments on release) |
| Error code `-32700` | Parse error | Stable |
| Error code `-32600` | Invalid request | Stable |
| Error code `-32601` | Method not found | Stable |
| Error code `-32602` | Invalid params | Stable |

### Tool: `system_info`

| Item | Value | Status |
|---|---|---|
| Tool name | `system_info` | Stable |
| Parameter: `categories` | Optional. Array of strings. Omit for all. | Stable |
| Category enum values | `cpu`, `memory`, `gpu`, `disk`, `os`, `network`, `power`, `thermal` | Stable |
| Unknown category value | Silently ignored (no error) | Needs review |
| Result format | `content` array with single `type: "text"` item containing JSON | Stable |
| `isError` flag on unknown tool | Present and `true` | Stable |

### Output fields: `cpu`

The `cpu` key holds a JSON object.

| Field | Type | Condition | Status |
|---|---|---|---|
| `physical_cores` | number (integer) | Always | Stable |
| `logical_cores` | number (integer) | Always | Stable |
| `performance_cores` | number (integer) | Apple Silicon only (hw.nperflevels > 0) | Stable |
| `efficiency_cores` | number (integer) | Apple Silicon only (hw.nperflevels > 0) | Stable |
| `brand` | string | When `machdep.cpu.brand_string` is available | Stable |
| `frequency_hz` | number | When `hw.cpufrequency` > 0 (absent on Apple Silicon) | Needs review |

### Output fields: `memory`

The `memory` key holds a JSON object.

| Field | Type | Condition | Status |
|---|---|---|---|
| `total_bytes` | number | Always | Stable |
| `free_bytes` | number | When `host_statistics64` succeeds | Stable |
| `used_bytes` | number | When `host_statistics64` succeeds | Stable |
| `compressed_bytes` | number | When `host_statistics64` succeeds | Stable |

`used_bytes` = (active + wired + compressor) pages * page_size.
`free_bytes` = (free + inactive) pages * page_size.
These definitions are subject to refinement before 1.0.

### Output fields: `gpu`

The `gpu` key holds a JSON array. Each element is an object.

| Field | Type | Condition | Status |
|---|---|---|---|
| `model` | string | When IOKit `model` property is available | Stable |
| `vram_mb` | number | When `VRAM,totalMB` > 0 (discrete GPUs) | Stable |
| `core_count` | number | When `gpu-core-count` > 0 (Apple Silicon) | Stable |

Entries with no recognized fields are omitted from the array. Array order
follows IOKit enumeration order, which is not guaranteed to be stable across
reboots.

### Output fields: `disk`

The `disk` key holds a JSON array. Each element is an object.

| Field | Type | Condition | Status |
|---|---|---|---|
| `mount` | string | Always | Stable |
| `total_bytes` | number | Always | Stable |
| `free_bytes` | number | Always | Stable |
| `used_bytes` | number | Always | Stable |

Currently reports only `/`. See gaps below.

### Output fields: `os`

The `os` key holds a JSON object.

| Field | Type | Condition | Status |
|---|---|---|---|
| `sysname` | string | When `uname()` succeeds | Stable |
| `release` | string | When `uname()` succeeds | Stable |
| `version` | string | When `uname()` succeeds | Stable |
| `machine` | string | When `uname()` succeeds | Stable |
| `macos_version` | string | When `kern.osproductversion` is available | Stable |
| `hostname` | string | When `kern.hostname` is available | Stable |
| `boot_time_unix` | number (Unix epoch, seconds) | When `kern.boottime` is available | Stable |

`version` is the full kernel version string (Darwin build tag), not the
macOS marketing version. `macos_version` is the product version (e.g. `15.4`).

### Output fields: `network`

The `network` key holds a JSON array. Each element represents one network
interface.

| Field | Type | Condition | Status |
|---|---|---|---|
| `name` | string | Always | Stable |
| `ipv4` | string | When AF_INET address is present | Stable |
| `ipv6` | string | First non-link-local AF_INET6 address | Needs review |
| `mac` | string (xx:xx:xx:xx:xx:xx) | When AF_LINK with 6-byte hardware address, non-zero | Stable |
| `primary` | boolean (true) | On the interface identified as primary by SCDynamicStore | Needs review |
| `router` | string | When SCDynamicStore `Router` key is a CFString on the primary interface | Needs review |

Loopback and down interfaces are excluded. Only the first non-link-local IPv6
address per interface is reported. `primary` is absent (not false) on
non-primary interfaces.

`router` detection reads `SCDynamicStoreDomainState/Network/Global/IPv4 ->
Router`. The retrieval path has a logic issue in the current implementation
(conflating a CFDictionaryRef check with a CFStringRef cast); this field is
unreliable and needs review before 1.0.

### Output fields: `power`

The `power` key holds a JSON object.

| Field | Type | Condition | Status |
|---|---|---|---|
| `has_battery` | boolean | Always | Stable |
| `power_source` | string (`"ac"` or `"battery"`) | Always | Stable |
| `battery_percent` | number | When battery present and `CurrentCapacity` available | Stable |
| `capacity_mah` | number | When battery present and `AppleRawCurrentCapacity` available | Stable |
| `max_capacity_mah` | number | When battery present and `AppleRawMaxCapacity` available | Stable |
| `battery_health_percent` | number | When `DesignCapacity` > 0 and `AppleRawMaxCapacity` available | Needs review |
| `charging` | boolean | When battery present and `IsCharging` available | Stable |
| `cycle_count` | number | When battery present and `CycleCount` available | Stable |
| `time_remaining_minutes` | number | When battery present, `TimeRemaining` in [1, 5999] | Needs review |

`battery_health_percent` = `AppleRawMaxCapacity / DesignCapacity * 100`.
The formula is correct but the field name may shift to `health_percent` for
clarity before 1.0.

### Output fields: `thermal`

The `thermal` key holds a JSON object.

| Field | Type | Condition | Status |
|---|---|---|---|
| `pressure` | string | When `kern.thermalpressure` is available | Needs review |

`pressure` values: `"nominal"`, `"moderate"`, `"heavy"`, `"critical"`,
`"unknown"`. The integer-to-string mapping mirrors Apple's documented
`NSProcessInfoThermalState` levels but relies on undocumented sysctl semantics.
Needs verification against Apple documentation before 1.0.

---

## Annotation key

| Annotation | Meaning |
|---|---|
| Stable | Committed. Will not change before or after 1.0. |
| Needs review | Present and functional, but name, type, or semantics may change before 1.0. |
| Fluid | Expected to change; do not depend on current value. |

---

## Gaps and prerequisites for 1.0

The following must be resolved before 1.0 is tagged.

**CLI interface**
- No `--version`, `--help`, or `--help-agent` flags. The binary must support
  all three before 1.0 (required by project convention).

**Testing**
- No automated tests exist. A baseline test suite is required: at minimum,
  round-trip JSON-RPC tests for `initialize`, `tools/list`, and
  `tools/call` for each category.

**CI**
- No CI pipeline. A build-and-test workflow (GitHub Actions, macOS arm64) is
  required before 1.0.

**Version management**
- No version macro in source. The version string `"0.1.0"` in
  `handle_initialize()` is a bare literal. A `SYSINFO_VERSION` macro (or
  equivalent) should be the single source of truth, injectable at build time.

**Disk reporting**
- Only `/` is reported. This is a known limitation. Before 1.0, decide whether
  to enumerate all mounted filesystems (excluding pseudo-filesystems) or
  document the single-mount restriction as a permanent 1.0 constraint.

**Network: router field reliability**
- The `router` field in network output has a logic defect in the
  SCDynamicStore retrieval path. Verify and fix before 1.0 or remove the
  field.

**Thermal pressure mapping**
- The `kern.thermalpressure` integer-to-label mapping should be validated
  against Apple documentation. If the mapping is undocumented and subject to
  change, the field should return the raw integer with a documented note, or
  be removed from the stable surface.

**IPv6 handling**
- Only the first non-link-local IPv6 address per interface is reported; others
  are silently dropped. Consider whether this is the right policy or whether
  an `ipv6_addresses` array is more correct.

**Memory accounting definitions**
- The `used_bytes` and `free_bytes` definitions (active+wired+compressor vs
  free+inactive) differ from what Activity Monitor reports. Document the
  definition explicitly or align with Activity Monitor before 1.0.

---

## Out of scope for 1.0

The following are explicitly deferred. They will not block 1.0 and will not
appear in the stable surface until a subsequent release.

- Linux support (no `IOKit`, no `kern.thermalpressure`, different sysctl paths)
- Windows support
- Additional mount points beyond `/`
- Process / per-process CPU and memory statistics
- Network traffic counters (bytes in/out per interface)
- Sensor data (fan speed, per-core temperature) — no public API on macOS
- GPU utilisation / memory pressure (no public API)
- Multiple GPU entries in `gpu` array ordering guarantees
- Wi-Fi SSID reporting (requires additional entitlements on macOS)
- Power adapter wattage / USB-C power delivery details

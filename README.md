# sysinfo-mcp

A lightweight C MCP server for macOS that exposes system hardware and OS information to AI coding tools via the Model Context Protocol (stdio/JSON-RPC).

## Quick start

Install and register with Claude Code:

```bash
brew install marcelocantos/tap/sysinfo-mcp
claude mcp add --scope user sysinfo -- sysinfo-mcp
```

Restart your Claude Code session to pick up the new server.

Or tell your agent:

```
Install sysinfo-mcp from https://github.com/marcelocantos/sysinfo-mcp — brew install,
register as an MCP server, and restart the session. Follow the agents-guide.md in the repo.
```

### Alternative: build from source

```bash
make
make install          # installs to /usr/local/bin/sysinfo-mcp
claude mcp add --scope user sysinfo -- sysinfo-mcp
```

### Manual MCP client configuration

Add to your MCP client config (e.g., `~/.claude.json`):

```json
{
  "mcpServers": {
    "sysinfo": {
      "command": "sysinfo-mcp"
    }
  }
}
```

## Tool: `system_info`

Accepts an optional `categories` array. Omitting it returns all categories.

```json
{ "categories": ["cpu", "memory", "thermal"] }
```

### Categories

| Category  | Description                                      |
|-----------|--------------------------------------------------|
| `cpu`     | Core counts, brand, frequency                    |
| `memory`  | Total, free, used, and compressed RAM            |
| `gpu`     | GPU model(s), VRAM, core count                   |
| `disk`    | Capacity and usage of `/` (no other mounts)      |
| `os`      | Kernel info, macOS version, hostname, boot time  |
| `network` | Network interfaces, IPs, MACs, primary flag      |
| `power`   | Battery level, health, charging state, source    |
| `thermal` | Thermal pressure level, where the OS exposes it  |
| `display` | Per-monitor name, resolution, refresh, scale (awake screens only) |

## Output schema

### `cpu`

| Field                | Type   | Notes                        |
|----------------------|--------|------------------------------|
| `physical_cores`     | int    |                              |
| `logical_cores`      | int    |                              |
| `performance_cores`  | int    | Apple Silicon only           |
| `efficiency_cores`   | int    | Apple Silicon only           |
| `brand`              | string | Present on Apple Silicon and Intel |
| `frequency_hz`       | double | From `hw.cpufrequency`; absent on Apple Silicon |

### `memory`

| Field              | Type   | Notes |
|--------------------|--------|-------|
| `total_bytes`      | double |       |
| `free_bytes`       | double |       |
| `used_bytes`       | double |       |
| `compressed_bytes` | double |       |

### `gpu` (array)

| Field        | Type   | Notes                        |
|--------------|--------|------------------------------|
| `model`      | string |                              |
| `vram_mb`    | double | Discrete GPUs only           |
| `core_count` | double | Apple Silicon only           |

### `disk` (array)

| Field         | Type   | Notes |
|---------------|--------|-------|
| `mount`       | string | Always `"/"` — see Known limitations |
| `total_bytes` | double |       |
| `free_bytes`  | double |       |
| `used_bytes`  | double |       |

### `os`

| Field            | Type   | Notes                  |
|------------------|--------|------------------------|
| `sysname`        | string | From `uname(3)`        |
| `release`        | string | Kernel release         |
| `version`        | string | Kernel version string  |
| `machine`        | string | Architecture           |
| `macos_version`  | string | e.g. `"15.3.2"`        |
| `hostname`       | string |                        |
| `boot_time_unix` | double | Unix timestamp         |

### `network` (array of interfaces)

| Field    | Type   | Notes                                    |
|----------|--------|------------------------------------------|
| `name`   | string | Interface name, e.g. `"en0"`            |
| `ipv4`   | string |                                          |
| `ipv6`   | string | Non-link-local only; omitted if absent   |
| `mac`    | string | Omitted if all-zero                      |
| `primary`| bool   | Present and `true` on primary interface; absent elsewhere |
| `router` | string | Gateway IP. **Not currently emitted** — see Known limitations |

### `power`

| Field                   | Type   | Notes                                    |
|-------------------------|--------|------------------------------------------|
| `has_battery`           | bool   |                                          |
| `battery_percent`       | int    |                                          |
| `capacity_mah`          | int    |                                          |
| `max_capacity_mah`      | int    |                                          |
| `battery_health_percent`| double |                                          |
| `charging`              | bool   |                                          |
| `power_source`          | string | `"ac"` or `"battery"`                   |
| `cycle_count`           | int    |                                          |
| `time_remaining_minutes`| int    | Only when on battery and estimate valid  |

### `thermal`

| Field      | Type   | Notes                                              |
|------------|--------|----------------------------------------------------|
| `pressure` | string | `"nominal"`, `"moderate"`, `"heavy"`, `"critical"`, `"unknown"`. Present only when the `kern.thermalpressure` sysctl exists; the object is otherwise empty |

### `display` (array)

| Field                | Type    | Notes                                                                |
|----------------------|---------|----------------------------------------------------------------------|
| `name`               | string  | Marketing name (e.g. `"LG ULTRAFINE"`); `"Built-in Display"` when the OS doesn't expose a product name for built-in panels (Apple Silicon) |
| `id`                 | int     | `CGDirectDisplayID`                                                  |
| `main`               | bool    | `true` for the main display (exactly one entry per call)            |
| `connection`         | string  | `"internal"`, `"external"`, or `"mirrored"`                          |
| `resolution_pixels`  | [int,int] | Native pixel dimensions of the current mode                       |
| `resolution_logical` | [int,int] | UI / point dimensions of the current mode                         |
| `scale`              | double  | `pixels / logical` (typically 1, 2, or 3)                            |
| `refresh_hz`         | double  | Refresh rate; falls back to CoreVideo when CG reports 0              |
| `refresh_range_hz`   | [double,double] | Min/max only when multiple discrete refresh rates exist for the current resolution |

## Example output

```json
{
  "cpu": {
    "physical_cores": 16,
    "logical_cores": 16,
    "performance_cores": 12,
    "efficiency_cores": 4,
    "brand": "Apple M4 Max"
  },
  "memory": {
    "total_bytes": 137438953472,
    "free_bytes": 42949672960,
    "used_bytes": 85899345920,
    "compressed_bytes": 8589934592
  },
  "gpu": [
    {
      "model": "Apple M4 Max",
      "core_count": 40
    }
  ],
  "disk": [
    {
      "mount": "/",
      "total_bytes": 2000398934016,
      "free_bytes": 1234567890432,
      "used_bytes": 765831043584
    }
  ],
  "os": {
    "sysname": "Darwin",
    "release": "24.4.0",
    "version": "Darwin Kernel Version 24.4.0: ...",
    "machine": "arm64",
    "macos_version": "15.3.2",
    "hostname": "macbook.local",
    "boot_time_unix": 1743900000
  },
  "network": [
    {
      "name": "en0",
      "ipv4": "192.168.1.42",
      "ipv6": "2001:db8::1",
      "mac": "aa:bb:cc:dd:ee:ff",
      "primary": true
    },
    {
      "name": "en1",
      "mac": "aa:bb:cc:dd:ee:00"
    }
  ],
  "power": {
    "has_battery": true,
    "battery_percent": 78,
    "capacity_mah": 5603,
    "max_capacity_mah": 7580,
    "battery_health_percent": 88.35528616388856,
    "charging": false,
    "power_source": "ac",
    "cycle_count": 137
  },
  "thermal": {},
  "display": [
    {
      "name": "Built-in Display",
      "id": 1,
      "main": true,
      "connection": "internal",
      "resolution_pixels": [3456, 2234],
      "resolution_logical": [1728, 1117],
      "scale": 2,
      "refresh_hz": 120
    },
    {
      "name": "LG ULTRAFINE",
      "id": 3,
      "main": false,
      "connection": "external",
      "resolution_pixels": [6720, 3780],
      "resolution_logical": [3360, 1890],
      "scale": 2,
      "refresh_hz": 60
    }
  ]
}
```

## Known limitations

Verified against the shipped binary on macOS 26.6.2 (Apple Silicon):

- **`display` reports nothing while the screens are asleep.** With the panels
  asleep, `CGGetOnlineDisplayList` returns no displays, so `display` comes back
  `[]` and `tests/run.sh` fails with "expected ≥1 display, got 0" — even though
  `system_profiler SPDisplaysDataType` still lists them (as `Display Asleep:
  Yes`). The same binary in the same shell returned both monitors once the
  screens were awake. Wake the display before trusting this category or running
  `make test`.
- **`thermal` is empty on macOS 26.** The `pressure` field needs the
  `kern.thermalpressure` sysctl, which that release no longer provides
  (`sysctl: unknown oid`). The category returns `{}` rather than an error.
- **`network.router` is never emitted.** The lookup is gated on a key that is
  absent from the dictionary it reads, so the field has no reachable producer.
  See `docs/audits/entropy-audit-2026-08-22.md` (ENT-002).
- **`disk` covers `/` only.** Other mounted filesystems are not enumerated.
- **Unknown category names are ignored** rather than rejected, so a typo yields
  a smaller report, not an error.

`STABILITY.md` records which fields are committed for 1.0 and which may still
change.

## Development

```bash
make            # build ./sysinfo-mcp
make test       # agent-guide drift check, then the display smoke test
make bullseye   # build + test + clean-tree assertion; what CI runs on macOS
make install    # install to $PREFIX/bin (default /usr/local/bin)
```

`make test` runs two checks. `tests/check-agent-guide.sh` asserts that the
binary's `--help-agent` output still matches `docs/agents-guide.md`, which is
the source of truth for that text. `tests/run.sh` drives the server over stdio
and checks the display contract, so it needs an awake screen as noted above.

## Agent guide

If you use an agentic coding tool, include
[`docs/agents-guide.md`](docs/agents-guide.md) in your project context,
or run `sysinfo-mcp --help-agent` for a self-contained reference.

## Requirements

- macOS (Apple Silicon or Intel)
- Xcode Command Line Tools: `xcode-select --install`

## License

Apache 2.0. See [LICENSE](LICENSE).

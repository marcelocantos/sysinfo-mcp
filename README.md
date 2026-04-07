# sysinfo-mcp

A lightweight C MCP server for macOS that exposes system hardware and OS information to AI coding tools via the Model Context Protocol (stdio/JSON-RPC).

## Quick start

```bash
# Build and install
make
make install          # installs to /usr/local/bin/sysinfo-mcp

# Register with Claude Code
claude mcp add sysinfo -- /usr/local/bin/sysinfo-mcp
```

Or add manually to `~/.claude.json`:

```json
{
  "mcpServers": {
    "sysinfo": {
      "command": "/usr/local/bin/sysinfo-mcp"
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
| `disk`    | Disk capacity and usage per mount                |
| `os`      | Kernel info, macOS version, hostname, boot time  |
| `network` | Network interfaces, IPs, MACs, primary/router    |
| `power`   | Battery level, health, charging state, source    |
| `thermal` | Thermal pressure level                           |

## Output schema

### `cpu`

| Field                | Type   | Notes                        |
|----------------------|--------|------------------------------|
| `physical_cores`     | int    |                              |
| `logical_cores`      | int    |                              |
| `performance_cores`  | int    | Apple Silicon only           |
| `efficiency_cores`   | int    | Apple Silicon only           |
| `brand`              | string | Intel only (typically)       |
| `frequency_hz`       | double | May be absent on Apple Silicon |

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
| `mount`       | string |       |
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
| `primary`| bool   | Present and `true` on primary interface  |
| `router` | string | Gateway IP; present on primary interface |

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
| `pressure` | string | `"nominal"`, `"moderate"`, `"heavy"`, `"critical"`, `"unknown"` |

## Example output

```json
{
  "cpu": {
    "physical_cores": 16,
    "logical_cores": 16,
    "performance_cores": 12,
    "efficiency_cores": 4
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
      "primary": true,
      "router": "192.168.1.1"
    },
    {
      "name": "en1",
      "ipv4": "10.0.0.5",
      "mac": "aa:bb:cc:dd:ee:00"
    }
  ],
  "power": {
    "has_battery": false,
    "power_source": "ac"
  },
  "thermal": {
    "pressure": "nominal"
  }
}
```

## Requirements

- macOS (Apple Silicon or Intel)
- Xcode Command Line Tools: `xcode-select --install`

## License

Apache 2.0. See [LICENSE](LICENSE).

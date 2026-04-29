# Agent's Guide: sysinfo-mcp

## What this is

A C MCP server for macOS that reports system hardware/OS info over stdio JSON-RPC (protocol version 2024-11-05). Exposes a single tool, `system_info`, with an optional `categories` array parameter for selective reporting. No categories = all categories.

## Repository layout

```
main.c              — entire implementation (collectors + MCP protocol + main loop)
vendor/cjson/       — vendored cJSON library
Makefile            — build entry point
```

## Architecture

**Collector functions** (`main.c:26–489`) — one per category, each returns a `cJSON*`:

| Function | Category key | Returns |
|---|---|---|
| `collect_cpu()` | `"cpu"` | object |
| `collect_memory()` | `"memory"` | object |
| `collect_gpu()` | `"gpu"` | array of objects |
| `collect_disk()` | `"disk"` | array of objects |
| `collect_os()` | `"os"` | object |
| `collect_network()` | `"network"` | array of interface objects |
| `collect_power()` | `"power"` | object |
| `collect_thermal()` | `"thermal"` | object |
| `collect_display()` | `"display"` | array of per-monitor objects |

**Protocol handlers** (`main.c:496–647`):
- `handle_initialize()` — returns server capabilities and `serverInfo`
- `handle_tools_list()` — returns JSON Schema for `system_info` including the `categories` enum
- `handle_tools_call(params)` — invokes collectors via `WANT` macro, serialises result as text content

**`WANT(name)` macro** (`main.c:609–620`): evaluates to 1 if `name` is in the `categories` array, or if `categories` was omitted entirely.

**Main loop** (`main.c:649–702`): reads newline-delimited JSON-RPC from stdin, dispatches on `method` string (`initialize`, `notifications/initialized`, `tools/list`, `tools/call`), writes responses to stdout. Both streams are line-buffered.

## Installation

Installation is a **multi-step process** — it is not complete until all steps succeed.

1. Install the binary:
   ```bash
   brew install marcelocantos/tap/sysinfo-mcp
   ```
2. Register as an MCP server:
   ```bash
   claude mcp add --scope user sysinfo -- sysinfo-mcp
   ```
3. **Restart the agent session** to pick up the new server.

For other MCP clients, add to the client config:
```json
{
  "mcpServers": {
    "sysinfo": {
      "command": "sysinfo-mcp"
    }
  }
}
```

## Build from source

```bash
make
```

Requires macOS with Xcode Command Line Tools. Links: `IOKit`, `CoreFoundation`, `SystemConfiguration`, `ApplicationServices`, `CoreVideo`.

## Adding a new category

1. Write a static collector: `static cJSON *collect_foo(void) { ... }` returning a `cJSON*`.
2. In `handle_tools_call` (around `main.c:623`), add:
   ```c
   if (WANT("foo")) cJSON_AddItemToObject(data, "foo", collect_foo());
   ```
3. In `handle_tools_list` (around `main.c:569`), add to the enum array:
   ```c
   cJSON_AddItemToArray(cat_enum, cJSON_CreateString("foo"));
   ```
4. Update the tool description string in `handle_tools_list` to mention the new category.

## Key conventions

- All JSON construction uses cJSON. Never use a different JSON library.
- Collectors must not abort on missing data — use `if (sysctlbyname(...) == 0)` guards and emit only fields that are available.
- IOKit objects must be released with `IOObjectRelease`; CF objects with `CFRelease`. No leaks on error paths.
- Error responses use `send_error(id, code, message)`; success responses use `send_response(id, result)`.
- The `WANT` macro is defined locally inside `handle_tools_call` and `#undef`-ed after use.

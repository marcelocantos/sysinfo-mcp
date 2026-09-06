# Agent's Guide: sysinfo-mcp

## What this is

A C MCP server for macOS that reports system hardware/OS info over stdio JSON-RPC (protocol version 2024-11-05). Exposes a single tool, `system_info`, with an optional `categories` array parameter for selective reporting. No categories = all categories.

This file is the source of truth for the agent guide. `--help-agent` prints a copy embedded in the binary (`AGENT_GUIDE[]` in `main.c`); `tests/check-agent-guide.sh` fails if the two ever diverge. Symbols are cited by name, not by line number, because line numbers go stale on the next category.

## Repository layout

```
main.c                        — entire implementation (collectors + MCP protocol + main loop)
vendor/cjson/                 — vendored cJSON library
Makefile                      — build entry point (all, test, bullseye, install, clean)
tests/run.sh                  — display-category smoke test over stdio
tests/check-agent-guide.sh    — asserts --help-agent matches this file
```

## Architecture

**Collector functions** — one per category, each returns a `cJSON*`:

| Function | Category key | Returns |
|---|---|---|
| `collect_cpu()` | `"cpu"` | object |
| `collect_memory()` | `"memory"` | object |
| `collect_gpu()` | `"gpu"` | array of objects |
| `collect_disk()` | `"disk"` | array of objects (only `/` today) |
| `collect_os()` | `"os"` | object |
| `collect_network()` | `"network"` | array of interface objects |
| `collect_power()` | `"power"` | object |
| `collect_thermal()` | `"thermal"` | object (empty where the sysctl is absent) |
| `collect_display()` | `"display"` | array of per-monitor objects |

`collect_display` is supported by `display_name`, `display_refresh_fallback`, `cfstring_to_cstring`, `dict_int`, and `dict_int64`.

**Protocol handlers**:
- `handle_initialize()` — returns `capabilities`, `serverInfo` (name `sysinfo`, version from the `VERSION` macro), and `protocolVersion`
- `handle_tools_list()` — returns the JSON Schema for `system_info` including the `categories` enum
- `handle_tools_call(params)` — invokes collectors via the `WANT` macro, serialises the result as a single `type: "text"` content item holding compact JSON
- `send_response(id, result)` / `send_error(id, code, message)` — the only two writers to stdout

**`WANT(name)` macro**: a GCC/clang statement expression defined inside `handle_tools_call` and `#undef`-ed after the dispatch block. Evaluates to 1 if `name` is in the `categories` array, or if `categories` was omitted or is not an array.

**Main loop** (`main()`): parses CLI flags, then reads newline-delimited JSON-RPC from stdin, dispatching on `method` (`initialize`, `notifications/initialized`, `tools/list`, `tools/call`); anything else gets error `-32601`. Both streams are line-buffered.

## Behaviour worth knowing before you trust the output

- **`display` reports nothing while the screens are asleep.** `CGGetOnlineDisplayList` returns no displays then, so `display` is `[]` and `tests/run.sh` fails, even though `system_profiler SPDisplaysDataType` still lists the panels as asleep. Wake the screen before trusting this category.
- **`thermal` can be empty.** The `pressure` field appears only when `kern.thermalpressure` exists; on macOS 26 that OID is gone, so the object is `{}`.
- **`network.router` is never emitted.** The lookup is gated on a key that is not in the dictionary it reads, so the field documented in README and STABILITY has no reachable producer (entropy audit ENT-002).
- **`disk` reports only `/`.** Other mounts are not enumerated.
- **`cpu.brand` is present on Apple Silicon** (e.g. `"Apple M4 Max"`), while `frequency_hz` is absent there.
- Collectors never fail the request: a missing datum means a missing field, not an error.

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

## Build and test from source

```bash
make          # build ./sysinfo-mcp
make test     # agent-guide check + display smoke test (needs an awake screen)
make bullseye # build + test + clean-tree assertion; what CI runs
```

Requires macOS with Xcode Command Line Tools. Links: `IOKit`, `CoreFoundation`, `SystemConfiguration`, `ApplicationServices`, `CoreVideo`.

## Adding a new category

The category name is hand-copied across several places. Change all of them in one commit:

1. Write a static collector: `static cJSON *collect_foo(void) { ... }` returning a `cJSON*`.
2. In `handle_tools_call`, add `if (WANT("foo")) cJSON_AddItemToObject(data, "foo", collect_foo());`
3. In `handle_tools_list`, add `cJSON_AddItemToArray(cat_enum, cJSON_CreateString("foo"));`
4. Update the tool description string in `handle_tools_list`, which lists the categories in prose.
5. Update `HELP[]` in `main.c`, which also lists them.
6. Update this file and regenerate `AGENT_GUIDE[]` in `main.c` from it, then run `tests/check-agent-guide.sh`.
7. Update the category table and output schema in `README.md`.
8. Update the interaction-surface catalogue in `STABILITY.md`.
9. Extend `tests/run.sh` if the category has a checkable contract.

## Key conventions

- All JSON construction uses cJSON. Never use a different JSON library.
- Collectors must not abort on missing data — use `if (sysctlbyname(...) == 0)` guards and emit only fields that are available.
- IOKit objects must be released with `IOObjectRelease`; CF objects with `CFRelease`. No leaks on error paths.
- Error responses use `send_error(id, code, message)`; success responses use `send_response(id, result)`.
- The `WANT` macro is defined locally inside `handle_tools_call` and `#undef`-ed after use.

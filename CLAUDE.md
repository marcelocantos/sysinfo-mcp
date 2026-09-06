# sysinfo-mcp

A lightweight C MCP server for macOS that exposes system information (CPU, memory, GPU, disk, OS, network, power, thermal, display) to AI tools via JSON-RPC over stdio. It implements the Model Context Protocol, presenting all system metrics as a single tool with selectable categories.

## Build

```bash
make
```

Requires macOS and Xcode Command Line Tools. No other dependencies.

## Architecture

Single-file C server (`main.c`). Each category is implemented as a `collect_*` function that returns a `cJSON*` object. The MCP JSON-RPC protocol layer handles three methods:

- `handle_initialize` — returns server capabilities
- `handle_tools_list` — returns the tool schema with supported category enum
- `handle_tools_call` — dispatches to `collect_*` functions based on the `categories` array argument

The main loop reads JSON-RPC requests from stdin line by line, dispatches, and writes responses to stdout. cJSON is vendored for JSON construction and parsing.

## Key Files

| Path | Purpose |
|---|---|
| `main.c` | Everything: collectors, protocol layer, main loop |
| `Makefile` | Build rules (incl. `bullseye`, `test` targets) |
| `tests/run.sh` | Smoke test driving the server over stdio |
| `vendor/cjson/` | Vendored cJSON library |

## Conventions

- macOS-only: uses IOKit, SystemConfiguration, ApplicationServices (CoreGraphics), CoreVideo, and Mach APIs
- C17 standard (`-std=c17`)
- Apache 2.0 license; SPDX headers on source files

## Adding a New Category

1. Write a `collect_foo(void)` function returning `cJSON*`.
2. Add `if (WANT("foo")) cJSON_AddItemToObject(data, "foo", collect_foo());` in the `handle_tools_call` dispatch block.
3. Add `"foo"` to the category enum in `handle_tools_list`.
4. Update the tool description string to mention the new category.

## Delivery

delivery: merged to master

Work lands on `master` by a gated push, not a pull request. Once per
clone:

```bash
make hooks      # git config core.hooksPath scripts/hooks
```

`scripts/hooks/pre-push` then runs `make gate` — the `-Werror` build and
`tests/run.sh` — and refuses the push on a non-zero exit. GitHub Actions
runs the same `make gate` on `macos-latest` after the fact, and on
inbound pull requests.

`make gate` prints a **residue ledger**: the assertions it did not exercise
on this host. "At least one display" is host state, not a code invariant
(`CGGetActiveDisplayList` reports nothing while the screens are asleep), so
it runs only under `SYSINFO_REQUIRE_DISPLAY=1` and is declared otherwise.
See [`docs/gate.md`](docs/gate.md).

`tests/run.sh` asserts the display contract, and `CGGetActiveDisplayList`
returns nothing while the screen is asleep. The gate is therefore red on
a sleeping Mac for a reason unrelated to the code; wake the display (or
`caffeinate -u`) before pushing.

## Gates

profile: base

# sysinfo-mcp

**One line:** a single-file C MCP server for macOS that reports CPU, memory, GPU, disk, OS, network, power, thermal, and display information over stdio JSON-RPC.

It implements the Model Context Protocol, presenting all system metrics as one tool, `system_info`, with a selectable `categories` array.

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
| `main.c` | Everything: collectors, protocol layer, `HELP[]`, `AGENT_GUIDE[]`, main loop |
| `Makefile` | Build rules (incl. `bullseye`, `test` targets) |
| `tests/run.sh` | Display-category smoke test driving the server over stdio |
| `tests/check-agent-guide.sh` | Asserts `--help-agent` matches `docs/agents-guide.md` |
| `docs/agents-guide.md` | Source of truth for the agent guide embedded in the binary |
| `STABILITY.md` | The 1.0 interaction-surface contract |
| `vendor/cjson/` | Vendored cJSON library |

## Conventions

- macOS-only: uses IOKit, SystemConfiguration, ApplicationServices (CoreGraphics), CoreVideo, and Mach APIs
- C17 standard (`-std=c17`)
- Apache 2.0 license; SPDX headers on source files

## Testing

```bash
make test      # agent-guide drift check, then the display smoke test
make bullseye  # build + test + clean-tree assertion; what CI runs
```

`tests/run.sh` asserts the display contract, so it needs an awake screen:
with the panels asleep CoreGraphics reports no online displays and the test
fails with "expected ≥1 display, got 0". See Known limitations in the README
for the other environment-dependent categories (`thermal`,
`network.router`).

## Adding a New Category

The category name is hand-copied in nine places. Change all of them in one
commit, or the next reader gets a different answer depending on which copy
they read (entropy audit ENT-003):

1. Write a `collect_foo(void)` function returning `cJSON*`.
2. Add `if (WANT("foo")) cJSON_AddItemToObject(data, "foo", collect_foo());` in the `handle_tools_call` dispatch block.
3. Add `"foo"` to the category enum in `handle_tools_list`.
4. Update the tool description string in `handle_tools_list`.
5. Update `HELP[]`, which lists the categories in the usage text.
6. Update `docs/agents-guide.md`, regenerate `AGENT_GUIDE[]` in `main.c` from it, and run `tests/check-agent-guide.sh`.
7. Update the category table and output schema in `README.md`.
8. Update the interaction-surface catalogue in `STABILITY.md`.
9. Extend `tests/run.sh` if the category has a checkable contract.

## Delivery

delivery: merged to master

## Gates

profile: base

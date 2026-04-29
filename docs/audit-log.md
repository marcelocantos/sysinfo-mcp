# Audit Log

Chronological record of audits, releases, documentation passes, and other
maintenance activities. Append-only — newest entries at the bottom.

## 2026-04-08 — /open-source v0.1.0

- **Commit**: `4c1c176` (initial state before open-source workflow)
- **Outcome**: Open-sourced sysinfo-mcp. Audit: 14 findings (2 high, 4 medium), all high/medium addressed (CF key leak, params null guard, IPv4 dedup, LICENSE, README). Docs: README.md, CLAUDE.md, docs/agents-guide.md written, inline doc comments added. CLI flags (--version, --help, --help-agent) added with VERSION macro. STABILITY.md created. Released v0.1.0 (darwin-arm64) with CI workflow. Homebrew tap formula pending HOMEBREW_TAP_TOKEN secret.
- **Deferred**:
  - No automated tests (STABILITY.md gap)
  - Homebrew formula not yet published (awaiting PAT secret)
  - Network router field logic defect (STABILITY.md gap)
  - Thermal pressure mapping unverified against Apple docs (STABILITY.md gap)
  - WANT macro uses GCC statement-expression extension (non-portable)

## 2026-04-08 — /release v0.2.0

- **Commit**: `a090425`
- **Outcome**: Released v0.2.0 (darwin-arm64). Added complete MCP installation instructions to README and agents-guide, Homebrew install command, copy-pasteable agent prompt.

## 2026-04-29 — /release v0.3.0

- **Commit**: `76d0766`
- **Outcome**: Released v0.3.0 (darwin-arm64). Added `display` category to `system_info` (per-monitor name, id, main flag, connection, pixel/logical resolution, scale, refresh, optional refresh range; PR #2). Added PR CI workflow running `make bullseye` on `macos-latest` for every PR and push to master (PR #3). Added `tests/run.sh` smoke test and `make bullseye` standing-invariants hook. STABILITY.md updated to add the `display` surface and remove the resolved CI/tests gaps (test gap narrowed to per-category coverage rather than absence of tests).
- **Deferred**: Per-category round-trip tests for cpu/memory/gpu/disk/os/network/power/thermal still missing (STABILITY.md gap). ProMotion variable refresh range not enumerable via public APIs (only discrete-mode ranges populate `refresh_range_hz`).

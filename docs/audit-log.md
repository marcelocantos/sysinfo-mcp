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

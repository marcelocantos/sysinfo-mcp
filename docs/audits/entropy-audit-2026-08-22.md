# Entropy audit — sysinfo-mcp — 2026-08-22

## Executive summary

- **Snapshot:** `/Users/marcelo/work/github.com/marcelocantos/sysinfo-mcp`
  - Branch: `master`
  - HEAD: `dafbbb45b1f6a8a6e135f30e863fde55a35d5139` (`dafbbb4 Record v0.3.0 release commit hash in audit log (#5)`)
  - Initial dirty state: clean (`git status --porcelain=v1 -b` → `## master...origin/master`)
  - Local `origin/master` equals HEAD. GitHub `master` is ahead at `3eb09566aad05ed30987c4a6a3e16fbe7fe0e9f7` (`ci: bump GitHub Actions to Node 24 majors (#6)`, checkout action `v4` → `v6`). This audit covers the local tree only.
  - Date: 2026-08-22
  - Released product: `v0.3.0` (`#define VERSION "0.3.0"` at `main.c:4`)
- **Scope:** production C server (`main.c`), Makefile, CI/release workflows, tests, docs, and governance files. **Excluded:** `vendor/cjson/` (vendored cJSON 1.7.19; LICENSE present) except as a dependency/license boundary.
- **Headline mechanism:** a deliberately small single-file architecture is sound, but the public surface is copied by hand across schema, dispatch, two agent guides, README, and STABILITY — and two collectors (`power`, `network.router`) have lifetime/lookup defects that the only smoke test never exercises.
- **Highest-consequence findings:**
  - **ENT-001 (P1):** `collect_power` releases `AppleRawMaxCapacity` then uses and releases it again (`main.c:412–433`). `clang --analyze` reports use-after-release at `main.c:426` and `main.c:433`. Default `tools/call` includes `power`.
  - **ENT-002 (P2):** `router` lookup is gated on the wrong SCDynamicStore key, so the documented field is absent on a live primary interface.
  - **ENT-003 (P2):** `tools/list` description omits `network` and `power` while the enum includes them; `docs/agents-guide.md` and embedded `AGENT_GUIDE[]` have already drifted.
- **Unverified residue:** display collector returned `[]` in this session (local binary and live MCP); GitHub `make bullseye` is green. Thermal-label mapping vs Apple docs. Why ASan did not fire on ENT-001. GitHub-only commit `3eb0956` not present locally.

## Scope and exclusions

| Path | Role | Treatment |
|---|---|---|
| `main.c` (1068 lines) | Entire server: collectors, JSON-RPC, CLI | Fully read |
| `Makefile`, `.github/workflows/` | Build, PR CI, release | Fully read |
| `tests/run.sh` | Display-category smoke | Fully read; executed |
| `README.md`, `STABILITY.md`, `CLAUDE.md`, `docs/agents-guide.md`, `docs/audit-log.md`, `bullseye.yaml` | Declared architecture and surface | Read |
| `vendor/cjson/` | Vendored JSON library | Named exclusion; LICENSE/NOTICE checked |
| Gitignored `./sysinfo-mcp` | Local binary | Rebuilt for shipped-path probes; not a source finding |

No `AGENTS.md`, no `hygiene.yaml`, no `docs/audits/` prior to this report. Languages analyzed: C17 (`cpp.md` read), bash 3.2-portable test script (`bash.md` read). No Python/Go/Rust/SQL/web/journey surfaces.

## Commands run

| Command | Version / notes | Exit | Shipped vs auxiliary | Limitation |
|---|---|---|---|---|
| `git rev-parse HEAD`; `git status --porcelain=v1 -b` | git 2.55.0 | 0 | provenance | Local `origin/master` stale vs GitHub |
| `git log --oneline -30`; `git tag -l`; `git ls-files` | | 0 | provenance | |
| `clang --version` | Homebrew LLVM 22.1.8 on PATH; Apple clang 21.0.0 at `/usr/bin/clang` | 0 | aux | Makefile `CC ?= clang` would pick Homebrew LLVM |
| `/usr/bin/cc -v` | Apple clang 21.0.0 | 0 | aux | |
| `make test` | GNU Make 3.81; `jq` 1.8.1 | 2 | **shipped** | Rebuilt binary; `FAIL: expected ≥1 display, got 0` |
| `printf … tools/call power \| ./sysinfo-mcp` | rebuilt `./sysinfo-mcp` | 0 | **shipped** | Returned `has_battery: true` and `battery_health_percent` |
| `printf … tools/call network \| ./sysinfo-mcp` | | 0 | **shipped** | Primary `en0` has `ipv4` + `primary` but **no `router`** |
| `printf … tools/list \| ./sysinfo-mcp` | | 0 | **shipped** | Description omits network/power; enum has nine categories |
| Live MCP `sysinfo__system_info` `{display}` | installed server | 0 | **shipped (product)** | `{"display":[]}` — same empty result as local binary |
| `/usr/bin/clang --analyze -Xanalyzer -analyzer-output=text -std=c17 -fblocks main.c -I.` | Apple clang 21.0.0 | 0 (2 warnings) | aux | `osx.cocoa.RetainCount` at `main.c:426` and `:433` |
| ASan build + `tools/call power` | Apple clang `-fsanitize=address -O1` (without `-Werror`; vendored `sprintf` is deprecated in the SDK) | 0 | aux | **Did not fire.** Does not falsify analyzer; implies extra retain |
| `/bin/bash -n tests/run.sh` | | 0 | aux | Syntax only |
| `gh run list --limit 8`; `gh release view v0.3.0` | | 0 | remote CI/release | Latest local-commit CI green; GitHub master has a later CI-only commit |
| `~/.claude/skills/hygiene/hygiene_check.py` | uv-run | 1 | hygiene | `FileNotFoundError: hygiene.yaml` |
| `diff docs/agents-guide.md` vs extracted `AGENT_GUIDE[]` | | 1 (diff) | aux | Frameworks list and display-row wording differ |

`make test` failure is treated as **environment residue**, not a product P0: GitHub Actions `make bullseye` on `macos-latest` succeeded for this commit (run `25103966896`, 2026-04-29) and for later GitHub-only `#6`. Both the rebuilt binary and the live MCP returned `display: []` in this session (no WindowServer access for the agent process).

## Observed architecture

```
stdin (line-delimited JSON-RPC 2.0)
  └── main()  CLI flags | getline loop
        ├── handle_initialize   → serverInfo.version = VERSION
        ├── handle_tools_list   → one tool, categories enum
        └── handle_tools_call   → WANT(name) × collect_*()
              collect_cpu/memory/os/thermal  → sysctl / Mach
              collect_gpu/power/display_name → IOKit
              collect_network                → getifaddrs + SCDynamicStore
              collect_disk                   → statvfs("/")
              collect_display                → CoreGraphics + CoreVideo fallback
                    └── cJSON (vendor/cjson) → stdout
```

Single deployable (`sysinfo-mcp`). No package graph, no cycles, no second runtime. Direction is collectors → protocol → stdio. Cross-cutting concerns (JSON, CF/IOKit retain/release, category selection) live in the same file.

**Declared and observed agree:** macOS-only C17 MCP server; one tool; nine categories; vendored cJSON; `make` / `make test` / `make bullseye`; PR CI on `macos-latest`; Apache 2.0 + NOTICE for cJSON.

**Observed, inferred from code:** `WANT` is a GNU statement-expression (clang-on-macOS, not portable C17). Display enumeration is capped at 16 (`main.c:610–612`). Network reports every non-loopback UP interface (utun/bridge/anpi/vmenet included).

**Contradictions:**

- `docs/agents-guide.md` and `AGENT_GUIDE[]` cite collector range `main.c:26–489`, protocol `496–647`, `WANT` `609–620`, main loop `649–702`. Actual: collectors `32–725`, protocol `728–886`, `WANT` `847–858`, main `996–1068`.
- `tools/list` description (`main.c:788–794`) names cpu/memory/gpu/disk/os/display/thermal and omits **network** and **power**, which are in the enum (`main.c:807–815`) and dispatch (`main.c:861–869`).
- Embedded guide build line (`main.c:973`) omits `ApplicationServices` and `CoreVideo`; `docs/agents-guide.md:71` and `Makefile:6–8` include them.
- GitHub `master` CI uses `actions/checkout@v6`; this tree still has `@v4`.

**Unknown intent (owner):** whether `/` is the permanent disk contract; whether `router` stays in the 1.0 surface; whether `--help-agent` or `docs/agents-guide.md` is canonical.

## Dimension vector

| Dimension | State | Evidence summary | Change from baseline |
|---|---|---|---|
| Architecture topology | healthy | One file, one binary, collectors → protocol → stdio; no cycles or layer leaks | n/a (first full audit) |
| Redundancy / sources of truth | concern | Category set and agent guide exist in ≥6 copies; live drift in description and embedded guide (ENT-003) | n/a |
| Change amplification | concern | Display category (commit `7c1610c`) touched 7 files / +420 lines; add-category recipe omits README, STABILITY, AGENT_GUIDE, tests | n/a |
| Local code quality | concern | Linear collectors are readable; ENT-001 CF over-release and ENT-006 dead SSID fetch are concrete defects | n/a |
| Correctness / verification | concern | Analyzer-proven CF bug; live-missing `router`; smoke covers 1 of 9 categories; this session cannot run the display oracle | n/a |
| Security / dependencies | concern | Local stdio, no secrets, cJSON vendored with LICENSE in-tree; release tarball is binary-only and Homebrew `skip_checksum: true` (ENT-005) | n/a |
| Build / release / operations | concern | PR CI runs `make bullseye` (right gate); release job uses a weaker grep smoke and does not run `tests/run.sh` | n/a |
| Documentation / governance | concern | STABILITY.md is an honest surface catalogue; competing agent guides; no `hygiene.yaml`; no `AGENTS.md` | n/a |

No scalar score.

## Findings

### ENT-001: `collect_power` over-releases `AppleRawMaxCapacity`

- **Priority:** P1
- **Dimensions:** Local code quality; Correctness / verification
- **Status:** observed fact
- **Evidence:**
  - `main.c:412–419` creates `rawmax`, reads it, `CFRelease(rawmax)`.
  - `main.c:423–426` uses `rawmax` again for `battery_health_percent`.
  - `main.c:433` `CFRelease(rawmax)` a second time.
  - `clang --analyze`: `main.c:426:13: warning: Reference-counted object is used after it is released [osx.cocoa.RetainCount]` and the same at `main.c:433:21`.
  - Introduced in `68e5fe3` (2026-04-08, power category) and unchanged since.
  - Shipped-path `tools/call` with `categories:["power"]` on this host returned `has_battery: true`, `max_capacity_mah: 7588`, `battery_health_percent: 88.44…` (so the use-after-release path runs on battery Macs). ASan did not abort (auxiliary; extra IOKit retain likely).
- **Mechanism:** `IORegistryEntryCreateCFProperty` returns a +1 object. Early `CFRelease` drops that retain; a later `GetValue` and a second `CFRelease` are over-release. Default `tools/call` (no categories) includes `power`, so every full report hits this on machines with `AppleSmartBattery`.
- **Blast radius:** laptop/desktop-with-battery users of `system_info`; latent CF heap corruption rather than a deterministic crash today.
- **Counterevidence checked:** STABILITY.md documents `battery_health_percent` as “Needs review” for naming, not lifetime. Tests never call `power`. Analyzer warning is the RetainCount checker, not a style lint. Desktop-without-battery takes the `else` at `main.c:473–476` and skips the bug.
- **Smallest coherent remediation:** drop the `CFRelease` at `main.c:418`; keep the single release at `main.c:433`. Optionally copy the int out once and release immediately, then compute health from locals.
- **Verification:** `clang --analyze` on `main.c` with zero `osx.cocoa.RetainCount` warnings; ASan `tools/call power` on a battery Mac; a smoke assertion that `battery_health_percent` is present and in `(0, 200]` when `has_battery` is true.
- **Ratchet candidate:** CI step `clang --analyze` (Apple clang) failing the job on RetainCount warnings. Does not require new packages.

### ENT-002: `network.router` lookup is unreachable

- **Priority:** P2
- **Dimensions:** Correctness / verification; Redundancy / sources of truth
- **Status:** observed fact
- **Evidence:**
  - `main.c:327–328` copies `State:/Network/Global/IPv4`.
  - `main.c:350–352` gates the Router read on `CFDictionaryGetValue(global, kSCEntNetIPv4)` — the entity name `"IPv4"` inside an already-IPv4 dictionary, which is not a key of that dict.
  - When that gate is false, `CFSTR("Router")` at `main.c:354–355` is never consulted.
  - Live shipped-path output: primary `en0` had `"ipv4":"192.168.1.217","primary":true` and **no `router` field**.
  - `STABILITY.md:142–145` and `STABILITY.md:231–234` already name a retrieval-path defect (the write-up mentions a CFDictionary/CFString cast; the live mechanism is the wrong-key gate).
- **Mechanism:** a documented output field has no reachable producer, so clients and README examples (`README.md:202–209` includes `"router": "192.168.1.1"`) describe a value the server does not emit.
- **Blast radius:** any agent using `network.router` for gateway reasoning; 1.0 surface decision still open.
- **Counterevidence checked:** `primary` marking (`main.c:332–347`) works on the same dictionary, so SCDynamicStore itself is fine. Loopback filter is unrelated. No test asserts `router`.
- **Smallest coherent remediation:** `CFStringRef router = CFDictionaryGetValue(global, CFSTR("Router"));` with a `CFStringGetTypeID` check; delete the `kSCEntNetIPv4` gate. Or remove the field from schema, README, and STABILITY before 1.0.
- **Verification:** smoke: if a primary interface exists, `router` is a non-empty string **or** the field is absent from enum/docs in the same commit.
- **Ratchet candidate:** extend `tests/run.sh` to parse `categories:["network"]` and assert the primary object shape (including `router` once the lookup is fixed).

### ENT-003: Public surface is hand-copied; copies have drifted

- **Priority:** P2
- **Dimensions:** Redundancy / sources of truth; Change amplification; Documentation / governance
- **Status:** observed fact
- **Evidence:**
  - Category names are independently listed in: `handle_tools_list` enum (`main.c:807–815`), `WANT` dispatch (`main.c:861–869`), tool description (`main.c:788–794`), `HELP[]` (`main.c:888–900`), `AGENT_GUIDE[]` (`main.c:902–994`), `docs/agents-guide.md`, `README.md` category table, `STABILITY.md` catalogue, `CLAUDE.md` opener.
  - Live `tools/list` description: cpu, memory, GPU, disk, OS, display, thermal — **not** network or power. Enum: all nine. Confirmed via shipped `tools/list` and the connected MCP schema.
  - `diff` of `docs/agents-guide.md` vs extracted `AGENT_GUIDE[]`: display row “per-monitor objects” vs “display objects”; frameworks `IOKit, CoreFoundation, SystemConfiguration, ApplicationServices, CoreVideo` vs without the last two (`main.c:973`). Both still carry pre-display line-number ranges (`docs/agents-guide.md:16–38`).
  - Add-category recipe (`CLAUDE.md:40–43`, `docs/agents-guide.md:73–84`) tells the reader to edit collector + WANT + enum + description only — not README, STABILITY, `AGENT_GUIDE[]`, or tests.
  - Commit `7c1610c` (display category) changed 7 files, +420/−17. `STABILITY.md` caught up later in `76d0766`.
- **Mechanism:** one domain fact (the tool surface) has many authorities. The next category will miss a copy; two copies already disagree, and the copy agents actually read (`tools/list` description, `--help-agent`) is the stale one.
- **Blast radius:** MCP clients that trust the description; agents following `--help-agent` for link flags and line numbers; every additive category change.
- **Counterevidence checked:** enum and dispatch currently match (nine names). VERSION is a single `#define` used by CLI, initialize, and HELP — that SoT is healthy. STABILITY marks several fields “Needs review” rather than pretending 1.0 lock-in.
- **Smallest coherent remediation:** make `docs/agents-guide.md` the source and generate `AGENT_GUIDE[]` (or drop the embed and have `--help-agent` print the file at build time). Build the `categories` enum array from one table used by description, dispatch, and tests. Update the add-category recipe to name every consumer.
- **Verification:** test that `tools/list` enum == dispatch keys == sorted unique category strings in README’s table; `diff` of generated vs embedded agent guide is empty.
- **Ratchet candidate:** a `tests/run.sh` (or Makefile) check comparing `tools/list` enum to a frozen list, plus `diff -q` of agent-guide sources.

### ENT-004: Standing oracle covers display only; release path is weaker than PR CI

- **Priority:** P2
- **Dimensions:** Correctness / verification; Build / release / operations
- **Status:** observed fact
- **Evidence:**
  - `tests/run.sh:19–24` sends `initialize` + `tools/call` display and asserts count ≥ 1, exactly one `main`, positive `refresh_hz`, and a field-type shape. Comment at `tests/run.sh:5–7` states that scope.
  - `STABILITY.md:220–224` records the per-category gap for cpu/memory/gpu/disk/os/network/power/thermal.
  - PR CI (`.github/workflows/ci.yml:18–19`) runs `make bullseye` (build + `tests/run.sh` + clean tree).
  - Release (`.github/workflows/release.yml:23–27`) greps `"result"` on `initialize` only — does not run `tests/run.sh`, does not call `system_info`.
  - Homebrew formula test in the same workflow is `system bin/"sysinfo-mcp", "--version"` (`release.yml:60`).
- **Mechanism:** ENT-001 and ENT-002 are invisible to the only automated test. A broken collector can ship via the release workflow even if PR CI would have caught a display regression. Release and PR oracles can disagree.
- **Blast radius:** every non-display category; every GitHub Release binary.
- **Counterevidence checked:** PR CI is the right command (`make bullseye`) and is green on GitHub for this commit. Display contract is real (T2 demonstration on a 2-monitor host). `jq` is assumed, not declared in README requirements — macos-latest provides it; a laptop without `jq` fails tests before any collector assertion.
- **Smallest coherent remediation:** point the release “Smoke test” step at `make test`. Extend `tests/run.sh` with per-category shape checks (presence of keys STABILITY marks Always). Declare `jq` in README/Requirements.
- **Verification:** a release-workflow dry run that fails if `tests/run.sh` is not invoked; category tests that fail if `power` omits `has_battery` or `network` omits `name`.
- **Ratchet candidate:** `make bullseye` as the release job’s test step (same as PR).

### ENT-005: Release artifact drops attribution and checksums

- **Priority:** P2
- **Dimensions:** Security / dependencies; Build / release / operations
- **Status:** observed fact
- **Evidence:**
  - `release.yml:35` `tar -czf "${ASSET}" sysinfo-mcp` — binary only.
  - GitHub release `v0.3.0` asset: `sysinfo-mcp-0.3.0-darwin-arm64.tar.gz` (22 553 bytes, one file).
  - `vendor/cjson/LICENSE` is MIT and requires the copyright notice in copies (`vendor/cjson/LICENSE:10–11`). `NOTICE:5–9` exists in the source tree and is not packed.
  - `release.yml:61` `skip_checksum: true` on `Justintime50/homebrew-releaser@v3`.
- **Mechanism:** the shipped tarball and Homebrew bottle are not a complete attribution set, and the tap update is instructed to skip checksums — a supply-chain hole on the path users actually `brew install`.
- **Blast radius:** every Homebrew and GitHub-release install of v0.3.0 (6 downloads recorded on the tarball).
- **Counterevidence checked:** source repo has LICENSE, NOTICE, and `vendor/cjson/LICENSE`. Build does not link Homebrew libraries (matches `cpp.md`). `target_linux_* : false` is correct for this macOS-only binary.
- **Smallest coherent remediation:** `tar` LICENSE + NOTICE + binary; set `skip_checksum: false` (or drop the key) on the releaser.
- **Verification:** `tar tzf` of the release asset lists `LICENSE` and `NOTICE`; formula in the tap has a checksum.
- **Ratchet candidate:** release job assertion `tar tzf "$ASSET" | grep -E 'LICENSE|NOTICE'`.

### ENT-006: Dead SCDynamicStore fetch left from SSID attempt

- **Priority:** P3
- **Dimensions:** Local code quality
- **Status:** observed fact
- **Evidence:** `main.c:316–324` comments “Add Wi-Fi SSID if available”, creates a store, copies `SCDynamicStoreKeyCreateNetworkInterface` value, immediately `CFRelease`s it unused. `STABILITY.md:267` lists Wi-Fi SSID as out of scope (entitlements).
- **Mechanism:** leftover work that performs extra dynamic-store I/O on every `network` collect and implies a feature the product will not ship.
- **Blast radius:** `collect_network` only; no output drift.
- **Counterevidence checked:** the following primary-interface block (`main.c:326–375`) is live and needed.
- **Smallest coherent remediation:** delete `main.c:316–324`.
- **Verification:** grep for `SCDynamicStoreKeyCreateNetworkInterface` returns none; network smoke still marks `primary`.
- **Ratchet candidate:** none until network tests exist (ENT-004).

## Redundancy and competing-source-of-truth inventory

| Fact | Authorities | Drift observed? |
|---|---|---|
| Server version | `#define VERSION` in `main.c:4` (CLI, initialize, HELP) | No. `STABILITY.md:26` is marked Fluid and matches `0.3.0` |
| Category enum | enum + WANT dispatch + description + HELP + two agent guides + README + STABILITY + CLAUDE.md | **Yes** — description omits network/power (ENT-003) |
| Agent guide body | `docs/agents-guide.md` vs `AGENT_GUIDE[]` | **Yes** — frameworks, display wording, both have stale line numbers |
| Add-category procedure | CLAUDE.md, agents-guide, AGENT_GUIDE | Incomplete vs actual consumers (ENT-003) |
| Test oracle | `tests/run.sh` vs release grep-`result` vs Homebrew `--version` | **Yes** — three strengths (ENT-004) |
| cJSON license | `vendor/cjson/LICENSE`, `NOTICE`, release tarball | Tarball omits (ENT-005) |
| Disk set | `collect_disk` only `/`; STABILITY gap; README example one mount | Agreed limitation, not silent drift |
| CI checkout action | local `.github/workflows/ci.yml` `@v4` vs GitHub master `@v6` | Clone lag, not in-tree dual SoT |

Deliberate duplication that should stay: STABILITY.md as the 1.0 contract vs code as implementation — provided tests lock the Always fields.

## Healthy structure worth retaining

- **Single-file collectors + protocol.** Adding display did not invent a second framework or a plugin loader. The topology matches CLAUDE.md. Do not split `main.c` for cleanliness.
- **Single VERSION macro** (`main.c:4`) feeds `--version`, `serverInfo`, and HELP.
- **Vendoring cJSON at `vendor/cjson/` with upstream LICENSE and a root NOTICE** matches `cpp.md` (no Homebrew-linked JSON).
- **`-Wall -Wextra -Werror -std=c17`** (`Makefile:5`) on the shipped compile.
- **PR CI = `make bullseye` on `macos-latest`** — the right OS (IOKit/CoreGraphics) and the right local gate (build + test + clean tree). T3 is achieved and matches `.github/workflows/ci.yml`.
- **STABILITY.md** names Needs-review fields, 1.0 gaps (tests, disk, router, thermal, IPv6, memory definitions), and explicit non-goals (Linux, SSID, extra mounts). That honesty resisted this audit: several candidate findings were already owned there rather than discovered as surprises.
- **Collectors skip missing sysctl/IOKit data** instead of aborting (`docs/agents-guide.md:90` matches `collect_*` guards).
- **Unknown MCP methods** return `-32601`; unknown tool names return `isError` (`main.c:828–839`, `1059–1060`).

Accepted exceptions (not findings): GNU `WANT` statement-expressions on a clang/macOS-only product (noted in `docs/audit-log.md` 2026-04-08 deferred list); line-delimited JSON-RPC as the declared transport (`STABILITY.md:19`); display empty in this agent session while GitHub CI is green.

## Hygiene posture

**Hygiene posture not declared.** No `hygiene.yaml`.

Validator run from repo root:

```
/Users/marcelo/.claude/skills/hygiene/hygiene_check.py
→ FileNotFoundError: .../sysinfo-mcp/hygiene.yaml
exit 1
```

No per-dimension held tiers, floors, or drift vector. This audit did not initialize hygiene.

Overlap with entropy (for a future `hygiene.yaml`, not applied here):

| Would-be item | Reality in this tree |
|---|---|
| `correctness.tests` | `make test` exists; covers display only |
| `correctness.ci` | `.github/workflows/ci.yml` `make bullseye` |
| `release.smoke` | weaker than PR (ENT-004) |
| `docs.license` | LICENSE + NOTICE in git; not in tarball (ENT-005) |
| `security.scanner` | absent |
| `quality.analyze` | `clang --analyze` available, not in CI (would catch ENT-001) |

Entropy findings ENT-001/004/005 are ratchet candidates; do not write them into `hygiene.yaml` until adopted.

## Oracle coverage and residue

| Property | Decided by |
|---|---|
| Compiles with `-Werror` on Apple clang / this host | Shipped `make` (green here) |
| Display shape ≥1 / one main / `refresh_hz` > 0 | Shipped `tests/run.sh`; **green on GitHub CI**; **failed in this session** (`display: []`) |
| `power` CF retain balance | Auxiliary `clang --analyze` (fail); ASan (pass); no shipped test |
| `network.router` present when a default route exists | Nothing. Live probe showed absence |
| Other seven categories’ JSON shape | Nothing (STABILITY gap) |
| `tools/list` description matches enum | Nothing (ENT-003) |
| Agent guide embed == `docs/agents-guide.md` | Nothing (diff non-empty) |
| Release tarball attribution | Nothing (ENT-005) |
| Thermal label ↔ `kern.thermalpressure` | Manual / accepted 1.0 review (`STABILITY.md:175–178`, `236–240`) |
| Memory used/free vs Activity Monitor | Documented definition gap (`STABILITY.md:247–250`) |
| Disk mounts beyond `/` | Owner decision (`STABILITY.md:226–229`) |
| GitHub master `3eb0956` (checkout@v6) | Not in this clone — not audited |

**Owner residue (intent only):**

1. Is `/`-only disk reporting a permanent 1.0 constraint or a bug to fix?
2. Keep `router` in the stable surface, or delete it?
3. Canonical agent guide: file on disk, embed, or generate one from the other?
4. Declare `hygiene.yaml` now, or wait until ENT-004’s tests exist so floors are honest?

Mechanical work (fix CFRelease, wire `make test` into release, lock enum) is **not** owner residue.

## Remediation sequence

1. **Fix ENT-001** (single `CFRelease` of `rawmax`) and add `clang --analyze` to the `bullseye`/CI job so RetainCount regressions fail the shipped gate.
2. **Fix or drop ENT-002** (`router` key) in the same network pass as deleting ENT-006’s dead SSID fetch.
3. **Converge ENT-003:** one category table driving enum + dispatch + description; generate or diff-lock the agent guide; extend the add-category recipe.
4. **ENT-004:** `tests/run.sh` shape checks for every Always field in STABILITY; release job runs `make test`, not grep-on-initialize. Declare `jq`.
5. **ENT-005:** pack LICENSE+NOTICE; stop `skip_checksum`.
6. If requested, author `hygiene.yaml` from the reality above (floors that do not over-claim per-category tests).
7. Re-run this audit against the same finding IDs and dimension vector.

No architectural rewrite. The single-file topology should stay.

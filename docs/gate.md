# The owner gate and its residue ledger

sysinfo-mcp ships to `master` by a gated push (`CLAUDE.md` → Delivery).
`scripts/hooks/pre-push` runs `make gate` — the `-Werror` C17 build plus
`tests/run.sh` — and refuses the push on a non-zero exit. GitHub Actions runs
the same recipe on `macos-latest` afterwards. Modelled on claudia's gate
(`docs/gate.md`, `scripts/gate-test.py` there).

## Why this file exists

The first version of the gate refused pushes for a reason that had nothing to
do with the commit.

`tests/run.sh` asserted `expected ≥1 display, got 0`. `CGGetActiveDisplayList`
returns nothing while the screens are asleep, so the *same unchanged binary*
reports two displays at 2pm and zero at 2am. Confirmed independently: under
`caffeinate -u` the identical binary returned two displays. The gate passed by
day and blocked by night.

A gate that fails for environmental reasons is worse than no gate, because it
teaches everyone that `--no-verify` is the normal way to push. The standing
rule: **never wire a gate over a red or host-state-dependent suite without
declaring the residue visibly, in a ledger the gate itself prints.**

## The split

Every assertion in `tests/run.sh` is now on one side of a line:

| | Asserted | Rationale |
|---|---|---|
| The `tools/call` succeeds and yields `result.content[0].text` | always | protocol invariant |
| The text parses as JSON | always | protocol invariant |
| `.display` is an **array** | always | type invariant |
| Exactly one `main` among the entries present (zero when empty) | always | per-entry invariant |
| No entry has `refresh_hz <= 0` | always | per-entry invariant |
| Every entry has `id`/`main`/`connection`/`resolution_pixels`/`resolution_logical`/`scale` with the right types | always | per-entry invariant |
| **At least one display exists** | only under `SYSINFO_REQUIRE_DISPLAY=1` | host state — display power, not code |

Dropping the presence assertion could have opened a hole: with it gone, a
server that returned an error, or no `display` key at all, would have reached
the array checks as an empty list and satisfied every one of them. That is why
the protocol and array-type assertions above are explicit rather than implied.
`jq length` answers `0` for `null` as happily as for `[]`.

## The ledger

The suite prints what it did **not** exercise, every run:

```
ok: 2 display(s), 1 main, all present entries well-formed
residue (declared, not exercised on this host):
  display presence       "≥1 display" not asserted (host state, not a code invariant); …
  untested categories    cpu, memory, gpu, disk, os, network, power, thermal …
✓ display contract: invariants (2 residue line(s) declared)
```

Three residue families:

- **display presence** — printed whenever `SYSINFO_REQUIRE_DISPLAY=1` is
  absent. Exercise it with the screens awake:
  `SYSINFO_REQUIRE_DISPLAY=1 make gate`.
- **per-display invariants** — printed only when the array came back *empty*.
  The main-flag, refresh-rate and shape checks all quantify over the array, so
  an empty array satisfies them without running a line of `collect_display()`'s
  per-display body. An empty green is a much weaker green than a populated one
  and must not read the same.
- **untested categories** — `collect_cpu`, `collect_memory`, `collect_gpu`,
  `collect_disk`, `collect_os`, `collect_network`, `collect_power`,
  `collect_thermal` are served by the binary and asserted by nothing. Each is
  the *same trap waiting to happen*: a Mac mini has no battery, a machine can
  have no active network interface, thermal sensors report nothing under some
  configurations. When coverage is added, split it the same way — invariants
  unconditional, presence opt-in — rather than asserting "a battery exists".

## Interactive vs gate runs

`SYSINFO_REQUIRE_DISPLAY=1` is for a human at an awake machine who wants the
strong check, and for a future CI runner with a virtual framebuffer. It is
deliberately **not** set by `make gate`, by the pre-push hook, or in CI: those
run whenever the owner pushes, including at 2am.

## Drill

The gate is only evidence if it has been shown to refuse:

```bash
sha=$(git rev-parse HEAD)
printf 'refs/heads/burst/ci-gate %s refs/heads/master %s\n' "$sha" "$sha" > /tmp/refs.txt
git -c core.hooksPath=scripts/hooks hook run --to-stdin=/tmp/refs.txt pre-push \
  -- origin https://github.com/marcelocantos/sysinfo-mcp.git
```

Non-zero with a planted failure, zero without. `git hook run` gives the hook
`/dev/null` on stdin unless `--to-stdin` names a file; piping into it is
silently an empty push list and the gate never runs.

# Project status

Updated: 2026-09-07.

## Current state

**M0 through M3 are implemented.** The native raylib app and graphics-free headless runner share a deterministic C++20 simulation. Workers choose eligible jobs using normalized stimuli and weighted response draws, deposit into a bounded double-buffered trail field, excavate connected useful space, carry spoil, nurse brood, recover dropped food, clean mature corpses, and age or starve. The queen lays capacity-gated eggs; fed brood progresses through egg, larva, and pupa stages; decline and extinction are explicit states.

The colony now also earns **Work** from productive worker ticks and spends it on four ten-level run adaptations that change dig rate, brood development, carry capacity, and laying interval. Colony focus unlocks at twelve workers and is rate limited. Store capacity grows with reachable nest air.

Saving is durable and complete. One atomic `profile.json` plus a `profile.backup.json` previous revision are written through a temporary file, flush, backup, replace, and directory sync, under a single-writer lock. The game autosaves every 30 real seconds, on manual request, and on orderly exit, then resumes at exactly the saved tick with no offline advancement. An unreadable profile opens a paused recovery prompt rather than being overwritten.

## Active scope

**Next: M4 — Complete generation loop (T011).** Maturity latch, deterministic reproductive assignment, and winged-queen development, then transactional prestige and permanent traits.

Suggested prompt for a smaller model:

> Read AGENTS.md and docs/planning/STATUS.md, then implement T011 from docs/planning/BACKLOG.md. Preserve deterministic command validation and exact integer accounting. Inspect existing changes first, run the documented headless and dev tests, update affected contracts and STATUS, and commit with the required identity and a descriptive body.

## Milestone state

| Gate | State |
|---|---|
| Planning | Complete |
| M0 Foundation | Complete — T001 |
| M1 Watchable slice | Complete — T002–T004 |
| M2 Living colony | Complete — T005–T007 |
| M3 Safe incremental slice | Complete — T008–T010 |
| M4 Complete generation loop | Not started |
| M5 Release candidate | Not started |

## Environment observations

Implemented and verified on Apple Silicon macOS with Apple Clang 21 and CMake 4.4.3 using Unix Makefiles. Python fonttools 4.63 was installed to instantiate the pinned Nunito variable source at weight 650; the runtime has no Python dependency. First configure downloads pinned source dependencies; warmed headless configure/build has no graphics dependency. `clang-format` is not installed on this machine and CI does not gate on formatting, so no reformatting pass was run.

## Validation of this deliverable

Latest M3 commands and outcomes:

- `cmake --preset headless`, `cmake --build --preset headless`, and `ctest --preset headless -E "sixty simulated minutes"`: pass, **60/60 tests** (24 before this task).
- `cmake --preset release` and `cmake --build --preset release`: pass.
- Two production-settings Release soaks, `ant_headless --seed 101 --ticks 72000`, produce the same canonical hash `165f12aa79054393` and complete 60 simulated minutes with nonnegative stores and a live colony: 149 workers, 12 brood, 446 excavated cells, 275 births, 132 deaths, 4,981,602 productive ticks, 4,151 Work. One measured run completed in 7.67 seconds. The hash differs from M2's `24c7977a334507d5` because productive ticks and adaptation levels are now part of the canonical hash.

### T008 — Work and four run adaptations

- Costs follow ECONOMY's `ceil(15 * 8^L / 5^L)` exactly, checked against an independently computed table for all ten levels of all four upgrades; effects are 25/10/20/15 percent per level.
- Purchases spend exactly once; insufficient Work and maximum level are rejected without spending; rebuilding the view fifty times leaves levels and the canonical hash unchanged.
- Work equals `productive_worker_ticks / ticks_per_work` at every tick of a 3,000-tick run, and is zero while no productive tick has occurred, so worker count alone never grants Work.
- Every focus keeps forage, excavate and nurse reachable over a 4,000-tick window; focus is locked below twelve workers and refuses a second change inside the cooldown.
- **Pacing was measured and the balance retuned.** At the original 200 ticks per Work the first 15-Work purchase arrived after a mean of 29.2 simulated seconds on seeds 1, 7, 42, 101 and 2026 — far inside the 2–4 minute target. Measured 800/1,000/1,200/1,400 alternatives and adopted **1,200** (mean 167.8 s, range 154.2–183.6 s), which lands every seed in the target window. ECONOMY records the before/after table. First delivery is 52.6–74.1 s and first birth 176.9–272.9 s on the same seeds.

### T009 — Snapshot codec and round-trip continuation

- A colony with cargo in transit, developing brood, live paths, deposited trails and both RNG streams advanced round-trips through the codec with an identical canonical hash, and continues to match an uninterrupted run after 4,000 further ticks.
- Malformed, empty, non-object, oversized (>64 MiB) and unknown-schema documents are rejected with an actionable message, and the live colony's hash is unchanged afterwards.
- Rejected: duplicate entity ids, non-monotonic `next_id`, reservation totals that disagree with their source, wrong-size terrain and trail arrays, an actor inside solid rock, upgrade level above ten, Work counters that disagree with the world, wallet ≠ earned − spent, phase/run disagreement, out-of-range settings, and an internally invalid embedded progression.
- A saved run keeps its embedded balance when the shipped content changes; only a new run picks up current content.

### T010 — Durable saves and recovery

- Failures injected at each save stage — candidate write, backup write, backup replace, final replace — all leave the previously committed revision byte-for-byte intact and remove their temporary files.
- An interrupted replacement and a failed directory sync both report `committed` with `durability_uncertain`, after re-reading the authoritative file to establish the real revision.
- A second `SaveService` on the same directory is refused while the first holds the lock; separate directories stay independently writable.
- A corrupt current profile is never overwritten by an ordinary save; `load` reports `RecoveryAvailable`, and explicit recovery retains the original as `profile.corrupt.N.json` before committing the backup. Both files invalid reports `Invalid` rather than repairing anything.
- Autosave fires once per 30 real seconds and not as a catch-up burst after a 10-minute stall; a manual request takes priority and is consumed once; nothing is scheduled while the recovery prompt is open.
- **Real file round trip:** `ant_headless --seed 42 --ticks 4000 --save-dir … --save` then `--ticks 4000 --resume --save` produced hash `d91f218722432923`, identical to the uninterrupted `--ticks 8000` run, with `profile.json` at revision 2 and `profile.backup.json` at revision 1. `--verify-round-trip` passes on seeds 1, 7 and 42.
- **Real app quit/relaunch:** launching `ant_farm --save-dir …` wrote revision 1 on exit; relaunching resumed that colony (backup tick 6015, current tick 6036), showed "Colony resumed (revision 1)" in the footer, and preserved profile id, run id, settings and the embedded balance.

### Visual review

Reviewed at 1440×900 (zoom 4) and at the 1024×640 minimum (zoom 3.2) on Retina. The inspector shows Work, four adaptation cards with level and next cost, four focus buttons with the active one highlighted, the focus cooldown note, and a bottleneck line. The footer shows the Save button and the save/resume status. Two defects were found and fixed during review: the recovery panel's em dashes rendered as `?` because the bundled font atlas only covers printable ASCII, and a long decoder error overflowed the panel — both status lines are now ASCII and truncated to their container. A pre-existing 20-pixel mismatch between the drawn adaptation cards and their click rectangles was also fixed by giving both a single geometry helper.

Raylib's high-DPI `TakeScreenshot` still produces a doubled canvas that requires cropping for review; the rendered window content itself is sharp. Review screenshots are stored outside the repository under the session scratch directory.

## Open work

No blocker for T011.

- **Keyboard activation of the recovery prompt was not exercised interactively.** The `R` and `N` handlers are three lines that call `recover_backup` and `replace_unreadable`, both covered by unit tests, and the prompt was rendered and photographed; the key presses themselves were not driven, because this session had no way to send input to the raylib window. Pointer activation of the new adaptation and focus controls was likewise not re-exercised.
- `last_flight_receipt` from the PERSISTENCE envelope is not in the profile schema yet; it belongs to T012 with the rest of prestige.
- Player economy UI is temporary cards pending T014. Trails bias source choice but are not yet rendered as a debug overlay. Balance runs across the full purchase policy, larger-population profiling, and `.app` packaging remain in their scheduled milestones. No project license has been chosen.

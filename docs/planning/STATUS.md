# Project status

Updated: 2026-09-07.

## Current state

**M0 through M2 are implemented.** The native raylib app and graphics-free headless runner share a deterministic C++20 simulation. Workers now choose eligible jobs using normalized stimuli and weighted response draws, deposit into a bounded double-buffered trail field, excavate connected useful space, carry spoil, nurse brood, recover dropped food, clean mature corpses, and age or starve. The queen lays capacity-gated eggs; fed brood progresses through egg, larva, and pupa stages; decline and extinction are explicit states.

The UI is now a warm paper-and-diorama presentation with live colony condition, brood capacity, task distribution, excavation and birth outcomes, selection details, rounded controls, and crisp DPI-aware Nunito rendering. Persistence is still only an explicit capability boundary and does not save yet.

## Active scope

**Next: T008 — Work and four run adaptations.** Add productive worker-tick accounting, validated progression data, four upgrade commands, focus cooldown/controls, and measured bottleneck reasons. Do not begin snapshot persistence as part of T008.

Suggested prompt for a smaller model:

> Read AGENTS.md and docs/planning/STATUS.md, then implement T008 from docs/planning/BACKLOG.md. Preserve deterministic command validation and exact integer accounting. Inspect existing changes first, run the documented headless and dev tests, update affected contracts and STATUS, and commit with the required identity and a descriptive body.

## Milestone state

| Gate | State |
|---|---|
| Planning | Complete |
| M0 Foundation | Complete — T001 |
| M1 Watchable slice | Complete — T002–T004 |
| M2 Living colony | Complete — T005–T007 |
| M3 Safe incremental slice | Not started |
| M4 Complete generation loop | Not started |
| M5 Release candidate | Not started |

## Environment observations

Implemented and verified on Apple Silicon macOS with Apple Clang 21 and CMake 4.4.3 using Unix Makefiles. Python fonttools 4.63 was installed to instantiate the pinned Nunito variable source at weight 650; the runtime has no Python dependency. First configure downloads pinned source dependencies; warmed headless configure/build has no graphics dependency.

## Validation of this deliverable

Latest M2 commands and outcomes:

- `cmake --preset dev`, `cmake --build --preset dev`, and `ctest --preset dev -E "sixty simulated minutes" --output-on-failure`: pass, 24/24 tests.
- `cmake --preset release` and `cmake --build --preset release`: pass.
- Two production-settings Release soaks, `ant_headless --seed 101 --ticks 72000`, produce the same canonical hash `24c7977a334507d5` and complete 60 simulated minutes with nonnegative stores and a live colony: 147 workers, 12 brood, 526 excavated cells, 288 births, and 147 deaths. One measured run completed in 7.59 seconds.
- The headless executable links only libc++ and libSystem on macOS; its build tree does not fetch raylib.
- Unit/scenario coverage now also includes zero-stimulus exclusion, ordering-independent weighted selection, broad deterministic sample proportions, trail decay/mass bounds, autonomous excavation and birth, focus unlock, queen-death continuation, and corpse maturation.
- Live visual review covered the crisp Retina 1440-wide presentation at 5.5× and the 1024×640 minimum at 3.2× after 8,000 ticks, including shortage state, brood capacity, job counts, controls, and compact-panel fit. Raylib's high-DPI `TakeScreenshot` produced a doubled canvas that required cropping for review; the rendered window content itself was sharp. Pointer activation was not re-exercised in this pass.
- Review screenshots are stored outside the repository under `/tmp/ant-m2-*.png`; generated evidence and build outputs are not committed.

## Open work

No blocker for T008. Player economy, durable saves, full keyboard focus/accessibility polish, balance runs, larger-population profiling, and `.app` packaging remain open in their scheduled milestones. Trails bias source choice but are not yet rendered as a debug overlay. No project license has been chosen.

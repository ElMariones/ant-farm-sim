# Project status

Updated: 2026-09-06.

## Current state

**M0 and M1 are implemented.** The native raylib app and graphics-free headless runner share a deterministic C++20 simulation. Seeded terrain, queen plus six workers, bounded navigation, source reservations, visible cargo, finite/refilling sources, colony stores, pause/speed controls, camera controls, HUD, and selection inspector are present.

The UI uses locally bundled Nunito SemiBold at larger body/control sizes. The temporary guided forage behavior is deliberately labelled M1 and must be replaced by T005 task selection; persistence is only an explicit capability boundary and does not save yet.

## Active scope

**Next: T005 — task selection and pheromones.** Replace guided foraging with deterministic weighted task selection and a bounded, double-buffered trail field. Do not begin excavation, brood, persistence, upgrades, or prestige as part of T005.

Suggested prompt for a smaller model:

> Read AGENTS.md and docs/planning/STATUS.md, then implement T005 from docs/planning/BACKLOG.md. Preserve the deterministic fixed-tick and physical-food invariants. Inspect existing changes first, run the documented headless and dev tests, update affected contracts and STATUS, and commit with the required identity and a descriptive body.

## Milestone state

| Gate | State |
|---|---|
| Planning | Complete |
| M0 Foundation | Complete — T001 |
| M1 Watchable slice | Complete — T002–T004 |
| M2 Living colony | Not started |
| M3 Safe incremental slice | Not started |
| M4 Complete generation loop | Not started |
| M5 Release candidate | Not started |

## Environment observations

Implemented and verified on Apple Silicon macOS with Apple Clang 21 and CMake 4.4.3 using Unix Makefiles. Python fonttools 4.63 was installed to instantiate the pinned Nunito variable source at weight 650; the runtime has no Python dependency. First configure downloads pinned source dependencies; warmed headless configure/build has no graphics dependency.

## Validation of this deliverable

Final commands and outcomes:

- `cmake --preset headless`, `cmake --build --preset headless`, `ctest --preset headless`: pass.
- `cmake --preset dev`, `cmake --build --preset dev`, `ctest --preset dev`: pass.
- `cmake --preset release`, `cmake --build --preset release`: pass.
- Two `ant_headless --seed 42 --ticks 2400` runs produce the same canonical hash `65a6902ea73a3b27`, nine completed trips, and 9,000 delivered milli-units.
- The headless executable links only libc++ and libSystem on macOS; its build tree does not fetch raylib.
- Unit/scenario coverage includes bounds, 100-seed starter connectivity, terrain determinism, PCG32 reference values, budgeted A*, home-field revisions, batching determinism, topology replan, food conservation, final-unit reservation, unreachable release, and full-store cargo retention.
- Live visual review covered the fitted 1440-wide layout, 1200×760 Retina pointer coordinates, 1024×640 minimum layout, 1.6×/3.2×/5.5×/12× readability, cursor-centered wheel zoom, paused state, and visible carrying ants. The automation bridge emits clicks too briefly for raylib's frame polling, so inspector hit-testing is supported by the corrected logical pointer coordinates and code path but remains a human-click follow-up check.
- Review screenshots are stored outside the repository under `/tmp/ant-farm-sim-evidence/`; generated evidence and build outputs are not committed.

## Open work

No blocker for T005. Guided behavior, sparse starter art, and the temporary M1 HUD are intentionally limited. Brood, excavation, player economy, durable saves, accessibility polish, balance runs, performance claims, and `.app` packaging remain open in their scheduled milestones. No project license has been chosen.

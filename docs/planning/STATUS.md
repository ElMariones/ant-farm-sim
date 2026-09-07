# Project status

Updated: 2026-09-07.

## Current state

**M0 through M4 are implemented.** The native raylib app and graphics-free headless runner share a deterministic C++20 simulation. Workers choose eligible jobs from normalized stimuli, deposit into a bounded trail field, excavate connected space, carry spoil, nurse brood, recover dropped food, clean corpses, and age or starve. The colony earns Work from productive labour and spends it on four ten-level run adaptations. Saving is durable and atomic, and reopening the game continues the same colony at the exact saved tick.

M4 completes the generation loop. A colony latches **maturity** at 100 living workers, 150 worker births and 12 simulated minutes; after that one egg in five becomes a **winged queen**, capped at ten live and developing combined. With three winged queens, a living founding queen and an unassisted run, the player can send a **nuptial flight**, which pays Genetic Legacy through a single committed profile revision carrying an immutable receipt. Between colonies, Legacy buys eight permanent trait tiers across Vigor and Industry, and founding the next colony applies the owned traits exactly once.

## Active scope

**Next: M5 — Release candidate (T014).** Replace the temporary economy and flight controls with the ART_AND_UX layout, onboarding and accessibility, then profile and package.

Suggested prompt for a smaller model:

> Read AGENTS.md and docs/planning/STATUS.md, then implement T014 from docs/planning/BACKLOG.md. Preserve deterministic command validation and exact integer accounting. Inspect existing changes first, run the documented headless and dev tests, update affected contracts and STATUS, and commit with the required identity and a descriptive body.

## Milestone state

| Gate | State |
|---|---|
| Planning | Complete |
| M0 Foundation | Complete — T001 |
| M1 Watchable slice | Complete — T002–T004 |
| M2 Living colony | Complete — T005–T007 |
| M3 Safe incremental slice | Complete — T008–T010 |
| M4 Complete generation loop | Complete — T011–T013 |
| M5 Release candidate | Not started |

## Environment observations

Implemented and verified on Apple Silicon macOS with Apple Clang 21 and CMake 4.4.3 using Unix Makefiles. First configure downloads pinned source dependencies; warmed headless configure/build has no graphics dependency, and the headless binary links only libc++ and libSystem. `clang-format` is not installed on this machine and CI does not gate on formatting, so no reformatting pass was run.

## Validation of this deliverable

- `cmake --build --preset headless` and `ctest --preset headless -E "sixty simulated minutes"`: pass, **94/94 tests** (60 before this task). Same suite passes under the `dev` preset.
- `cmake --preset release` and `cmake --build --preset release`: pass.
- `ant_headless --verify-round-trip` passes at 20,000 / 60,000 / 100,000 / 120,000 / 140,000 / 160,000 / 200,000 ticks on seed 42, and at 40,000 and 120,000 ticks on seeds 1, 7, 101 and 2026.

### Simulation speeds

Speeds are now **1x / 5x / 20x** (previously 1x / 2x / 5x), on the footer buttons and keys `1`, `2`, `3`. The per-frame tick budget scales with the selected speed so 20x is real at a low frame rate rather than silently clamped; speed still changes the number of whole ticks, never the size of one. `preferred_speed` validation and the saved settings accept the new set.

### T011 — Maturity and winged queens

- Every maturity boundary is covered: age alone, population and births without the age, one birth short, one worker short, and all three together. A population collapse after latching does not revoke maturity.
- No winged queens are allocated before maturity; afterwards the egg assignment counter tracks exactly the eggs laid since maturity and one in five becomes a gyne.
- The combined cap of ten counts developing brood as well as adults, both while ten are in the nursery and after they emerge.
- Gynes take exactly twice the trait-adjusted duration at every stage, never count toward worker births, and never take a job.
- The assignment sequence, the maturity latch and the winged population survive a save and continue from where they left off.
- Trait modifiers are verified individually: Vigor II founds eight workers instead of six, Vigor I shortens the egg stage to 540 ticks, Vigor III shortens larva and pupa to 1,020 and 765, and a gyne still doubles the trait-adjusted figure.

### T012 — Transactional prestige and permanent traits

- `integer_sqrt` is exact across the full 64-bit domain, checked at every perfect square and boundary up to 3,000 and at values where a double has already lost precision.
- Documented payouts reproduce exactly: 4 at eligibility, 6 at 600 births with 6 queens, 8 at 1,350 births with 10 queens, with the step boundaries at 150/600/1,350/2,400 births and 3/6/9 queens, and more than ten live queens never counting for more than ten.
- Each blocking condition is named correctly: immature, too few winged queens, dead queen, assisted run.
- A flight credits Legacy exactly once. A replay against the committed profile, a revision the caller has not seen, a command naming a different run, and a profile that already flew are all refused.
- A failed commit leaves the live colony and the profile untouched: the candidate is never adopted unless the write succeeded.
- Interrupted before replacement, the run is still eligible and still pays 4. After replacement, the reward is credited once, the receipt names the run, and loading the between-runs profile runs no payout logic.
- A repeated tier purchase cannot overspend: the second click is stale, and a fresh view has nothing left to spend. The cost table is exactly 3/7/15/32, one branch costs 57, and tiers are refused beyond four and while a run is active.
- Industry IV lowers the Work threshold from 1,200 to 960 ticks exactly once.
- The full flight → shop → new colony flow was exercised through a real `SaveService`, committing one step at a time, and the founded colony carries the bought trait with zero Work and zero adaptations.

### T013 — Seeded balance and second-run proof

Full numbers in [BALANCE_REPORT](BALANCE_REPORT.md). Summary:

- **All ten policy runs survive.** No-purchase colonies reach flight in 40.1–40.3 minutes, buy-cheapest in 30.3–30.9 minutes; both inside ECONOMY's 30–45 minute target. First investment lands at 2.7–2.9 minutes, inside the 2–4 minute target.
- Investing removes about ten minutes from the run, roughly quarters the death count, and adds about 30 workers at peak.
- **Vigor I reduces time to first birth on all five seeds** (−5 to −21 s), which is the required second-run proof.
- **Industry I does not reduce time to an excavation target**; four of five seeds get slower. The mechanism is measured and recorded rather than hidden: faster digging grows nest air, which raises store capacity, which pulls workers onto foraging. Recorded as an open balance question.
- Three constant-tuning alternatives were measured and rejected because each caused more extinctions than it fixed. No ECONOMY constant was changed in T013.

### Defects found and fixed during M4

Four real bugs surfaced by pushing further than M3 did:

1. **Foragers ignored colony need.** Source choice followed trail strength alone, so seed 1 with no purchases went extinct with its carbohydrate source untouched at 100,000, its carbohydrate store at 0 and protein pinned at capacity. Source choice now ranks by shortfall against the foraging target, counting food in transit.
2. **Workers over-committed to a nearly full store**, then held undeliverable cargo indefinitely. Reservations now subtract food already carried.
3. **Clearing a route left its cursor past the end**, producing a movement state that snapshot validation rejected — the app's exit save failed outright at 20,000 ticks. A path and its cursor are now cleared together.
4. **Save/load lost path and frontier staleness**, so a resumed colony skipped a replan it still owed itself and diverged from an uninterrupted run after roughly 20,000 ticks. Staleness is now stored as a fact and restored with revision 0, which the grid never issues.

The profile schema is **version 2**, with a tested in-memory v1 migration that fills castes, traits, the maturity latch and the flight receipt, and keeps the migrated colony's canonical hash.

### Presentation upgrade (visual slice of T014)

Ant art and the overall diorama were reworked; the rest of T014 (final layout, onboarding, accessibility) is untouched.

- **Ants are drawn along a real heading** taken from their travel, so they face where they are going instead of always pointing left. A stopped ant keeps its last heading, and one that has never moved gets a stable heading from its id.
- Three zoom tiers as ART_AND_UX specifies: a dot with a cargo accent below zoom 2.6, a segmented silhouette from 2.6, and legs with an alternating tripod gait, elbowed antennae, mandibles and eyes from 5.5. Limbs are drawn a shade darker than the body so they read as legs.
- **Winged queens now look like winged queens** — two translucent swept wings, sized between worker and queen. They were previously indistinguishable from workers, which was a gap left by M4.
- Cargo is carried at the mandibles rather than floating above the head, and spoil and corpse loads are drawn, not just food.
- Chitin is warm brown rather than UI ink, so ants read against the near-black tunnel.
- **Terrain is baked into a texture** at three texels per cell, rebuilt only when a cell is dug. This replaced about 80,000 rectangle draws per frame and paid for sparse flecks, a depth gradient, and lit and shadowed edges that make a tunnel look carved. Grass became clumps of leaning blades; the nursery became a soft hollow instead of a drawn-on ring.
- **The camera now opens on the colony** instead of the geometric centre of the map, which was bare subsoil.

Two defects were found and fixed during this work: every ant body segment was invisible because `DrawTriangleFan` needs counter-clockwise winding and screen Y points down, so a positive parametric sweep culled every segment; and the heading cache could grow for the life of a session, so it is now bounded.

### Visual review

Reviewed at 1440x900 (zoom 4) and at the 1024x640 minimum (zoom 3.2) on Retina. The flight panel shows the readiness checklist with per-condition progress, the exact payout with its birth and queen components, and the next threshold computed from the rules. The between-colonies panel shows both trait branches with tier, cost and effect summary, and the found-next-colony action. A staged between-runs profile was opened in the real app and its exit save preserved `phase: BetweenRuns` with the receipt intact and no invented run. One layout defect was found and fixed: the focus buttons overlapped the excavation line, and the bottleneck line ran into the footer at the minimum height.

## Open work

No blocker for T014.

- **The surface view was not re-photographed after the final art pass.** The window server stopped accepting new windows partway through review (`GLFW: Failed to determine Monitor to center Window`), so the last captures are of the nursery at zoom 11, which does show the reworked ants, brood, queen and terrain. The surface, food sources and foraging column at mid zoom were reviewed before the final colour and limb-tone tweaks but not after them.
- **Keyboard activation was not exercised interactively.** The flight, trait and found-colony key handlers were verified through unit tests against a real `SaveService` and their panels were rendered and photographed, but this session had no way to send key presses to the raylib window. The same limitation applies to the recovery prompt's `R`/`N` keys from M3.
- **Excavation task selection is O(workers²) per tick.** Each excavating worker scans every worker to count frontier claims, costing about 22 ms per tick at 159 workers in a Debug build. This dominates test runtimes and is the first thing T015 should measure.
- Industry I has no metric that improves; payouts of 6 and 8 are verified arithmetically but not reached in a measured run. Both are recorded in the balance report.
- Player-facing UI is still temporary cards and key bindings pending T014. Trails bias source choice but are not rendered as a debug overlay. Profiling, ASan/UBSan and `.app` packaging remain in M5. No project license has been chosen.

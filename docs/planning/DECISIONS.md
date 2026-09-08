# Decision log

Accepted design baseline, 2026-09-06. These are product/architecture decisions, not completed implementation.

| ID | Decision | Reason / consequence |
|---|---|---|
| D001 | Native C++20 + raylib retained | Matches supplied direction; a headless core protects tests and future renderer options |
| D002 | Build an incremental game around autonomous ants | Early focus/investment gives the player decisions without micromanaging workers |
| D003 | One fictional generalist species and colony | Avoid incompatible biological assumptions and a premature species framework |
| D004 | Start with queen + six workers | Immediate activity; defer fragile true claustral opening to a later mode |
| D005 | Only explicit player flight grants prestige | Resolves automatic-flight/reset contradiction and makes timing meaningful |
| D006 | Guaranteed integer Legacy and a shared wallet | Predictable first reward; clear branch opportunity cost; no random prestige payout |
| D007 | One atomic profile, save before prestige | Avoid split-file duplicate/lost rewards; save/resume becomes an early milestone |
| D008 | No offline progress in v0.1 | Exact ant simulation is not cheaply extrapolatable without another tested model |
| D009 | Fixed ticks, explicit RNG, stable IDs | Reproduction, replay, and save continuity are first-class correctness requirements |
| D010 | No cave-ins or fluid model | Archive's connectivity heuristic cannot establish structural stability; defer complex hazards |
| D011 | UI draft early, polished UI before release | Visual legibility is a milestone gate; debug tooling cannot be final product design |
| D012 | Separate game rules from disk and rendering | Prestige logic must not call UI/save directly, unlike archive pseudocode |
| D013 | Pin verified dependencies at M0 | Upstream has changed since archive tags; planning-time research is not integration testing |
| D014 | No Queen age death in first release | Prevent automatic run loss during relaxed observation; starvation remains recoverable by restart |
| D015 | Work measures productive worker time | Keeps upgrades tied to actual visible activity with integer accounting |
| D016 | Permanent traits modify parameters, not fundamental AI access | All runs can excavate/use essential tasks; prestige should improve an existing loop |
| D019 | Foragers choose a source by colony need, not trail strength alone (2026-09-07) | T013 measured a colony death caused by workers hauling one nutrient to its cap while starving for the other. Source choice now ranks by shortfall against the foraging stimulus target and counts food in transit; trails break ties only. Three constant-tuning alternatives were measured first and each caused more extinctions than it fixed, so the fix belongs in foraging behaviour, not in ECONOMY's numbers. See [BALANCE_REPORT](BALANCE_REPORT.md) |
| D020 | Path and frontier staleness is saved as a fact, not a revision counter (2026-09-07) | Navigation revisions are rebuilt on load, so absolute values are meaningless across a save. Storing `path_valid` / `frontiers_valid` and restoring stale entries with revision 0 keeps a resumed run identical to an uninterrupted one; the previous behaviour marked everything current and diverged after roughly 20,000 ticks. Affects T009's continuation guarantee and the schema, which is now version 2 with a v1 migration |
| D018 | Work threshold retuned from 200 to 1,200 productive ticks per Work (2026-09-07) | T008 measured the first purchase arriving at a mean of 29.2 simulated seconds against ECONOMY's 2–4 minute target, because starting workers are productive about 85% of the time rather than the 50% the original estimate assumed. Measured 800/1,000/1,200/1,400 on seeds 1, 7, 42, 101 and 2026 and adopted 1,200 (mean 167.8 s, range 154.2–183.6 s). D015 is unchanged: Work still measures productive worker time. ECONOMY carries the before/after table; the Industry tier-4 trait scales with it (1,200 -> 960); T013 revisits this with the full purchase policy |
| D017 | Query macOS display geometry before creating the raylib window | raylib 6.0 reports physical dimensions after a post-creation resize on Retina, corrupting logical layout; the app layer may use a narrow AppKit adapter to choose one safe initial size while sim/game/presentation remain platform-independent. Pointer coordinates remain raylib-owned because live Retina checks confirmed they are already logical. The `.app` package itself remains deferred to T016 |

## Risks and resolution points

- **Pacing:** proposed rates may mature too quickly/slowly. T008 measures local progression; T013 tunes complete production-setting runs before release polish is closed. *Resolved for M4:* both purchase policies reach flight inside the documented 30–45 minute window on all five seeds, with no extinctions. Industry I still has no metric that improves — recorded openly in [BALANCE_REPORT](BALANCE_REPORT.md).
- **Emergent behavior:** reasonable local rules can produce deadlocks or ugly tunnels. Test target abandonment, low-population recovery, and screenshots in M1/M2 before adding more systems.
- **Deterministic save:** paths, reservations, RNG and update ordering are easy to omit. T009 continuation hashes block durable progression work until correct. *Resolved for M3:* save/load continuation reproduces uninterrupted hashes in unit tests, in the headless runner, and across a real app quit and relaunch.
- **Performance:** all-agent visualization at 5,000 workers is a stretch budget. T015 measures before threading or crowd abstraction.
- **Desktop UI accessibility:** raylib text/buttons do not automatically expose native accessibility. Support keyboard/scaling/reduced motion and document the remaining limitation.
- **Biological accuracy:** archive claims were not independently audited here. Player copy should say “inspired by ant colonies”; scientific educational claims require primary-source review.

## Decisions reserved for later

The owner should choose the project license and any final commercial name. M0 dependency pins and their license records are now fixed in `docs/engineering/DEPENDENCIES.md`; changes require a new compatibility check and decision entry. Signing/notarization and public binary release await packaging/user release intent. Music, paid assets, and model-generated art are not necessary to execute the current plan.

When changing an accepted decision, append a dated entry with the previous rule, new rule, rationale, affected tasks/contracts, and migration/validation consequences. Do not silently edit the archive to make history agree with the new design.

## D021 — Repair observation before release packaging (2026-09-07)

The user requested a commit/work audit and a guide for natural tunnels, varied movement, subtle within-species appearance, working selection and better UI flow. Previously T014 was a broad UI task followed by hardening. The new order in [NEXT_STEPS](NEXT_STEPS.md) starts with T014a input correctness, splits renewed excavation/navigation acceptance into T006a/b and T003a/b, re-baselines T013a, then closes T014 and the remaining T015/T016 gates. M0–M4 historical delivery is retained; T015 is explicitly partial.

The direction remains a 2D cross-section, one species and fixed 20 Hz simulation. Cosmetic variation is presentation-only; routes and active digging state are deterministic and require complete snapshot/migration treatment. No new biology systems, 3D engine or automatic terrain carving are authorized by this revision. New numeric geometry/appearance ranges are initial tuning proposals, not completed or measured behavior.

## D022 — Excavation plans faces, and the envelope reaches the clay (2026-09-07)

Previously excavation ranked every diggable cell touching nest air, kept the best eight by distance
to home, and let any worker claim the first free one. That policy expanded a single cavity around
the nursery: it had no memory of direction, so seeded noise scattered pits instead of bending a
passage, and no cell was ever part of a corridor. The envelope was Manhattan distance 64 from home
at y=58, which put the clay layer at y=138 permanently out of reach, so the hardness rules could
never apply.

Excavation now plans at most four persistent dig faces, each holding a heading for several cells and
carving a three-wide cross-section, branching after a minimum length and widening into a chamber
only when the colony is short of space. The envelope is Manhattan distance 96, which reaches the
clay. Faces steer future digging, so they are serialized with the run and included in the canonical
hash; a save written before faces existed simply carries none and the plan reseeds from the grid.

Affected: `docs/engineering/SIMULATION.md` excavation and reservation contracts, `WorldSnapshot`,
the profile codec, and the balance baseline. Measured consequence: flight moves from 30.3–30.9 to
31.7–31.9 minutes on seeds 1/7/42 under buy-cheapest, first purchase stays at 2.8–2.9 minutes, and
no seed goes extinct. Capacity rules are deliberately unchanged so the geometry change is isolated;
separating useful chamber space from transit corridors remains T006b.

Spoil overflow is now counted rather than folded into deliveries, so excavated grains, the visible
mound and off-view overflow reconcile.

## D023 — Invest surplus labor in useful nest connections (2026-09-08)

The owner requested creative gameplay development, richer ant farms and many passages, explicitly
without visual testing or lengthy routine verification this session. T014i extends the existing
room-driven loop instead of adding another currency or player-drawn blueprints. Rooms connect to
nearby reachable nest air, keep their routes through excavation, and detour around rock. When fed
and staffed, the colony can cut a cross-passage that reduces an existing trip by at least 25%.
Urgent space retains priority; optional intent persists through interruptions and saving.

This supersedes T014f's recomputed straight corridor toward home. Passage cells grant no capacity
unless they belong to an existing designated room. Real digging and hauling earn existing Work;
there is no completion payout and no passive reward for planning. Focus influences when spare
labor is available. Geometry and logistics affect future state, so schema 4 stores passages and
migrates older saves without promising cross-version hash equality. Limits and focused acceptance
are in NEST_NETWORK and STATUS; full balance and native visual acceptance remain open.

## D024 — Reuse computation buffers and update damaged terrain for 20x (2026-09-08)

A live sample of the owner's Debug game identified full-grid A* scratch initialization and full
terrain texture rebuilding as major costs. T015a replaces per-route arrays with World-owned stamped
scratch and per-bake texture allocation with retained pixels/texture and dirty chunk uploads.
Path ordering, budgets, tick scheduling, gameplay, save format and the honest limiter are unchanged.
A small graphics-free damage helper covers shading neighbors and changed room geometry in tests.

Normal play defaults to the Release build through `play.command`; Debug remains available for
development. Short comparative CPU measurements and exact-state checks are recorded in
PERFORMANCE_REPORT; they do not close the integrated T015 population/GPU/memory/soak gate.

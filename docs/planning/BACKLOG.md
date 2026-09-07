# Implementation backlog

All tasks below are **not started** unless STATUS records completion with evidence. IDs are stable. Tasks should fit one focused implementation session; split a task into suffixed IDs when its scope grows, preserving the parent acceptance criteria. Dependencies mean completed and verified, not merely files created.

## Milestone gates

| Milestone | Tasks | Exit condition |
|---|---|---|
| M0 — Foundation | T001 | Headless tests and a clean desktop target configure/build |
| M1 — Watchable slice | T002–T004 | Seeded terrain and ants visibly complete a real food round trip |
| M2 — Living colony | T005–T007 | Autonomous tasks, excavation, brood, and death coexist |
| M3 — Safe incremental slice | T008–T010 | Decisions change growth; closing/resuming preserves the colony |
| M4 — Complete generation loop | T011–T013 | Earn, save, spend Legacy, and start a stronger second run |
| M5 — Release candidate | T014–T016 | Visual, correctness, performance, and package gates met |

Do not wait until M5 to view the application. M1 is a visual quality checkpoint; M3 is the first saveable gameplay checkpoint. Stop adding scope if either is not convincing.

## M5 revised work order — 2026-09-07

The [audit and design guide](NEXT_STEPS.md) defines source findings, implementation details and acceptance checks for these stable child IDs. All children below are **not started**; the audit itself is complete. Existing parent criteria remain binding.

| Task | Scope | Depends on | Minimum completion evidence |
|---|---|---|---|
| T014a — complete | DPI-correct rendered-pose selection, click/pan separation, modal ownership, Escape/save keys, selection lifecycle and responsive inspector | T012 | Native moving-ant selection at both sizes/DPI, no modal click-through, gesture and scene tests |
| T006a — complete | Persistent bounded dig faces, connected corridors, branch/chamber geometry, explicit spoil overflow | T014a | Seeded tunnel time sequences; contention, unreachable-face and spoil accounting tests |
| T003a — complete | Deterministic per-ant route tie variation | T006a | Multiple legal routes in wide fixture, narrow-route success and continuation equality |
| **T003b — next ready** | Weighted diagonal navigation, fixed-point 2D travel and safe lane offsets | T003a | All octants, speed, no corner cuts, topology invalidation and mid-route saves |
| T014b | Subtle stable size/color/gait variation using shared render/pick poses | T003b | Zoom-tier native captures, stable reload appearance, unchanged sim state |
| T006b | Separate useful chamber capacity from transit air, compatibility accounting | T006a, T003b | No double-granted capacity on migration/reload; reachable-space invariants |
| T013a | Re-baseline geometry/motion, affordable first trait, delayed flight and long-lived colony policies | T006b, T003b | Five seeds × both policies, tier-I local benefit measurement, larger-payout and no-flight evidence |
| T014c | Pointer/keyboard flight confirmation, shop resume, save failure and normal defeat restart | T014a, T013a | Actual cancel/commit/shop/reload/new-colony flow with both input methods |
| T014d | Onboarding, trends, four clear cards, scaling and reduced motion | T014b, T014c | Required scenes, minimum-size/scaling/focus review; limitations recorded |
| T015 remaining | Population/renderer/save benchmarks, sanitizers, two-hour soak | T013a, T014d | Full VALIDATION report or explicit release scope reduction |
| T016 remaining | `.app`, archive/version/attribution and external launch/restart | T015 | Local package works independently of cwd; publication only if requested |

T015's once-per-tick claim optimization is delivered; its full gate is not. Do not repeat that optimization or mark the full task complete from the sixty-minute test. T006/T003 children reopen experience quality rather than erasing historical M1/M2 delivery.

## T001 — Bootstrap build, libraries, and tests

Dependencies: none. Read ARCHITECTURE, AGENTS, VALIDATION.

Create CMake targets `ant_sim`, `ant_game`, `ant_persistence`, `ant_presentation`, `ant_farm`, `ant_headless`, and test target only as meaningful minimal sources exist. Minimal placeholder main may establish linking but must be labeled as such. Add `ANT_BUILD_DESKTOP` so OFF never fetches/builds/links raylib or UI. Add `dev`, `headless`, `release` configure/build presets and matching dev/headless test presets; use a generator actually available or document installation. Do not auto-install system tools.

Select/pin dependencies, record their revisions/licenses in DEPENDENCIES, add formatting config, and CI for headless macOS/Linux plus desktop macOS build. Initial meaningful test covers a bounds-checked grid or clock/seed value, not `true == true`. README must contain commands actually executed.

Acceptance: fresh configure/build/CTest succeed; headless binary has no graphics dependency; release config builds; any missing runner/OS check is identified. Reject empty directory forests or a generated engine scaffold with no purpose.

## T002 — Seeded grid and camera

Dependencies: T001. Owners: sim/grid, terrain generation, presentation terrain/camera. Read SIMULATION world model and ART_AND_UX.

Build fixed-size layered terrain, connected starter entrance and nursery, seeded roots, chunk revisions, pan/zoom, DPI-safe selection coordinates. Render 384x216 cells into a viewport with clear surface/tunnel silhouettes. Provide `--seed` and screenshot/debug terrain overlay if straightforward.

Acceptance: same seed produces same material grid; guaranteed starter connectivity across 100 seeds; bounds tests; manually open 1440x900 and 1024x640 windows, pan/zoom/resize, verify pointer-to-cell mapping on Retina. Record at least one screenshot in review evidence (not required as a permanent repository asset).

## T003 — Entities, fixed ticks, navigation

Dependencies: T002. Owners: sim/components, world, rng, navigation, movement, headless CLI.

Spawn queen and six workers; stable IDs; PCG32 streams; fixed ticks; home BFS; bounded source A*; navigation revision checks. Add headless `--seed`, `--ticks`, and machine-readable stats/hash output. Simulate a synthetic round-trip target before food economics.

Acceptance: actors remain on passable cells; topology changes invalidate paths; pending searches respect budget; identical seed+ticks yields same hash; rendering frequency and 1x/5x produce identical state after the same ticks. Visually confirm motion and camera-independent simulation.

## T004 — Real forage loop and visual legibility

Dependencies: T003. Owners: sim/foraging, food components, presentation ants/HUD.

Implement two finite/refilling food sources, reservations, cargo, colony stores, initial guided foraging behavior, visible carrying, counters, and selection inspector. Guided behavior is temporary until T005; label this in STATUS. No brood/economy purchase required yet.

Acceptance: source loses exactly pickup amount; delivery credits once; full stores retain cargo; exhausted/unreachable targets release reservations; two workers cannot reserve the same final unit. Watch one actual source->cargo->storage round trip. M1 gate: ants and food remain readable at all three zoom levels.

## T005 — Task selection and pheromones

Dependencies: T004. Owners: sim/tasks, pheromones, navigation memory.

Replace guided foraging with normalized stimuli, weighted response draw, commitment, target eligibility, starvation safety, and a single double-buffered trail field. Initially only forage/idle are enabled; expose diagnostics for future tasks. Implement focus command now but keep player unlock at 12 workers when UI follows.

Acceptance: zero-stimulus jobs do not win; task ordering does not bias weighted selection; invalid tasks excluded; trails decay without deposit; mass does not increase under diffusion/evaporation; stale-source cooldown terminates fixation; same seed reproduces behavior. Use fixed statistical samples and broad deterministic tolerances, not flaky randomness.

## T006 — Excavation and useful space

Dependencies: T005. Owners: sim/excavation, grid, task eligibility, renderer.

Add frontier scoring, shared dig progress, cell clearing, spoil cargo/delivery, connected nursery capacity, and storage expansion. Use simple connected air; do not add cave-ins. Queue topology rebuild once per changed tick, not once per hit.

Acceptance: only adjacent diggable cells targeted; final hit clears once under contention; capacity counts connected nest air only; no food/dirt conversion; carried spoil reaches surface; tunnel growth is connected and visually varied across seeds 1, 7, 42. Confirm ants spend time carrying spoil rather than instantly digging the whole map.

## T007 — Brood, nutrition, and mortality

Dependencies: T006. Owners: sim/brood, metabolism, death, tasks, stats.

Implement laying gates, normalized development, care, food rations, worker births, sampled aging, starvation grace, corpse maturation/cleanup, dropped cargo recovery, Decline/Extinct. Add nurse/clean eligibility and starvation recovery. Only workers are laid until T011.

Acceptance: stage transition boundaries; initial workers excluded from births; larval shortage stalls then starves; queen death blocks new eggs but existing brood can mature; corpse timer respected; cleaner death preserves corpse/cargo consistently. Accelerated scenario settings may test lifecycle quickly, but label them separately from production pacing. M2 gate: no negative food or stuck terminal state in a 60-minute headless run.

## T008 — Work and four run adaptations

Dependencies: T007. Owners: game/economy, progression, commands/view; config parsing in persistence.

Implement productive worker-tick accounting, four 10-level upgrades, validated balance/progression JSON, integer costs, effective-parameter calculation, focus unlock/cooldown, bottleneck reasons. UI may use simple temporary cards at this stage.

Acceptance: purchase spends exactly once; insufficient Work/max level rejected; effective effects do not compound on menu reopen; zero productive ticks yields no Work; focus cannot disable essential jobs; all costs/effects match ECONOMY. Report real-setting first delivery/birth/purchase times on fixed seeds. Tune only with recorded before/after data.

## T009 — Snapshot codec and round-trip continuation

Dependencies: T008. Owners: game/snapshot, persistence codec/content/migrations, fixtures.

Implement v1 complete snapshot and strict limits, stable reference restoration, embedded content snapshot, and paused resume. No durable prestige yet. Implement save-dir override for tests. Keep file replacement mechanics in T010.

Acceptance: uninterrupted vs. serialize/load continuation hashes match with cargo, brood, paths, pheromones, and RNG active; malformed/oversized/unknown-schema input rejected without touching live state; duplicate IDs and broken references rejected; new config does not silently change an active run.

## T010 — Durable saves and recovery

Dependencies: T009. Owners: persistence/save_service, platform_paths, FileOps, app save UI.

Implement profile lock, temporary write/flush/backup/replace, revision handling, 30-second autosave, exit/manual save, safe recovery, and no offline advancement. Every test uses isolated save dirs.

Acceptance: inject failures at each save stage; current or previous valid revision remains recoverable; uncertain post-replace outcome re-reads disk; double writer refused; corrupted current file never overwritten by automatic fallback. Open app, save, quit, reopen, and verify colony and settings. M3 gate: leave/reopen the incremental slice without loss beyond documented autosave interval.

## T011 — Maturity and winged queens

Dependencies: T010. Owners: sim/brood/phase, game readiness/view, presentation.

Add maturity latch, deterministic one-in-five reproductive assignment, combined cap of 10, winged-queen development/nutrition, readiness checklist. No automatic flight or multiple colonies.

Acceptance: all maturity boundaries; living-count dip does not revoke maturity; queen death revokes flight; worker birth stats never count gynes; 10 cap includes winged brood; assignment sequence survives save/load; no flight merely from loading a mature save.

## T012 — Transactional prestige and permanent traits

Dependencies: T011. Owners: game progression/commands, persistence integration, shop UI.

Implement exact integer payout, pause/preview/confirmation, candidate-session commit, receipt, eight trait tiers, one wallet, between-runs state, fresh run IDs and modifiers, reset without reward.

Acceptance: known payouts 4/6/8; repeated/stale flight command cannot duplicate credit; failed save retains prior live colony; crash before/after replacement yields one valid phase/payout; double-click tier purchase cannot overspend; branch prerequisites enforced; Vigor/Industry effects accumulate once; next colony persists before entering play. Complete one manual flight->shop->new-colony flow.

## T013 — Seeded balance and second-run proof

Dependencies: T012. Owners: headless scenario policies, config, ECONOMY, benchmark report.

Add a deterministic policy that buys cheapest affordable run upgrade (ties excavation/nursing/foraging/queen), Balanced focus, and flies immediately when eligible. Compare with no-purchase runs for seeds 1,7,42,101,2026. Add targeted Vigor I / Industry I comparisons under identical seeds and action policies. Run policy commands once per sim second so tests do not assume every render frame.

Acceptance: report production-setting times to first investment/maturity/flight, bottlenecks, failures and spread; aim at the documented pacing range; ensure no-purchase colonies survive or explicitly fix the balance. Show which second-run metric improves. If targets cannot be met, keep M4 open and revise documented constants; do not invent success or hide failing seeds.

## T014 — Player-facing UI and onboarding

Dependencies: T012; final copy/tuning follows T013. Owners: presentation/theme/UI, game view.

Replace temporary controls with ART_AND_UX layout, four cards, inspectors, progress checklist, shop, pause/save/recovery menu, objective hints, UI scaling, focus navigation, reduced motion, and assisted-run warning before debug mutations.

Acceptance: every required scene in ART_AND_UX reviewed as available; keyboard and pointer ownership verified; no click-through purchases; readable at minimum size and Retina; unavailable action explanations correct; shortage warning names an actual bottleneck; hints do not re-award anything after loading. Report accessibility limitations honestly.

## T015 — Profile performance and harden

Dependencies: T013, T014. Owners: measured hot systems only; scenario/benchmark tooling.

Measure 100/1,000/5,000-worker scenarios, Release vs. Debug distinction, memory, saves, path queues, renderer chunk updates. Perform ASan/UBSan and 2-hour simulation soak. Optimize only demonstrated bottlenecks; do not add threads reflexively.

Acceptance: VALIDATION targets met or concrete documented release scope reduction; no growth in leaked entities/caches; no unbounded path queues; speed limits visible; state consistency after long save/load cycles. Do not claim 5x at 5,000 workers unless actually achieved.

## T016 — Package and release checklist

Dependencies: T015. Owners: app asset/save paths, packaging scripts, README, attribution.

Create macOS app bundle with assets resolved independently of cwd, release archive and version metadata. Verify clean-machine/toolchain assumptions, writable user-save location, and no bundled personal saves. Record unsigned/notarization status honestly; no developer certificate purchase is required for this task. Ask owner about project license before a public binary release that requires that decision.

Acceptance: packaged app launched from Finder outside repo, assets/input/save/relaunch work, first-run instructions accurate, all v0.1 milestone gates closed, outstanding limitations documented. Tag/publish binaries only when the user requests release; local packaging is part of this task.

## Not scheduled

True claustral founding, sandbox as a separate polished mode, cave-ins, rain/floods, combat, extra species, full male mating biology, multiple colonies, cloud saves, offline progress, and browser port. Reconsider only after v0.1 has evidence of a satisfying first and second run.

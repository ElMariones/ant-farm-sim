# Validation strategy

This document specifies required evidence. Current executed checks are recorded in STATUS and NEXT_STEPS; an unmeasured target below is not a delivered result.

## Test layers

| Layer | Examples | Execution |
|---|---|---|
| Unit/contract | Grid bounds, cost rounding, payout boundaries, PCG reference vectors, stage transitions | Catch2 + CTest headless |
| System/scenario | Real food round trip, invalid path, queen starvation, colony recovery, first flight | Seeded world with finite tick count and explicit invariants |
| Persistence | Continuation equality, corrupt input, failure injection, prestige retry | Temporary directories; FileOps test adapter |
| Visual/manual | Legibility, controls, resize, selection, flight/shop, recovery UI | Native app + screenshots and observed actions |
| Performance | Tick/system timings, path counts, rendered frame time, save latency | Release build, specified seeds and machine |
| Distribution | Finder launch, independent asset/save paths, restart | Built macOS bundle outside checkout |

Build success is necessary but does not demonstrate correct behavior or visual quality. Use the narrowest relevant tests after a change, then milestone checks at integration boundaries. Do not repeatedly run unrelated suites without a reason.

## High-value invariants

1. Food sources + cargo + dropped food + stores + recorded consumption/decay equals initial food + external refill, per nutrient. Reservations do not enter this sum.
2. Wallet = lifetime Legacy earned - lifetime Legacy spent; all three nonnegative.
3. Worker births equal actual worker eclosions, excluding six starters and winged queens.
4. No valid entity is out of bounds or inside solid terrain; no path step enters an invalid cell.
5. One target quantity cannot be picked/delivered twice; death releases its actor's claims.
6. No dead entity simultaneously has a live role. Phase and living population agree after tick completion.
7. Sim seed, input commands and tick count determine canonical state independent of render frequency.
8. Save/load continuation preserves authoritative state, including RNG, task commitment, active paths, care, and fractional remainders.
9. Any interrupted prestige yields either original active run or committed BetweenRuns plus one receipt/reward.
10. Trait and run-upgrade modifiers apply exactly once from immutable base parameters.

## Boundary matrix

Test at zero, one below, exactly at, and one above: upgrade cost; level/tier cap; 150/600/1350 birth payout steps; 3/6/9 winged-queen bonuses; 100 living worker maturity; 720-second age gate; nursery capacity; starvation/corpse/development timer boundaries; world edges; entity/input limits.

Include an active run whose config file changes after saving, a queued path during snapshot, multiple workers contending for the last source unit, cleaner death carrying a corpse, full store on delivery, no workers with surviving brood, and process reopen after prestige replacement but before animation.

## Scenario report format

Record repository revision, compiler/build type, machine/OS, content hash, seed, initial state, ordered action policy, total ticks, measured result, and expected invariant. Keep large traces in ignored output; commit compact regression fixtures and representative reports only when they explain a decision.

Production balance uses five fixed seeds listed in ECONOMY and the scripted T013 purchase policy. Short-duration life-cycle fixtures are useful for correctness but must not appear in pacing results. A baseline failure is a result to fix, not a seed to silently drop.

## Performance budgets (targets, not measured)

Reference class: Apple Silicon laptop with 16–24 GB memory; record the exact device when testing. At 384x216 cells:

| Scene | Target |
|---|---|
| 1,000 workers, 1x, default view | 60 FPS; p95 simulation tick <=5 ms |
| 1,000 workers, 5x | Sustained 100 ticks/sec with responsive input and bounded backlog |
| 5,000 workers, 1x | >=30 FPS and sustained 20 ticks/sec |
| Working memory | <512 MiB at 5,000 workers including graphics resources |
| Save/load at 1,000 workers | <500 ms each, with visible feedback if over 100 ms |
| Soak | 2 simulated hours, no invariant failures or unbounded queue/entity growth |

Warm up for 10 seconds, measure at least 60 seconds wall time for frame/tick rates; retain sample counts and percentile method. Synthetic high-population fixtures are for stress, not normal progression claims. Sample without logging every actor/tick. A debug build is not a release benchmark.

At 5x a 20 Hz sim needs 100 ticks/sec; “5 ms per tick” alone does not prove a 60 FPS combined renderer budget. Measure the integrated loop, terrain rebuilds, paths, and saves. Disable automatic high-speed selection if real hardware cannot sustain it.

## Build verification

The implemented presets provide:

```text
cmake --preset headless
cmake --build --preset headless
ctest --preset headless --output-on-failure
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
cmake --preset release
cmake --build --preset release
```

These commands are implemented; record which were actually run for each change. A sanitizer configuration and its evidence remain part of T015. CI must prove headless configuration excludes desktop graphics dependencies; avoid a virtual display as a substitute for a true headless sim.

## Release evidence

Complete the required ART_AND_UX scenes, one full manual generation loop, restart/recovery tests, fixed-seed pacing report, measured performance and soak, packaged launch outside cwd, attribution inventory, and README accuracy. Outstanding defects that break saving, duplicate currency, or trap normal progression block release. Cosmetic issues can be documented explicitly.

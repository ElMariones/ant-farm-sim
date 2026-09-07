# Economy and initial balance

This file is the numeric authority. **All timings and rates are untested starting values.** T008 and T013 must measure the full loop and update them. Do not represent arithmetic checks as evidence of fun or a proven 30-minute run.

## Units and clocks

Seconds below are simulated seconds at 1x. Food uses signed 64-bit milli-units internally (1 food = 1,000 milli-units). Work and Genetic Legacy use signed 64-bit whole numbers. Parameters use rational/fixed-point values after load; never calculate purchase costs from floating-point money.

`level` means owned run-upgrade levels; buying from level 0 to 1 costs the level-0 price. Each run upgrade has 10 levels. All four use `cost(level) = ceil(15 * 8^level / 5^level)`, calculated with checked integer arithmetic. The first costs are 15, 24, 39, 62, 99. Effects use total current level and apply from immutable base parameters, never compounded on already-modified values.

## Run starting values

| Parameter | Starting value |
|---|---|
| Population | Queen + 6 workers; 0 brood; 0 winged queens |
| Stores / capacity per food | Carbs 60 / ~99; protein 30 / ~50 at founding, both set by the granary cells dug |
| Food sources | Finite. Two known at founding, 140 each; new ones appear every 30-70 s, 60-160 each, up to eight at once |
| Source supply | None. A site is spent when it is carried away; the surface keeps about two sugar sites per protein one |
| Carry capacity | 2 units of one food per worker |
| Worker movement | 6 cells/sec |
| Worker metabolism | 0.003 carbs/sec |
| Queen metabolism | 0.02 carbs/sec + 0.01 protein/sec |
| Winged queen metabolism | 0.003 carbs/sec + 0.002 protein/sec |
| Larva metabolism | 0.008 carbs/sec + 0.015 protein/sec; winged-queen larva 2x |
| Queen laying interval | 12 seconds, one egg, no catch-up burst |
| Worker development with continuous care | Egg 30 sec; larva 60 sec; pupa 45 sec |
| Development without care | 50% of normal rate; food shortage stalls larva |
| Winged-queen development | 2x each worker stage duration |
| Starvation grace | Worker 120 sec; queen 300 sec; larva 180 sec; winged queen 120 sec |
| Initial Work / Legacy | 0 / existing profile wallet (0 on new profile) |
| Nursery capacity | max(12, connected brood-room cells) |
| Food stimulus targets | A full larder: 100% of current store capacities |

Consumption occurs each second with fixed-point remainder retained for fractional rates. When a due per-entity ration is unavailable, do not consume a partial ration; increment continuous shortage duration and apply any relevant development stall. A complete ration resets that entity's continuous starvation timer. The HUD should distinguish available food from whether larvae are receiving it.

The 6-worker colony needs 0.038 carbs/sec before brood, below the source supply. Fully cared base queen output is at most 5 eggs/minute. Fifteen concurrent larvae consume 0.225 protein/sec; with queen upkeep, below the 0.4 source refill. These are local sanity checks only: travel, care, crowding, aging, and reproductive diversion may still constrain a run.

## Work: progression earned from activity

Work represents colony adaptation, not edible nutrition or soil. Add **1 Work per 60 productive worker-seconds** accumulated across workers. Productive time is time actually moving on a valid committed food/spoil/corpse delivery or source approach route, performing dig hits, or completing a nursing service. Idle time, failed searches, path-queue waiting, blocked movement, and revisiting a fully cared brood grant none.

At most one productive tick per worker per simulation tick. Maintain `productive_worker_ticks` as an integer remainder; every 1,200 accumulated ticks grants 1 Work, retaining the remainder. Worker count alone never grants Work. This intentionally rewards useful motion even before a round trip completes; arbitrary debug loops mark the run assisted.

**T008 measurement (2026-09-07).** The original 200-tick threshold (1 Work per 10 productive worker-seconds) was measured, not assumed, and was far too fast: starting workers are productive roughly 85% of the time rather than the 50% the earlier estimate used, so the first 15-Work purchase became affordable after a mean of **29.2 simulated seconds** across seeds 1, 7, 42, 101 and 2026 (range 27.6–31.2 s) against a 2–4 minute target. Measured alternatives on the same seeds and policy:

| Ticks per Work | Mean time to first purchase | Range |
|---|---|---|
| 200 (original) | 29.2 s | 27.6–31.2 s |
| 800 | 111.9 s | 104.2–119.5 s |
| 1,000 | 140.1 s | 129.2–152.2 s |
| **1,200 (adopted)** | **167.8 s** | **154.2–183.6 s** |
| 1,400 | 194.2 s | 179.2–205.7 s |

1,200 is adopted because every measured seed lands inside the 2–4 minute window with margin at both ends. Release build, Apple Silicon macOS, production settings, no purchases made before the first affordable one. T013 revisits this alongside the full purchase policy.

## Run adaptations

| Upgrade ID | Player name | Effect at owned level L |
|---|---|---|
| `excavation` | Strong Mandibles | Dig work rate = base * (1 + 0.25L) |
| `nursing` | Nursery Rhythm | All brood stage durations = base / (1 + 0.10L) |
| `foraging` | Efficient Trails | Food carry capacity = base * (1 + 0.20L) |
| `queen` | Queen Vitality | Laying interval = base / (1 + 0.15L) |

These upgrades change rates/capacity, not task eligibility. Existing progress is stored in normalized fixed-point development/work units; a duration reduction changes future speed without resetting or instantly replaying completed births. A carrying-capacity reduction is impossible during a run under normal controls; load validation preserves already carried quantities rather than deleting excess.

Run upgrades are bought with Work and reset to zero on a new run. Neither food nor Work converts to Legacy directly. Food storage capacity is the room the colony has physically dug: each cell of a **granary room** holds 1.8 units, two thirds of the cells allotted to carbohydrate and the rest to protein. A founding nest opens with one granary, roughly 99 carbohydrate and 50 protein, and capacity grows only when the colony widens that room or digs another. Granary cells only ever accumulate, so capacity is monotonic and can never invalidate stored food.

## Maturity and prestige payout

Maturity requires simultaneously >=100 living workers, >=150 worker births during the run (initial workers excluded), and >=720 seconds of run time. Once reached, it stays latched while the queen lives. Flight requires maturity, at least 3 live winged queens, and an unassisted run.

Payout is deterministic and previewed:

```text
birth_bonus = integer_sqrt(floor(workers_born / 150))
queen_bonus = floor(min(live_winged_queens, 10) / 3)
Legacy earned = 2 + birth_bonus + queen_bonus
```

At eligibility (150 births, 3 winged queens), payout = **4**. At 600 births and 6 winged queens, payout = **6**. At 1,350 births and 10 winged queens, payout = **8**. No random survival roll affects the currency. Square-root steps occur at 150, 600, 1,350, 2,400 births; queen steps at 3, 6, and 9 live queens. The Flight UI computes the next useful threshold from these rules (do not hard-code mock hint text).

`integer_sqrt` must floor exactly and handle its full validated input domain without floating-point conversion. The game tracks total earned/spent Legacy for diagnostics plus the spendable wallet; only the wallet funds purchases.

## Permanent traits

One shared wallet; the branches have independent linear prerequisites. Owning tier 3 includes tiers 1 and 2. Costs are an explicit table, **not** the archive's approximate exponential formula.

| Tier | Vigor effect added at this tier | Industry effect added at this tier | Cost per branch |
|---|---|---|---|
| 1 | Egg duration x0.90 | Dig rate x1.15 | 3 |
| 2 | +2 starting workers | Movement speed x1.10 | 7 |
| 3 | Larva and pupa durations x0.85 | Food carry capacity x1.20 | 15 |
| 4 | Queen food consumption x0.50 | Productive worker ticks required per Work: 1,200 -> 960 | 32 |

Total to complete one branch = 57 Legacy; both = 114. Completing the full tree is not required for a v0.1 release test. Four Legacy from the first flight affords one tier-1 trait with one remaining; it cannot buy both branches immediately.

Traits can only be bought between runs. On founding, calculate effective parameters in order: base config -> cumulative owned Legacy effects -> run upgrade effects (initially zero). Save the base config snapshot, applied trait levels, and content version. Do not stack tier modifiers repeatedly when loading or opening a menu.

## Run reset and anti-duplication

Flight closes the run and stores an immutable receipt `{run_id, earned_legacy, births, winged_queens}` in the same committed profile revision as the wallet update. `run == null` and `phase == BetweenRuns` after success. A retry of the same flight cannot create another receipt. A crash before file replacement leaves the previous run eligible; a crash after replacement loads BetweenRuns with the one reward already credited.

Restart without prestige carries no payout. Starting a new run increments generation and allocates a new unique run ID. Profile creation begins generation 1; failure resets also advance generation but not successful-flight count. See [PERSISTENCE](../engineering/PERSISTENCE.md) for durable transaction ownership.

## Balance evaluation

Run fixed seeds 1, 7, 42, 101, 2026 with both no-purchase and a documented deterministic purchase policy. Measure first delivery, first affordable upgrade, first birth, maturity, flight readiness, starvation, worker peak, Work/minute, path idle fraction, and food inventories. Use production settings, not accelerated test durations, for pacing reports.

Targets: no mandatory early rescue; a viable no-purchase colony; first investment approximately 2–4 minutes; first prestige approximately 30–45 minutes under a stated purchase policy; at least one bottleneck change visible after an investment. With the same seed and policy, Vigor I should reduce first-birth time and Industry I should reduce time to a specified excavation target. Do not require every trait to improve every metric.

If targets fail, tune rates and ratios before adding currencies or systems. Document changed values and measured deltas. The full eight-tier purchase horizon is an open balance question for post-first-run playtests.

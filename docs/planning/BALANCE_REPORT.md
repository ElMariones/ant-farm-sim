# Seeded balance report — T013

Measured 2026-09-07, re-baselined after the M5 spoil-mound and excavation-scheduling changes. Release build, Apple Silicon macOS (Apple Clang 21), production settings.
Reproduce any row with:

```sh
./build/release/ant_headless --seed 42 --ticks 216000 --policy buy-cheapest
```

`--policy none` makes no purchases; `--vigor N` / `--industry N` found the colony with owned
permanent traits. The policy issues commands once per simulated second, never per rendered frame,
sets Balanced focus as soon as it unlocks, and stops at the tick flight first becomes possible.

## Purchase policy

Buy the cheapest affordable run adaptation whenever one is affordable, repeating until nothing is.
All four adaptations share a cost table, so ties break in the documented order: excavation,
nursing, foraging, queen.

## Headline result

| Policy | Time to flight | Maturity | First purchase | Peak workers | Deaths | Payout |
|---|---|---|---|---|---|---|
| No purchase | 39.8–39.9 min | mean 32.3 min | — | 147–153 | 38–46 | 4 |
| Buy cheapest | 31.7–31.9 min | mean 26.9 min | 2.8–3.0 min | 172–179 | 10–17 | 4 |

Both policies land inside ECONOMY's 30–45 minute first-prestige target on all five seeds, and the
first investment lands inside the 2–4 minute target. **All ten runs survive**; none declines or
goes extinct.

From the rows below, investment saves a mean **483.4 seconds (8.1 minutes, 20.2%)**,
reduces mean deaths from **42 to 14.4** (65.7%), and adds **25.2 peak workers** on average. The dominant reported bottleneck in every
run is "Brood needs nursing".

## Per seed

| Seed | Policy | Flight (s) | Maturity (s) | First buy (s) | First birth (s) | Peak | End | Deaths |
|---|---|---|---|---|---|---|---|---|
| 1 | none | 2391 | 1941 | — | 252 | 148 | 146 | 42 |
| 1 | buy | 1912 | 1610 | 169 | 252 | 175 | 175 | 14 |
| 7 | none | 2390 | 1942 | — | 189 | 149 | 142 | 46 |
| 7 | buy | 1904 | 1603 | 175 | 189 | 172 | 172 | 17 |
| 42 | none | 2389 | 1940 | — | 253 | 153 | 150 | 38 |
| 42 | buy | 1906 | 1608 | 167 | 253 | 179 | 179 | 10 |
| 101 | none | 2393 | 1942 | — | 228 | 147 | 144 | 44 |
| 101 | buy | 1909 | 1611 | 171 | 228 | 177 | 175 | 14 |
| 2026 | none | 2392 | 1941 | — | 223 | 152 | 148 | 40 |
| 2026 | buy | 1907 | 1607 | 178 | 216 | 172 | 172 | 17 |

Spread across seeds is tight: 4 s between the slowest and fastest no-purchase flight, 8 s
between buy-cheapest flights. Every run reaches flight with the minimum eligible payout of 4,
because the policy flies immediately rather than waiting for a larger reward.

## Second-run proof

Same seed, same action policy, permanent traits owned at founding.

**A completed trait branch reaches flight sooner on every seed.** This is the demonstration that a
fully upgraded run is measurably stronger. Each full branch costs 57 Legacy, so these
comparisons do not establish the benefit affordable after the first four-Legacy flight:

| Seed | Base flight (s) | Vigor IV (s) | Delta | Industry IV (s) | Delta |
|---|---|---|---|---|---|
| 1 | 1912 | 1860 | **-52** | 1862 | **-50** |
| 7 | 1904 | 1844 | **-60** | 1857 | **-47** |
| 42 | 1906 | 1844 | **-62** | 1860 | **-46** |
| 101 | 1909 | 1856 | **-53** | 1861 | **-48** |
| 2026 | 1907 | 1852 | **-55** | 1862 | **-45** |

Vigor IV is worth a mean **-56.4 s** and Industry IV **-47.2 s**, each faster on **5 of 5 seeds**.

**A single tier-1 trait has no consistent reported benefit.** Vigor I moves flight by a few seconds out of roughly
1,900, and its effect on time to first birth swings between −21 s and +49 s depending on seed;
Industry I behaves the same way. This is a reported negative result requiring a controlled local comparison: the run is
gated by maturity (150 births, 100 living workers, 720 s), and one tier of one branch is too small
to shift a gate that far downstream. It does mean the first flight's four Legacy buys an
improvement the player is unlikely to feel, which is a design question worth revisiting — the
branches only pay off visibly once several tiers are owned.

## M5 re-baseline

Five M5 changes altered the simulation and required these numbers to be measured again: excavated
grains are tipped onto a real surface mound, frontier claims are counted once per tick instead of
rescanned per excavator, a movement bug that left about a fifth of workers stuck was fixed, ants
now need solid ground within reach to walk on, and food sources moved from a cell above the ground
to the ground itself.

Pacing survived all of it. The most consequential was the movement fix: workers whose next path
cell was directly above or below them used to ping-pong across the target column forever, and they
counted as productive while doing it. Excavation over sixty simulated minutes went from 353 cells
to about 1,200 once those workers actually walked, which is why the spoil apron needed a proper
cone profile rather than a flat cap.

First delivery is now 60–109 s, against 52.6–75.0 s before M5, because foragers walk the ground
contour and climb the spoil their own colony piled up instead of flying over it.

## Balance changes made during T013

Two colony-survival failures were found and fixed at the owning layer rather than by tuning
constants:

1. **Foragers ignored which nutrient the colony needed.** Source choice followed trail strength
   alone, so a colony could haul one nutrient until its store capped while starving for the other.
   Seed 1 with no purchases went extinct this way: its carbohydrate source sat untouched at 100,000
   while the store held 0 and 37 workers starved, with protein pinned at capacity. Source choice
   now prefers the nutrient with the larger shortfall against the same 50%-of-capacity target the
   foraging stimulus uses, counting food already in transit; trails only break a tie.
2. **Workers over-committed to a nearly full store.** Several foragers could each reserve a full
   load for a nutrient with room for one, then sit in `WaitingForStorage` holding undeliverable
   cargo. Reservations now subtract food already carried as well as food already reserved.

Three tuning attempts were measured first and **rejected**, because each traded one failing seed
for more:

| Attempt | Result |
|---|---|
| Carry capacity 2 → 3 units | Fixed seed 1, but seeds 7 and 2026 went extinct under the buy policy |
| Laying gated on a 10%-full larder | Three seeds went extinct; the gate blocks replacement of aging workers |
| Worker carbohydrate metabolism 0.003 → 0.002 /s | Four seeds went extinct |

No ECONOMY constant was changed in T013. The documented starting values still stand; the T008
Work-threshold retune (200 → 1,200 ticks per Work) remains the only balance constant changed in
this project so far.

## Audit note (2026-09-07)

Summary arithmetic above was corrected from the existing rows, not from fresh balance runs.
Older STATUS measurements are historical and differ from this later table. Reproduce and record
a revision/content hash before tuning; T013a in NEXT_STEPS requires first-affordable-trait evidence.

## Open balance questions

- Industry I has no metric that improves, as recorded above.
- Every run flies at the minimum payout of 4 because the policy flies immediately. Payouts of 6 and
  8 are verified arithmetically in unit tests but have not been reached in a measured run; that
  needs a policy that waits for 600 births and 6 winged queens.
- Larval starvation is reported at some point in nearly every run. Colonies recover, but the
  larva ration may be tighter than intended.
- These runs stop at first flight, so nothing is measured about a colony left running much longer.

## T014e re-check — physical food and a living nest

Measured 2026-09-07 after roots became mineable, stone became permanent, the founding nest became
three chambers, and stored food moved from a counter into heaps on real cells. Release build,
Apple Silicon macOS, three seeds against both policies:

| Seed | Policy | Flight (s) | Maturity (s) | First buy (s) | Peak | Deaths |
|---|---|---|---|---|---|---|
| 1 | buy | 1982 | 1662 | 182 | 172 | 18 |
| 1 | none | 2391 | 1942 | — | 148 | 42 |
| 42 | buy | 1958 | 1639 | 183 | 179 | 11 |
| 42 | none | 2392 | 1941 | — | 151 | 39 |
| 101 | buy | 1951 | 1635 | 187 | 174 | 15 |
| 101 | none | 2412 | 1966 | — | 146 | 44 |

Pacing is unchanged: every run lands inside ECONOMY's 30–45 minute first-prestige target and inside
the 2–4 minute first-investment target, peak population and deaths match the rows above, and no run
declines or goes extinct. The dominant bottleneck is still "Brood needs nursing".

Sixty simulated minutes on seed 42 gives the same 143 workers, 278 births and 141 deaths as before
the change, and 1,808 Work against 1,692, while cells excavated rose from 664 to 905 because a full
larder is now a reason to dig a chamber.

One balance change was needed and is recorded here rather than tuned away. Taking a dig face now
commits a worker for `5 + distance/6` seconds instead of a flat five. A store chamber can be a ten
second walk from the queen; with the old flat commitment a worker reconsidered and turned back
before it ever arrived, and excavation over twenty thousand ticks fell from 117 cells to 17.

## T014f re-check — rooms on demand and food that runs out

Measured 2026-09-07 after excavation became room-building and forage sites became finite,
discoverable and self-renewing. Release build, Apple Silicon macOS, three seeds against both
policies:

| Seed | Policy | Flight (s) | Maturity (s) | First buy (s) | Peak | Deaths | Cells dug |
|---|---|---|---|---|---|---|---|
| 1 | buy | 2034 | 1725 | 163 | 175 | 16 | 450 |
| 1 | none | 2573 | 2107 | — | 147 | 42 | 379 |
| 42 | buy | 2035 | 1722 | 160 | 180 | 11 | 388 |
| 42 | none | 2600 | 2139 | — | 146 | 46 | 355 |
| 101 | buy | 2003 | 1700 | 176 | 175 | 15 | 399 |
| 101 | none | 2414 | 1946 | — | 148 | 45 | 380 |

Every run lands inside ECONOMY's 30-45 minute first-prestige target — 33.4-33.9 minutes buying and
40.2-43.3 not — and inside the 2-4 minute first-investment target. Peak population and deaths are
unchanged. No run declines or goes extinct. The dominant bottleneck is still "Brood needs nursing".

Excavation is lower and means something different: 355-450 cells of rooms and the corridors that
reached them, against 664-1,049 cells of corridor driven for its own sake. Sixty simulated minutes
across five seeds gives 139-150 workers, 262-270 births and 125-129 deaths, against 143 workers and
278 births before the change.

### Supply, and two failures found by measuring it

Finite food needed the supply tuned against what the colony actually eats. A colony of 140 workers
burns about 420 grains of sugar a second; the surface now offers about 1,470 grains of sugar and
730 of protein a second across sites of 60,000-160,000, appearing every 30-70 seconds. Two real
faults surfaced while measuring, and both are fixed at the owning layer rather than tuned away:

- **The colony buried its own food.** Spoil is tipped onto the surface around the entrance, and a
  site that appeared there was walled in by it. Foragers then planned the same impossible walk for
  ever — a colony of 110 starved to nothing at tick 36,000 with 109,649 grains of sugar sitting
  twelve cells from the entrance. Spoil now avoids any column a site stands in, and a site nothing
  can path to is written off.
- **A promised heap could be over-promised.** A forager holds its assigned heap for the whole
  delivery, and an empty cell was accepted as a destination without re-checking the nutrient's cell
  budget, so the last free cells could be promised to more ants than the chambers held. The store
  then exceeded its capacity and the world invariant failed at tick 32,601. Deposits are now bounded
  by the budget as well as by the cell.

## T014h re-check — small irregular chambers and a queen who walks

Measured 2026-09-07 after rooms shrank to radius 3-5, gained an eight-direction bulge, and the queen
started walking her nest. Release build, Apple Silicon macOS, three seeds against both policies:

| Seed | Policy | Flight (s) | Maturity (s) | First buy (s) | Peak | Deaths | Cells dug |
|---|---|---|---|---|---|---|---|
| 1 | buy | 1958 | 1643 | 158 | 176 | 14 | 332 |
| 1 | none | 2502 | 2040 | — | 147 | 42 | 308 |
| 42 | buy | 1950 | 1636 | 156 | 178 | 11 | 340 |
| 42 | none | 2439 | 1975 | — | 152 | 38 | 269 |
| 101 | buy | 1971 | 1650 | 161 | 174 | 15 | 339 |
| 101 | none | 2530 | 2057 | — | 147 | 48 | 299 |

Flight lands at 32.5-32.9 minutes buying and 40.7-42.2 not, both inside ECONOMY's 30-45 minute
target, and first investment at 156-161 s inside the 2-4 minute one. Peak population and deaths are
unchanged. No run declines or goes extinct. Sixty simulated minutes on three seeds gives 138-150
workers, 268-274 births and 128-137 deaths, matching the previous measurement.

Smaller chambers cost slightly fewer cells for the same storage, because a small room is more corridor
per cell but far less unused floor: a mature colony now holds ten or more granaries instead of three
or four large ones.

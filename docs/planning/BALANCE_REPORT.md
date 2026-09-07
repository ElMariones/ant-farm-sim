# Seeded balance report — T013

Measured 2026-09-07. Release build, Apple Silicon macOS (Apple Clang 21), production settings.
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
| No purchase | 40.1–40.3 min (mean 40.2) | mean 32.5 min | — | 146–152 | 40–47 | 4 |
| Buy cheapest | 30.3–30.9 min (mean 30.5) | mean 25.8 min | 2.7–2.9 min | 177–183 | 8–14 | 4 |

Both policies land inside ECONOMY's 30–45 minute first-prestige target on all five seeds, and the
first investment lands inside the 2–4 minute target. **All ten runs survive**; none declines or
goes extinct.

Investment is worth roughly **ten minutes off the run** (a 24% reduction), nearly **four times
fewer deaths**, and about **30 more workers at peak**. The dominant reported bottleneck in every
run is "Brood needs nursing".

## Per seed

| Seed | Policy | Flight (s) | Maturity (s) | First buy (s) | First birth (s) | Peak | End | Deaths |
|---|---|---|---|---|---|---|---|---|
| 1 | none | 2412 | 1946 | — | 268 | 148 | 147 | 42 |
| 1 | buy | 1826 | 1549 | 163 | 268 | 177 | 177 | 14 |
| 7 | none | 2416 | 1949 | — | 239 | 149 | 142 | 47 |
| 7 | buy | 1817 | 1541 | 162 | 239 | 179 | 178 | 13 |
| 42 | none | 2406 | 1949 | — | 230 | 151 | 149 | 40 |
| 42 | buy | 1852 | 1567 | 166 | 230 | 183 | 183 | 8 |
| 101 | none | 2410 | 1944 | — | 188 | 146 | 143 | 46 |
| 101 | buy | 1829 | 1555 | 175 | 188 | 181 | 181 | 10 |
| 2026 | none | 2411 | 1951 | — | 275 | 152 | 149 | 40 |
| 2026 | buy | 1821 | 1540 | 164 | 275 | 181 | 181 | 10 |

Spread across seeds is tight: 10 s between the slowest and fastest no-purchase flight, 35 s
between buy-cheapest flights. Every run reaches flight with the minimum eligible payout of 4,
because the policy flies immediately rather than waiting for a larger reward.

## Second-run proof

Same seed, same action policy, one permanent trait owned at founding.

**Vigor I (egg duration x0.90) reduces time to first birth on all five seeds:**

| Seed | Base (s) | Vigor I (s) | Delta |
|---|---|---|---|
| 1 | 268.0 | 262.0 | **−6.0** |
| 7 | 239.0 | 218.0 | **−21.0** |
| 42 | 230.0 | 209.0 | **−21.0** |
| 101 | 188.0 | 183.0 | **−5.0** |
| 2026 | 275.0 | 256.0 | **−19.0** |

This is the required demonstration that a second run is measurably different.

**Industry I (dig rate x1.15) does not reduce time to 100 excavated cells.** Measured under the
no-purchase policy so the trait is not diluted by bought excavation levels:

| Seed | Base (s) | Industry I (s) | Delta |
|---|---|---|---|
| 1 | 1165.0 | 1086.0 | −79.0 |
| 7 | 1083.0 | 1220.0 | +137.0 |
| 42 | 1111.0 | 1244.0 | +133.0 |
| 101 | 1066.0 | 1195.0 | +129.0 |
| 2026 | 1067.0 | 1150.0 | +83.0 |

Four of five seeds get **slower**, mean +80.6 s. The mechanism is a feedback loop rather than
noise: digging faster grows reachable nest air sooner, nest air raises food store capacity, and the
foraging stimulus targets 50% of current capacity — so a larger nest pulls workers off digging and
onto foraging. Time to an excavation target is therefore governed by task allocation, not dig
speed, and a dig-rate trait cannot move it.

ECONOMY expects Industry I to reduce time to a stated excavation target. **It does not, and this is
recorded as a failure rather than reported as success.** It is not a correctness bug: the trait is
applied exactly once and does raise the dig rate (a level-10 excavation colony digs 173 cells where
a level-0 colony digs 120 over the same 24,000 ticks). Options for a follow-up are to measure
Industry against a metric it can actually move (spoil delivered, or time to a nest-air target), or
to decouple store capacity from nest air so excavation is not self-limiting. Left open; it does not
block M4's gate, which asks that *a* second-run metric improve.

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

## Open balance questions

- Industry I has no metric that improves, as recorded above.
- Every run flies at the minimum payout of 4 because the policy flies immediately. Payouts of 6 and
  8 are verified arithmetically in unit tests but have not been reached in a measured run; that
  needs a policy that waits for 600 births and 6 winged queens.
- Larval starvation is reported at some point in nearly every run. Colonies recover, but the
  larva ration may be tighter than intended.
- These runs stop at first flight, so nothing is measured about a colony left running much longer.

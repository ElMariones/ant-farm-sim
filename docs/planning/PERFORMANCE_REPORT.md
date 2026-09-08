# 20x performance — T015a, 2026-09-08

Baseline: `3d81029`. Apple M5, macOS 26.6.2, Apple Clang 21.0.0. The running game was confirmed to
be `build/dev/ant_farm` (Debug). A five-second CPU sample of that process found pathfinding array
initialization and full terrain painting among its major costs. The live process was read only;
its colony was not restarted, changed or used as a benchmark fixture.

## Implemented changes

- Each World owns reusable A* scratch. Search stamps initialize only visited cells; costs, parents
  and heap capacity survive between searches. No path decisions or topology results are cached.
  The same ordering, expansion limits and route bias remain. Scratch is neither saved nor hashed.
- Terrain keeps its CPU pixels and GPU texture. Only changed 32x32 chunks are repainted/uploaded,
  including a one-cell material halo across chunk boundaries. Both old and new room bounds and
  seed changes invalidate correctly. An interior one-cell cut repaints 1,024 cells rather than
  all 82,944; a boundary/corner cut touches at most three chunks. Pixel formulas are unchanged;
  material color is calculated once per cell instead of once per subpixel.
- Focus eligibility reads the existing worker counter instead of building another actor snapshot
  for every view. The public condition is unchanged.
- `play.command` and README default to Release for normal play. Debug remains a separate preset.
  Both executables are built; an already-running executable needs a normal close/relaunch.

The Simulation Limited indicator and fixed-tick scheduling have not been weakened. Work still
runs at 20 Hz with whole ticks; 20x requires 400 ticks per real second.

## Seeded before/after measurement

`ant_benchmark --seed 42 --warmup 30000 --ticks 6000`: production settings, no purchases, 110 workers
at the start and 130 at the end. Warm-up is outside timing. Samples cover 6,000 individual ticks and
900 view constructions, the 400 ticks / 60 views per second ratio for 20x. Percentiles use the
nearest rank of the sorted individual tick durations. Baseline executables were retained before
source optimization. Same machine, with the user's original game still running in the background.

| CPU metric | Debug before | Debug after | Release before | Release after |
|---|---:|---:|---:|---:|
| Mean tick, ms | 2.080 | 0.429 | 0.046 | 0.045 |
| p95 tick, ms | 9.725 | 1.076 | 0.206 | 0.195 |
| p99 tick, ms | 25.328 | 1.555 | 0.312 | 0.305 |
| Maximum observed tick, ms | 81.787 | 7.223 | 0.542 | 1.923 |
| Simulation + view per 20x/60-Hz frame, ms | 14.700 | 3.181 | 0.313 | 0.308 |

Debug simulation/view CPU cost falls **4.62x**. Release was already fast in this CPU-only fixture;
its small mean difference is within measurement noise, and its maximum sample increased. Do not
claim a Release simulation speedup from this run. Terrain savings are structural and verified by
invalidation coverage; GPU/upload time and integrated FPS were not measured after the change.

All four runs end with hash `bf1adbee369e407d`, 5,882 recorded path requests, and the same worker/view
counts. This is a short comparative benchmark, not the 60-second integrated measurement required
by the full T015 gate. Debug checks, snapshot comparisons and migration regressions are in STATUS.

## Remaining evidence

No visual QA or live restart was performed, honoring the owner's prior direction. In particular,
new texture uploads are compiled and invalidation-tested, not visually approved. There is no claim
that every population/zoom can sustain 20x, nor a new 1,000/5,000-worker, sanitizer, memory, save-I/O
or two-hour soak result. Autosaves, a hidden/occluded macOS window, background load or large colonies
can still cause the honest Simulation Limited indicator to appear.

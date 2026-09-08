# Ant Farm Sim

A desktop incremental game about nurturing an autonomous ant colony: watch tunnels grow, improve the colony's habits, and send a new generation into the world.

**Status: M0–M4 complete.** The deterministic living colony chooses work from changing needs, lays and nurses brood, excavates connected tunnels, carries spoil, follows bounded food trails, ages, dies, and cleans recoverable remains. It now also earns Work from productive labour, spends it on four ten-level adaptations, and saves durably: closing the game and reopening it continues the same colony, tick for tick. A mature colony raises winged queens, sends a nuptial flight for Genetic Legacy, and founds a stronger next colony with permanent traits. The final player-facing UI, profiling and packaging remain in M5.

## Start here

- [Project brief](ant-colony-sim-design.md): vision, scope, and design decisions.
- [AGENTS.md](AGENTS.md): instructions for every implementation session.
- [Game design](docs/design/GAME_DESIGN.md): player experience, systems, progression, and failure.
- [Visual and interaction design](docs/design/ART_AND_UX.md): screen layout, controls, art, and feedback.
- [Architecture](docs/engineering/ARCHITECTURE.md): stack, module boundaries, proposed files, and APIs.
- [Simulation specification](docs/engineering/SIMULATION.md): tick order, agents, terrain, and resources.
- [Economy specification](docs/design/ECONOMY.md): initial numbers and exact progression rules.
- [Save and command contracts](docs/engineering/PERSISTENCE.md): state, validation, and crash-safe prestige.
- [Implementation backlog](docs/planning/BACKLOG.md): ordered work packets and acceptance criteria.
- [Validation plan](docs/engineering/VALIDATION.md): headless tests, benchmarks, and visual review.
- [Project status](docs/planning/STATUS.md): current state and next task.
- [Audit and next steps](docs/planning/NEXT_STEPS.md): commit review, defects, tunnel/movement design and ordered M5 acceptance gates.
- [Balance report](docs/planning/BALANCE_REPORT.md): measured seeded runs, pacing, and second-run proof.
- [Decision log](docs/planning/DECISIONS.md): rationale and deferred features.
- [Session handoff](docs/planning/HANDOFF_TEMPLATE.md): reusable implementation-session checklist.

The implemented stack is **C++20, raylib, EnTT, CMake, Catch2, and nlohmann/json**. macOS on Apple Silicon is the first development target; the headless simulation target has no raylib dependency. Exact versions, revisions, hashes, and licenses are recorded in [DEPENDENCIES.md](docs/engineering/DEPENDENCIES.md).

## Build and play

Requirements: CMake 3.25+, a C++20 compiler, Git, and network access for the first configure. The tested macOS generator is Unix Makefiles, so Ninja is optional.

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
./build/dev/ant_farm --seed 42
```

The colony is drawn as a real cross-section. Roots run down from the turf and can be mined, slowly — four times the cost of soil — while scattered stone lenses through the deep ground can never be removed and passages have to bend around them.

The colony digs for a reason. Every excavation is a **room** it decided it needed — a brood room or a granary — and the corridor that reached it, so the nest is chambers joined by passages rather than wandering lines. Chambers are small and none of them is a perfect circle: each bulges and pinches by a cell, so a mature colony is a cluster of a dozen or more irregular pockets rather than a few caverns. When a room is half full the colony widens it; when it can be widened no further, it sites another one further out, and it keeps roughly one cradle for every two workers as it grows.

Passages now branch from the nearest reachable part of the nest and bend around permanent stone.
Their routes stay planned until workers finish the edges, so a thin pilot tunnel develops into a
proper passage. Once a colony has food and spare workers, it can connect two rooms whose existing
journey is needlessly long. These cross-passages make loops through the nest; brood and storage
space always take priority. Dig focus brings this work forward, while Food and Brood focus defer it.
The activity panel explains the current construction purpose and announces completed passages.
See [nest network design](docs/design/NEST_NETWORK.md) for the rules and remaining validation.

The queen walks. She settles, then makes for a cradle in a brood room or takes a turn about her own chamber, at a third of a worker's pace — and she lays where she stands, so a nurse has to carry each egg on from wherever she happened to be. Every grain the colony owns is a visible heap on a real cell in a granary, the queen lays where she stands and a nurse carries each egg to a free cradle in a brood room, and store capacity is exactly the room the colony has dug.

Food runs out. A forage site is a fallen berry or a dead beetle with a finite amount in it, and when the last grain is carried away it is gone. New ones appear along the surface, and the colony does not know they are there: an undiscovered site is drawn as a faint outline until a scout walks past it. When the colony is short of something it has nowhere left to collect, a few workers go searching while the rest fetch what is still worth fetching or wait — and the moment one finds a site, the whole colony is alerted and steered onto it.

The interface reports back. A flight-readiness bar shows how close the colony is to its one real goal, a five-line log under the inspector says what the colony just did — a worker emerged, scouts found food, a site ran dry, a chamber is finished — and a chip in the header carries the colony's condition where it is readable even with the inspector closed. Digging throws soil, delivered grain lands on its heap, and a worker emerging blooms at the queen; every one of those effects is read off changes in the world rather than pushed from the simulation, and all of them stop dead when the game is paused.

The app supports a resizable 1440×900-requested window (fitted to the visible desktop when necessary), a 1024×640 minimum, pointer-centered wheel zoom, drag pan, ant inspection, pause/resume, and 1×/5×/20× speed. Its calm paper-and-diorama interface uses a high-resolution bundled Nunito atlas with DPI-aware rendering for crisp Retina text. Space toggles pause; `1`, `2`, and `3` select speeds; `I` toggles the inspector; WASD/arrows pan; `+`/`-` zoom.

`F1`–`F4` buy the four run adaptations, `B`/`G`/`X`/`F` set colony focus once twelve workers are alive, and `S` saves immediately. `L` opens the flight and Legacy panel: it shows the readiness checklist and the exact payout while a colony is running, and between colonies it sells permanent traits (`V` Vigor, `Y` Industry) and founds the next colony (`C`). `Enter` sends the flight when every condition is met.

Every action is also a button. Speed, pause, save, focus, adaptations, the flight, permanent traits, founding the next colony, restoring a damaged save and abandoning a run all have pointer controls with drawn vector icons, hover lift, press settle and a hover explanation — including on controls that are currently unavailable, which say why. Abandoning a colony asks for confirmation before it ends the run. While the game is paused the ants hold still: gait animation runs off a clock that only advances with the simulation.

### Saving

The colony autosaves every 30 real seconds and on an orderly exit, into one atomic `profile.json` with a `profile.backup.json` holding the previous validated revision. The default location is `~/Library/Application Support/AntFarmSim/` on macOS, the local app-data directory on Windows, and the XDG data home on Linux. Only one process may write a profile at a time.

If the current save cannot be read, the game opens a paused recovery prompt instead of overwriting it: `R` restores the previous revision and `N` starts a new colony, and either way the unreadable file is kept alongside as `profile.corrupt.N.json` for diagnosis. Nothing is written while that prompt is open. There is no offline progress; a loaded colony resumes at exactly the tick it was saved.

Pass `--save-dir PATH` to use an isolated profile directory, and `--config PATH` to load a different balance file. Every test uses its own temporary save directory.

For simulation-only work:

```sh
cmake --preset headless
cmake --build --preset headless
ctest --preset headless
./build/headless/ant_headless --seed 42 --ticks 2400
```

The headless runner also accepts `--save-dir PATH` with `--save` and `--resume`, and `--verify-round-trip`, which serialises the colony, restores it, and fails if the restored run diverges from an uninterrupted one:

```sh
./build/headless/ant_headless --seed 42 --ticks 4000 --save-dir /tmp/ant --save
./build/headless/ant_headless --seed 42 --ticks 4000 --save-dir /tmp/ant --resume
./build/headless/ant_headless --seed 42 --ticks 3000 --verify-round-trip
```

`--policy none|buy-cheapest` runs a deterministic scripted player and reports run milestones as
JSON; `--vigor N` and `--industry N` found the colony with permanent traits, for second-run
comparisons. Measured results are in the [balance report](docs/planning/BALANCE_REPORT.md).

```sh
./build/headless/ant_headless --seed 42 --ticks 216000 --policy buy-cheapest
```

For an optimized build:

```sh
cmake --preset release
cmake --build --preset release
```

Diagnostic desktop arguments include `--width`, `--height`, `--zoom`, `--fast-forward`, `--start-paused`, `--screenshot`, `--exit-after-screenshot`, `--fps`, `--display-metrics`, `--save-dir`, and `--config`. raylib writes `--screenshot` paths relative to the working directory. Generated builds and screenshots are not source artifacts.

## Scope of the first release

One colony, one stylized species, autonomous foraging and digging, brood development, finite worker lifespans, meaningful run upgrades, save/resume, and voluntary prestige with eight permanent upgrades. The player influences priorities and investment; individual ants execute the work.

No real-money economy, account service, multiplayer, offline income, fluid simulation, or species catalogue is planned for v0.1.

## Repository

[ElMariones/ant-farm-sim](https://github.com/ElMariones/ant-farm-sim)

Every implementation commit should explain the change and its validation. See [contribution workflow](CONTRIBUTING.md). The supplied brief is preserved [verbatim in the archive](docs/archive/original-brief.md); the active documents supersede it.

No project license has been chosen. Do not assume permission to redistribute third-party assets; record licenses when dependencies and assets are introduced.

# Ant Farm Sim

A desktop incremental game about nurturing an autonomous ant colony: watch tunnels grow, improve the colony's habits, and send a new generation into the world.

**Status: M0–M2 complete.** The deterministic living colony now chooses work from changing needs, lays and nurses brood, excavates connected tunnels, carries spoil, follows bounded food trails, ages, dies, and cleans recoverable remains. Progression and saving begin in the next milestones.

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

The app supports a resizable 1440×900-requested window (fitted to the visible desktop when necessary), a 1024×640 minimum, pointer-centered wheel zoom, drag pan, ant inspection, pause/resume, and 1×/2×/5× speed. Its calm paper-and-diorama interface uses a high-resolution bundled Nunito atlas with DPI-aware rendering for crisp Retina text. Space toggles pause; `1`, `2`, and `3` select speeds; `I` toggles the inspector; WASD/arrows pan; `+`/`-` zoom.

For simulation-only work:

```sh
cmake --preset headless
cmake --build --preset headless
ctest --preset headless
./build/headless/ant_headless --seed 42 --ticks 2400
```

For an optimized build:

```sh
cmake --preset release
cmake --build --preset release
```

Diagnostic desktop arguments include `--width`, `--height`, `--zoom`, `--fast-forward`, `--start-paused`, `--screenshot`, `--exit-after-screenshot`, `--fps`, and `--display-metrics`. Generated builds and screenshots are not source artifacts.

## Scope of the first release

One colony, one stylized species, autonomous foraging and digging, brood development, finite worker lifespans, meaningful run upgrades, save/resume, and voluntary prestige with eight permanent upgrades. The player influences priorities and investment; individual ants execute the work.

No real-money economy, account service, multiplayer, offline income, fluid simulation, or species catalogue is planned for v0.1.

## Repository

[ElMariones/ant-farm-sim](https://github.com/ElMariones/ant-farm-sim)

Every implementation commit should explain the change and its validation. See [contribution workflow](CONTRIBUTING.md). The supplied brief is preserved [verbatim in the archive](docs/archive/original-brief.md); the active documents supersede it.

No project license has been chosen. Do not assume permission to redistribute third-party assets; record licenses when dependencies and assets are introduced.

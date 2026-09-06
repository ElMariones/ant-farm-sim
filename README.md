# Ant Farm Sim

A desktop incremental game about nurturing an autonomous ant colony: watch tunnels grow, improve the colony's habits, and send a new generation into the world.

**Status: design and planning only. No playable application, build system, or game tests exist yet.** This repository currently contains the implementation specification for the first playable version. It does not claim benchmark or playtest results.

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

The planned stack is **C++20, raylib, EnTT, CMake, Catch2, and nlohmann/json**. macOS on Apple Silicon is the first development target; portable headless builds are part of the architecture. Dependency versions are selected and pinned during the initial build milestone, not assumed compatible from this document.

## Build and play

Not available yet. Begin with **M0 / T001** in the backlog. That task must add and verify the documented build presets before runnable commands are advertised here.

## Scope of the first release

One colony, one stylized species, autonomous foraging and digging, brood development, finite worker lifespans, meaningful run upgrades, save/resume, and voluntary prestige with eight permanent upgrades. The player influences priorities and investment; individual ants execute the work.

No real-money economy, account service, multiplayer, offline income, fluid simulation, or species catalogue is planned for v0.1.

## Repository

[ElMariones/ant-farm-sim](https://github.com/ElMariones/ant-farm-sim)

Every implementation commit should explain the change and its validation. See [contribution workflow](CONTRIBUTING.md). The supplied brief is preserved [verbatim in the archive](docs/archive/original-brief.md); the active documents supersede it.

No project license has been chosen. Do not assume permission to redistribute third-party assets; record licenses when dependencies and assets are introduced.

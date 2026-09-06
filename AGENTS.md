# Agent instructions — Ant Farm Sim

## Current project and authority

This is a C++20 desktop incremental colony game in its planning phase. The user's current deliverable is a detailed design and implementation handoff; game code has not been started. Do not infer a working build from proposed paths or commands.

Read in this order at the start of a coding session:

1. `docs/planning/STATUS.md` — implemented state, next task, blockers.
2. `docs/planning/BACKLOG.md` — choose the smallest ready task and its acceptance criteria.
3. `ant-colony-sim-design.md` — product boundaries.
4. Only the design/engineering documents linked by that task.
5. Actual code, tests, and Git changes relevant to the task.

`docs/archive/original-brief.md` is historical and superseded. `README.md` is the navigation and real setup guide. Active specs define intended behavior; actual code and recorded test output define what has been delivered. Do not label a plan as implemented.

## Session workflow

- Inspect `git status --short`, current branch, and recent commits before editing. Preserve unrelated changes.
- Take one ready backlog task unless the user requests a broader scope. Mark it in progress in STATUS with a short scope note; do not mark dependencies complete by implication.
- Implement a thin working slice, including failure paths and the meaningful checks listed for that task.
- If a prerequisite is absent, implement the prerequisite or record a concrete blocker. Avoid speculative frameworks and placeholder systems for later milestones.
- Run scoped checks; update STATUS with commands, outcomes, limitations, and next ready task. Keep completed acceptance evidence concise and reproducible.
- Update affected specs when changing a contract. Record scope/architecture changes in DECISIONS. Use HANDOFF_TEMPLATE for session notes when needed.
- Commit coherent changes with a descriptive subject and body. Do not create empty checkpoint commits or commit generated build outputs, local saves, credentials, or recordings.
- Do not launch paid/hosted/local model inference without fresh user permission. Do not delegate to subagents unless the user or an applicable instruction explicitly requests it.

## Git identity and publishing

Repository: `https://github.com/ElMariones/ant-farm-sim.git`.

All commits must be authored as `ElMariones <mariolandaburuclares@gmail.com>`. Check the effective Git identity and set repository-local `user.name` and `user.email` if needed. Use plain `git commit`; never use `git -c user.name=...` / `-c user.email=...` or infer identity from a signed-in account.

Each commit body describes what changed, why, and what was verified. Push only to the requested repository within the user's authorized scope. Never force-push or discard other work. Check the remote for new work before publishing.

## Architecture rules

- Simulation and game rules have no raylib, UI, wall-clock, filesystem, network, or global RNG dependency.
- `sim` owns physical state. `game` owns progression and command validation. `persistence` owns serialization and file replacement. `presentation` reads state and emits commands. `app` coordinates them.
- One thread and fixed 20 Hz simulation for v0.1. Rendering can run at 60 Hz. Speed changes the number of whole ticks, never the size of a tick.
- Use stable monotonic entity IDs, explicit units, bounded containers/inputs, and seeded serializable RNG. Never serialize EnTT IDs or raw memory layouts.
- Use named systems and plain data. No custom engine, generic event bus, service locator, DI framework, scripting runtime, or database.
- Validate player commands in game code. UI buttons never directly adjust resources, tiers, or colony counts.
- Exact integer accounting for resource costs and Legacy. Food has explicit physical locations; taking it out of the world and crediting stores cannot duplicate it.
- Prestige, upgrade purchase, and ordinary saving must follow PERSISTENCE. Do not reintroduce independent run/meta files.
- Keep gameplay content in validated data once the relevant system exists. Avoid precreating an empty file for every proposed path.
- Pin dependencies to verified immutable revisions with license records. No moving branch dependency pins or silent engine upgrades.

## Quality and scope

- Fix bugs at the owning layer. Tests should demonstrate behavior or invariants, not mirror internal implementation.
- A compiler pass is not visual QA. Renderer/UI tasks require opening the game, checking interaction at relevant sizes, and recording screenshots when tools permit. State any unavailable checks.
- Performance targets in VALIDATION are goals until measured. Record machine, build type, seed, population, and tick count for claims.
- Do not add cave-ins, weather, combat, offline progress, multiple colonies, cloud saves, or extra species while implementing v0.1.
- Avoid speculative biology claims. This is a fictional generalist species with compressed development, not a scientific simulator.
- Keep public UI focused on decisions, colony state, and observable consequences. Debug IDs, ticks, seeds, and budgets belong in diagnostics.

## Build commands

There are none yet. M0 must introduce and verify `dev`, `headless`, and `release` CMake presets plus CTest integration. Thereafter read actual commands from README, not from remembered commands in another repository.

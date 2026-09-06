# Development workflow

Read [AGENTS.md](AGENTS.md) before changing code. Work follows the [backlog](docs/planning/BACKLOG.md), with current evidence in [STATUS](docs/planning/STATUS.md).

## Small implementation sessions

Choose a ready task, inspect its owning modules, implement its acceptance criteria, and record what passed. Do not combine a renderer redesign, economy rebalance, and serialization migration in one task. If a contract must change, edit the relevant specification alongside the code.

Prefer one commit per coherent task; larger tasks can use independently understandable commits. Include a body even for a single planning commit:

```text
feat(sim): deliver food through autonomous round trips

Add reachable food reservations, return routing, and store delivery.
This connects worker movement to the colony's actual nutrition supply.

Validation: headless forage-round-trip and blocked-source tests pass.
Limits: visual carrying feedback follows in T004.
```

Use the required Git identity in AGENTS. Do not claim tests that were not executed. For documentation changes, check local links, contradictory contracts, and Git whitespace errors; a game build is not applicable before source exists.

## Review checklist

- The task's observable behavior is implemented, including invalid/unavailable actions.
- Ownership boundaries and resource accounting remain intact.
- Relevant tests and visual checks have evidence or an explicit limitation.
- README and STATUS accurately describe current capabilities.
- No unrelated edits, local saves, secrets, downloaded dependencies, or build outputs are staged.

No license has been selected for the project itself. Add third-party attribution when introducing a dependency, font, sound, or sprite. Do not choose a project license on the owner's behalf.

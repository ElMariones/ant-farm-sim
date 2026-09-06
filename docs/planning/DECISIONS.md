# Decision log

Accepted design baseline, 2026-09-06. These are product/architecture decisions, not completed implementation.

| ID | Decision | Reason / consequence |
|---|---|---|
| D001 | Native C++20 + raylib retained | Matches supplied direction; a headless core protects tests and future renderer options |
| D002 | Build an incremental game around autonomous ants | Early focus/investment gives the player decisions without micromanaging workers |
| D003 | One fictional generalist species and colony | Avoid incompatible biological assumptions and a premature species framework |
| D004 | Start with queen + six workers | Immediate activity; defer fragile true claustral opening to a later mode |
| D005 | Only explicit player flight grants prestige | Resolves automatic-flight/reset contradiction and makes timing meaningful |
| D006 | Guaranteed integer Legacy and a shared wallet | Predictable first reward; clear branch opportunity cost; no random prestige payout |
| D007 | One atomic profile, save before prestige | Avoid split-file duplicate/lost rewards; save/resume becomes an early milestone |
| D008 | No offline progress in v0.1 | Exact ant simulation is not cheaply extrapolatable without another tested model |
| D009 | Fixed ticks, explicit RNG, stable IDs | Reproduction, replay, and save continuity are first-class correctness requirements |
| D010 | No cave-ins or fluid model | Archive's connectivity heuristic cannot establish structural stability; defer complex hazards |
| D011 | UI draft early, polished UI before release | Visual legibility is a milestone gate; debug tooling cannot be final product design |
| D012 | Separate game rules from disk and rendering | Prestige logic must not call UI/save directly, unlike archive pseudocode |
| D013 | Pin verified dependencies at M0 | Upstream has changed since archive tags; planning-time research is not integration testing |
| D014 | No Queen age death in first release | Prevent automatic run loss during relaxed observation; starvation remains recoverable by restart |
| D015 | Work measures productive worker time | Keeps upgrades tied to actual visible activity with integer accounting |
| D016 | Permanent traits modify parameters, not fundamental AI access | All runs can excavate/use essential tasks; prestige should improve an existing loop |

## Risks and resolution points

- **Pacing:** proposed rates may mature too quickly/slowly. T008 measures local progression; T013 tunes complete production-setting runs before release polish is closed.
- **Emergent behavior:** reasonable local rules can produce deadlocks or ugly tunnels. Test target abandonment, low-population recovery, and screenshots in M1/M2 before adding more systems.
- **Deterministic save:** paths, reservations, RNG and update ordering are easy to omit. T009 continuation hashes block durable progression work until correct.
- **Performance:** all-agent visualization at 5,000 workers is a stretch budget. T015 measures before threading or crowd abstraction.
- **Desktop UI accessibility:** raylib text/buttons do not automatically expose native accessibility. Support keyboard/scaling/reduced motion and document the remaining limitation.
- **Biological accuracy:** archive claims were not independently audited here. Player copy should say “inspired by ant colonies”; scientific educational claims require primary-source review.

## Decisions reserved for later

The owner should choose the project license and any final commercial name. Dependency pins await M0 compatibility tests. Signing/notarization and public binary release await packaging/user release intent. Music, paid assets, and model-generated art are not necessary to execute the current plan.

When changing an accepted decision, append a dated entry with the previous rule, new rule, rationale, affected tasks/contracts, and migration/validation consequences. Do not silently edit the archive to make history agree with the new design.

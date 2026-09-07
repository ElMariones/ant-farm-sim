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
| D018 | Work threshold retuned from 200 to 1,200 productive ticks per Work (2026-09-07) | T008 measured the first purchase arriving at a mean of 29.2 simulated seconds against ECONOMY's 2–4 minute target, because starting workers are productive about 85% of the time rather than the 50% the original estimate assumed. Measured 800/1,000/1,200/1,400 on seeds 1, 7, 42, 101 and 2026 and adopted 1,200 (mean 167.8 s, range 154.2–183.6 s). D015 is unchanged: Work still measures productive worker time. ECONOMY carries the before/after table; the Industry tier-4 trait scales with it (1,200 -> 960); T013 revisits this with the full purchase policy |
| D017 | Query macOS display geometry before creating the raylib window | raylib 6.0 reports physical dimensions after a post-creation resize on Retina, corrupting logical layout; the app layer may use a narrow AppKit adapter to choose one safe initial size while sim/game/presentation remain platform-independent. Pointer coordinates remain raylib-owned because live Retina checks confirmed they are already logical. The `.app` package itself remains deferred to T016 |

## Risks and resolution points

- **Pacing:** proposed rates may mature too quickly/slowly. T008 measures local progression; T013 tunes complete production-setting runs before release polish is closed.
- **Emergent behavior:** reasonable local rules can produce deadlocks or ugly tunnels. Test target abandonment, low-population recovery, and screenshots in M1/M2 before adding more systems.
- **Deterministic save:** paths, reservations, RNG and update ordering are easy to omit. T009 continuation hashes block durable progression work until correct. *Resolved for M3:* save/load continuation reproduces uninterrupted hashes in unit tests, in the headless runner, and across a real app quit and relaunch.
- **Performance:** all-agent visualization at 5,000 workers is a stretch budget. T015 measures before threading or crowd abstraction.
- **Desktop UI accessibility:** raylib text/buttons do not automatically expose native accessibility. Support keyboard/scaling/reduced motion and document the remaining limitation.
- **Biological accuracy:** archive claims were not independently audited here. Player copy should say “inspired by ant colonies”; scientific educational claims require primary-source review.

## Decisions reserved for later

The owner should choose the project license and any final commercial name. M0 dependency pins and their license records are now fixed in `docs/engineering/DEPENDENCIES.md`; changes require a new compatibility check and decision entry. Signing/notarization and public binary release await packaging/user release intent. Music, paid assets, and model-generated art are not necessary to execute the current plan.

When changing an accepted decision, append a dated entry with the previous rule, new rule, rationale, affected tasks/contracts, and migration/validation consequences. Do not silently edit the archive to make history agree with the new design.

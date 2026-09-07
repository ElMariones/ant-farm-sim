# Ant Farm Sim — project brief

**Revision:** 2026-09-07. **Stage:** M0–M4 systems implemented; M5 incomplete. **Working title:** Ant Farm Sim.

## Pitch

A living cross-section of an ant farm becomes a small incremental game. Start with a queen and her first workers, watch them open passages and carry food home, invest in better colony habits, and eventually launch a nuptial flight to earn permanent Genetic Legacy.

The pleasure is seeing the numbers become physical: an excavation investment puts more workers at the tunnel face; better nursing fills a chamber with brood; a successful flight clears the sky and starts a stronger generation.

## Design pillars

1. **A colony worth watching.** Readable ants, organic excavation, carried food, brood chambers, and changing density matter more than a sprawling upgrade menu.
2. **Autonomy with influence.** Ants choose and perform jobs. Players choose a colony focus, buy adaptations, and decide when to end a run.
3. **Visible causes.** Show why growth stalls: nutrition, nursing, nursery space, or queen output. Never hide a required action behind an unexplained population cap.
4. **A reliable incremental loop.** Progress saves early. Prestige is voluntary, previewable, and credited exactly once.
5. **Small, testable systems.** A headless simulation owns game state; graphics and persistence adapt it without controlling the rules.

## Intentional changes from the supplied brief

| Original tension | Active decision |
|---|---|
| Passive viewer plus a late prestige appendix | Incremental colony game from the first vertical slice; observation remains the main experience |
| No player decisions until a full reproductive simulation | Focus and four run upgrades provide early decisions |
| Both automatic flight/new colonies and player-triggered reset | One colony per run; only a confirmed prestige command ends a successful run |
| Save system introduced near the end; two independent files | Save/resume before progression; one atomic profile contains run and meta state |
| Population/queen RNG can yield no useful first prestige | Guaranteed deterministic payout after explicit maturity conditions |
| Branch costs described as independent despite shared currency | One Legacy wallet; branches have separate prerequisites and compete for spending |
| Major worker castes and several species mixed together | Fictional monogyne generalist with ordinary workers and cosmetic nanitics |
| Structural connectivity presented as physical stability | Cave-ins deferred; simple connectivity is not a soil mechanics solver |
| Queen-only fragile opening | Standard mode starts with queen + 6 workers; true claustral founding deferred |
| 50x acceleration assumed | 1x/5x/20x implemented; sustained rates require measured budgets |

The [original](docs/archive/original-brief.md) is retained for provenance, not implementation authority. Biological mechanisms inspire this game; its compressed timings and combined traits are deliberate abstractions. Do not repeat unsourced species-specific claims from the archive as established fact.

## Product boundary

v0.1 targets a satisfying 30–45 minute first prestige at 1x, with visible activity in the first 10 seconds and a first investment in roughly 2–4 minutes. Headless first-investment and flight measurements are recorded in the balance report; visible activity and player experience still require the M5 review gates.

The player can keep a mature colony running instead of prestiging. There is no forced queen aging death, timed login reward, or punishment while the application is closed. A failed colony can restart for free without losing permanent progress.

See [game design](docs/design/GAME_DESIGN.md), [economy](docs/design/ECONOMY.md), and the [backlog](docs/planning/BACKLOG.md) for implementation authority. When active documents conflict, resolve the conflict and record the decision before dependent code changes.

# Game design — v0.1

Status: accepted planning baseline; tuning remains provisional. Numeric authority is [ECONOMY](ECONOMY.md).

## The experience

You are tending a formicarium, not commanding an army. The camera begins at a small chamber underneath grass. Six workers explore, carry bright food fragments home, and expand a narrow passage. The queen and brood provide a readable center of activity. Within minutes you choose how to invest the colony's accumulated Work.

There is no repeat-click income mechanic. The simulation earns everything. Between decisions the farm should be pleasant to observe, with enough explanation to understand the next bottleneck.

## The three loops

| Loop | Duration target | Player experience |
|---|---|---|
| Observation | Seconds | Ant finds food, follows a route, carries it to storage; digger returns with spoil |
| Colony development | Minutes | Focus, investment, new workers, visible chamber expansion, first winged queen |
| Generational growth | 30–45 minutes first run | Preview Legacy, choose flight timing, buy permanent traits, found a fresh colony |

All times mean foreground play at 1x. Pausing or closing suspends progression. At 5x biological and economic clocks both advance five times as fast if the machine sustains the tick budget.

## First-session sequence

1. New colony opens immediately with queen + 6 nanitic workers, a connected starter nursery, food stores, and two reachable food sources. No mandatory modal tutorial.
2. A short objective says “Watch a worker bring food home.” Completing a real delivery reveals the Work counter; an objective itself grants nothing.
3. At 15 Work, suggest the first investment and show the resulting modifier. The player may choose another affordable investment or wait.
4. When growth stalls, the nursery panel explains the limiting condition and points to a focus or upgrade that can help.
5. At 12 workers, show colony focus controls; before this the default is Balanced. Sandbox/debug access is separate.
6. At 50 workers, show a disabled Flight tab with exact maturity conditions and progress.
7. When flight is ready, quietly highlight it. The colony continues until the player confirms.

Onboarding tracks observed facts; loading a save never grants duplicate rewards or blocks a feature already unlocked. Hints can be dismissed. Milestones are derived from run statistics, not a second currency.

## Player verbs

| Action | Behavior | Constraint |
|---|---|---|
| Observe/inspect | Pan, zoom, select ant/chamber/source, follow selection | Cannot alter simulation state |
| Change focus | Balanced, Growth, Expansion, or Foraging biases task response weights | Free; 10 sim-second cooldown; cannot disable essential jobs |
| Invest Work | Purchase one level in one of four run adaptations | Validated cost; immediate modifier; levels reset at prestige |
| Adjust time | Pause, 1x, 2x, 5x | No special rewards; always same fixed ticks |
| Launch flight | End eligible run and credit shown Legacy | Confirmation pauses; commit must save before reset is accepted |
| Spend Legacy | Buy next tier in either permanent branch between runs | Shared wallet; sequential prerequisites |
| Start next colony | Create new seeded plot with owned modifiers | Costs nothing |
| Restart failed/current colony | End a run without Legacy | Explicit confirmation; permanent progress preserved |

Food placement, teleportation, forced maturation, and terrain painting are debug/sandbox tools. Using one marks the run assisted and disables Legacy earnings. The UI must tell the user before the first assisted mutation. Standard mode provides renewable food without manual tending.

## Colony phases and end states

Standard runs begin in **Growth** with a living queen and six workers. Nanitics use the ordinary worker logic with a visual size difference, not a permanent efficiency penalty in v0.1.

**Mature** is latched when living workers >= 100, workers born this run >= 150, and age >= 12 simulated minutes are simultaneously true. Falling population does not revoke maturity. The colony then allocates one in five newly laid eggs to winged queens, provided there are fewer than 10 live winged queens or winged-queen brood combined. The remaining eggs are workers. The exact assignment sequence is saved.

**Flight ready** is a derived condition, not a separate mutually exclusive phase: Mature + at least 3 live winged queens + living queen + unassisted run. Winged queens take twice each worker development duration and do not work; mating and male populations are abstracted.

**Decline** begins if the queen dies from sustained starvation. She does not age to death in v0.1. Surviving brood can mature. **Extinct** means no queen, workers, live brood, or winged queens remain. During Decline or Extinct, offer a free restart without Legacy; retaining a save of the run is optional, never required for recovery.

Flight closes the current run; it never spawns a second active colony. Between runs the player visits the Legacy shop, then starts a new run. Failure/reset does not count as successful prestige.

## Living-world scope

- Grid cross-section with sky, surface, diggable soil/clay, roots and bedrock obstacles.
- Four worker jobs: forage, excavate, nurse, and clean. Idle/explore are behavior states.
- Connected tunnel expansion and a compact central nursery. Nursery capacity scales with reachable dug area; no automatic functional chamber classification in v0.1.
- Carbohydrate and protein stores, brood progression, worker upkeep, aging and starvation, corpses and midden delivery.
- One food trail channel, evaporation, and finite search patience. No combat or alarm channel yet.
- Spoil heaps and colony density visibly change over a run. Dirt is visual material, never a spendable upgrade currency.

## Guardrails against frustrating play

A colony cannot permanently run out of surface food: source refills are deterministic and configurable. Full stores stop pickup rather than deleting excess food; overflow cargo can wait for space. Population pressure slows reproduction through nutrition, labor, and nursery capacity, with an explicit technical population limit only as a final safety bound.

A no-worker state with surviving brood is not declared extinct. Nursing is a speed bonus rather than a mandatory maturation gate; properly fed brood can recover the colony. No random founding failure, spontaneous disaster, or automatic reset is in the first release.

## Release scope and follow-ups

v0.1 must deliver one complete enjoyable run, safe save/resume, and a measurably changed second run. The initial visual slice may omit life cycle and prestige, but is not the release.

Post-release candidates: true claustral mode, relaxed sandbox mode without Legacy, a second species with genuinely different behavior, ambient sound, then weather or local hazards. Offline progression requires a separate resource-consistent model and is deliberately excluded. Multiplayer, a backend, and live-service monetization are not architectural requirements.

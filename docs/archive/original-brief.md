# Ant Colony Simulator — Design Document

**Genre:** passive sandbox simulation (formicarium viewer, not an RTS)
**Reference:** cross-section pixel-art ant farms like the ones you screenshotted — layered soil, organic tunnel networks, a live population counter, no player commands, the colony just runs.
**Target platform:** desktop (macOS-first, cross-platform by construction)
**Language:** C++20

---

## 1. Vision & Core Loop

You are not playing the ants. You seed a queen into a block of diggable substrate, optionally scatter some food, and watch. The colony expands its tunnel network, forages, raises brood, loses members, and eventually throws off new queens. The "gameplay" is the same as the Instagram account you found: a population number ticking up in the corner while an organic tunnel network grows underneath a strip of grass and sky.

Core loop, every simulation tick:

```
sense environment → pick a task (dig / forage / nurse / feed queen / clear corpses / idle)
→ act → deposit pheromone if relevant → age / metabolize → maybe die
```

Nothing here is scripted centrally. Colony-level behavior (population growth, tunnel shape, when reproduction happens) is an *emergent result* of thousands of ants running the same simple per-ant rules. That's the actual design principle, not a simplification — real ant colonies work exactly this way (see §2.3).

---

## 2. Biological Grounding

This section is the "why" behind every mechanic in §4–7. It's a condensed reference, not a biology essay — treat it as your spec for tuning constants.

### 2.1 Life cycle (complete metamorphosis)

| Stage | Real-world duration | What's happening | Sim implication |
|---|---|---|---|
| Egg | ~1–2 weeks | Small, immobile, tended by nurses | Stationary entity, timer-driven |
| Larva | ~1–3 weeks, molts ~3 times | Legless, eyeless, blind, fed by regurgitation, huge appetite | Consumes colony food reserve per tick; growth stalls if unfed |
| Pupa | ~2 weeks | Immobile, non-feeding, body reorganizes; some species cocoon, some (e.g. *Lasius niger*) don't | No upkeep cost, pure timer |
| Adult | — | Pale and soft on emergence, hardens within hours, then takes on a caste role | Caste assigned at eclosion, not fixed at the egg |

Full egg→adult is **6–10 weeks** biologically. That's too slow to be a satisfying sandbox — compress it (see §7.4) but keep the *ratios* between stages, and keep the temperature dependency: warmer chambers = faster development. This gives players (and the ants) a reason to relocate brood.

An unfertilized egg becomes a male; a fertilized egg becomes a worker or a queen depending on larval feeding — this is your reproductive caste-switch hook (§6.4).

### 2.2 Castes

| Caste | Role | Notes for the sim |
|---|---|---|
| Queen | Sole egg-layer (in a monogyne colony) | One per colony by default; egg-laying rate gated by her "fed" status |
| Nanitic workers | The *first* generation of workers a founding queen raises | Visibly smaller, reduced work-rate multiplier — they run identical behavior code, just weaker stats |
| Minor/major workers | Foraging, nursing, digging, general labor / larger-bodied defense specialists | Most of the population; this is where task allocation (§2.3) lives |
| Male alates | Winged, exist only to mate | Produced late-colony, die within ~2 weeks of the nuptial flight regardless of outcome |
| Female alates (gynes) | Winged virgin queens | Produced late-colony; the ones that mate and survive found new colonies |

### 2.3 Division of labor: the response-threshold model

This is the single most important mechanism to get right, and it's well studied (Bonabeau et al.'s threshold model, still the standard reference). There is **no foreman ant**. Instead:

- Each colony-level need (dig more space, bring in food, feed larvae, clear corpses) produces a **stimulus** that rises the longer it goes unaddressed.
- Each ant has an individual, semi-random **response threshold** per task.
- An ant's probability of switching to a task is a function of stimulus vs. its threshold — low-threshold ants engage at low stimulus, high-threshold ants only jump in when things get bad.
- Ants that just finished a task tend to keep doing it (self-reinforcement); idle capacity naturally flows toward whatever is most neglected.

This single mechanism reproduces colony-level self-repair for free: remove all your foragers and the threshold-model math means other ants pick up slack without any global rebalancing code. Implement this once, correctly, and most of your "colony feels alive" requirement is solved.

### 2.4 Pheromones

Real ants use several distinct chemical channels, not one generic "scent":

| Channel | Function | Key quirk to replicate |
|---|---|---|
| Trail (nest ↔ food) | Recruits foragers to a known food source | Deposited **up to ~20x more heavily near the food** than near the nest — bias deposition by proximity to source, not a flat rate |
| Alarm | Local threat response | Short range, fast decay, causes aggression not attraction once ants are close |
| Recruitment/digging | Marks a promising dig frontier | Optional; lets diggers cluster on one tunnel face instead of scattering |
| Necrophoric (death cue) | Corpse identification | See §2.5 |

Two behavioral quirks worth keeping because they make the sim *feel* right instead of suspiciously optimal:
- Trails are **not corrected** once laid — if a scout marks a dead-end, ants don't "realize" the error and erase it; the trail just evaporates naturally over time. Don't build smart trail-cleanup logic; dumb reinforcement + evaporation is the accurate behavior.
- Recruitment sensitivity scales with colony hunger — a starved colony responds more eagerly to a weak trail than a well-fed one. Cheap, realistic knob: multiply pheromone-following weight by `1 - foodReserve/foodCapacity`.

### 2.5 Death & necrophoresis

Ants don't recognize death by "stillness" — they recognize **oleic and linoleic acid**, fatty acids that appear on a corpse roughly **2–3 days after death** as tissue breaks down. Freshly dead ants are largely ignored (~15% removal rate); by 2–6 days post-mortem removal jumps to ~80%.

Sim rule: a `Corpse` entity carries a `daysSinceDeath` timer. Undertaker-task ants only pick up corpses whose timer has crossed a threshold — this is a free, biologically-grounded way to avoid instant "corpse teleports to graveyard" cleanup and instead get organic-looking pileup during a die-off event.

### 2.6 Colony founding

A mated queen digs a small sealed chamber and goes **claustral**: she does not leave, does not forage, and raises her first brood entirely from stored body reserves (fat body + histolyzed flight muscle). This is a real metabolic trade-off — it's *why* the first workers (nanitics) come out undersized. Only after nanitics eclose and start foraging does the colony "open up."

This gives you a natural, biologically accurate difficulty curve for free: the founding phase is fragile (single point of failure, no income), and the colony only stabilizes once it has workers. Virgin queens fail at a very high rate in nature — most nuptial-flight queens never establish a colony. Reflect that with a low survival roll for player-spawned "wild" queens if you ever let new colonies seed themselves (§6.5).

### 2.7 Digging physics

This is genuinely useful, non-obvious research (Caltech/Andrade et al., X-ray CT of ants excavating soil): ants don't "know" structural engineering, but their digging *algorithm* produces stable tunnels as an emergent side effect:

- They dig in **straight, piecewise-linear segments** at close to the material's angle of repose (as steep as possible without collapsing) — a straight line is the shortest path, so this is closer to laziness than cleverness.
- They **hug existing surfaces** (container walls in the lab, bedrock/roots in nature) because a wall is free structural support.
- Removing a grain **redistributes force through the remaining grains** ("soil arching") — the tunnel wall gets reinforced by the digging itself, which is why colonies can maintain tunnels for decades without any repair behavior.

Translate this into a grid sim as a **structural-stability solver**, not a grain-level physics simulation (see §5.3) — you get the emergent "ants don't collapse their own home" behavior without simulating individual sand grains.

---

## 3. Architecture Overview

```
/src
  /sim              # pure simulation — no rendering, no I/O, unit-testable
    world_grid.h/.cpp        # voxel/cell terrain
    pheromone_grid.h/.cpp    # per-channel diffusion grids
    components.h             # ECS component structs
    task_allocation.cpp      # response-threshold system
    movement.cpp             # pathfinding + steering
    digging.cpp              # excavation + spoil handling
    foraging.cpp              # food pickup/return/trophallaxis
    brood.cpp                # egg/larva/pupa timers, nursing
    colony_manager.cpp       # founding → growth → reproductive phase FSM
    stability.cpp            # collapse solver
    death.cpp                # aging, starvation, corpses
  /render
    renderer.cpp             # raylib draw calls, palette, camera
    debug_ui.cpp             # ImGui sandbox controls
  /app
    main.cpp                 # fixed-timestep loop, window setup
  /data
    species.json             # tunable per-species parameters
```

Keep `/sim` free of any rendering or engine dependency. That's what lets you unit-test the colony logic headless and, later, swap the renderer without touching behavior code.

### 3.1 Recommended stack

| Concern | Library | Why |
|---|---|---|
| ECS | [EnTT](https://github.com/skypjack/entt) | Header-only, fast, trivial to pull in with CMake `FetchContent`, scales to tens of thousands of entities without a custom allocator |
| Windowing/rendering/input | [raylib](https://www.raylib.com/) | Minimal API, great for pixel-art 2D, runs natively on Apple Silicon, no boilerplate |
| Debug/sandbox UI | Dear ImGui + `rlImGui` bridge | Sliders, population graphs, click-to-inspect panels — this *is* your sandbox toolset |
| Build | CMake + Ninja | Standard, `FetchContent` avoids you needing a package manager for a solo project |
| Optional: procedural terrain seeding | [FastNoiseLite](https://github.com/Auburn/FastNoiseLite) | Single header, good for initial soil-layer variation |

Minimal `CMakeLists.txt` skeleton:

```cmake
cmake_minimum_required(VERSION 3.21)
project(ant_colony_sim CXX)
set(CMAKE_CXX_STANDARD 20)

include(FetchContent)
FetchContent_Declare(raylib GIT_REPOSITORY https://github.com/raysan5/raylib.git GIT_TAG 5.5)
FetchContent_Declare(entt GIT_REPOSITORY https://github.com/skypjack/entt.git GIT_TAG v3.13.2)
FetchContent_MakeAvailable(raylib entt)

add_executable(ant_colony_sim src/app/main.cpp)
target_link_libraries(ant_colony_sim PRIVATE raylib EnTT::EnTT)
```

On Apple Silicon: `brew install cmake ninja`, then `cmake -G Ninja -B build && cmake --build build`. raylib's OpenGL backend runs fine on macOS through GLFW; you don't need to touch Metal directly for this project.

---

## 4. World / Terrain Model

### 4.1 Representation

A 2D grid (cross-section, matching the reference images), one cell = one small voxel of substrate. Each cell stores:

```cpp
enum class Material : uint8_t { Air, Topsoil, Sand, Clay, Bedrock, Root, Water };

struct Cell {
    Material material;
    uint8_t  hardness;     // dig-hits required to clear; Bedrock = undiggable
    uint8_t  moisture;     // optional: affects collapse risk, mold, plant roots
    bool     stable;       // written by the stability solver, §5.3
};
```

Layer generation on world creation: grass/topsoil strip at the top, sand for the bulk of the diggable volume, clay/bedrock as an undiggable floor — this alone reproduces the strata look in your screenshots. Use noise (FastNoiseLite) to vary the strata boundary instead of flat lines.

### 4.2 Chunking

Split the grid into fixed-size chunks (e.g. 32×32 cells) for:
- dirty-rect rendering (only redraw chunks that changed)
- localized stability-solver runs (§5.3) instead of scanning the whole world every time
- future multithreading boundaries

---

## 5. Ant Agent Model

### 5.1 Components (ECS)

```cpp
struct Position { float x, y; };
struct Velocity { float x, y; };
struct Caste     { CasteType type; float sizeMultiplier; };
struct Age       { float ticksAlive; float expectedLifespan; };
struct Hunger    { float value; float maxValue; float metabolicRate; };
struct Task      { TaskType current; float taskTimer; };
struct Carrying  { CargoType type; float amount; }; // dirt, food, corpse fragment
struct PathFollow{ std::vector<GridPos> path; size_t index; };
struct Thresholds{ std::array<float, TASK_COUNT> value; }; // per-ant response thresholds
```

Brood are entities too, just stationary and without `Velocity`/`PathFollow`:

```cpp
struct Egg   { float timer; };
struct Larva { float timer; float careSatisfaction; };
struct Pupa  { float timer; };
```

### 5.2 Task selection (response-threshold model, §2.3)

```cpp
float responseProbability(float stimulus, float threshold, float n = 2.0f) {
    float sn = std::pow(stimulus, n);
    return sn / (sn + std::pow(threshold, n));
}

TaskType chooseTask(const Thresholds& t, const ColonyStimuli& s, std::mt19937& rng) {
    for (TaskType task : kCandidateTasks) {
        float p = responseProbability(s.value(task), t.value[task]);
        if (std::uniform_real_distribution<float>(0,1)(rng) < p)
            return task;
    }
    return TaskType::Idle;
}
```

`ColonyStimuli` is computed once per tick per colony, cheaply:

```cpp
struct ColonyStimuli {
    float dig      = desiredChamberSpace - currentChamberSpace;
    float forage   = std::max(0.f, foodTarget - foodReserve);
    float nurse    = countLarvaeNeedingFood();
    float undertaker = countRipeCorpses();
    float defend   = threatLevel;
    float value(TaskType t) const { /* dispatch */ }
};
```

Run task selection when an ant goes idle or finishes its current task — not every tick for every ant. That keeps this cheap even at thousands of ants.

### 5.3 Structural stability solver (digging physics, §2.7)

Instead of simulating individual grains, run a periodic flood-fill from "anchored" cells (bedrock, world border) through connected solid cells. Anything solid that's *not* reachable from an anchor is unsupported and collapses:

```cpp
void StabilitySolver::run(WorldGrid& world) {
    world.markAllSolid(Unstable);
    std::queue<GridPos> frontier;
    for (auto pos : world.bedrockAndBorderCells()) frontier.push(pos);

    while (!frontier.empty()) {
        GridPos p = frontier.front(); frontier.pop();
        Cell& c = world.at(p);
        if (c.isSolid() && c.stable == Unstable) {
            c.stable = Stable;
            for (auto n : world.solidNeighbors(p)) frontier.push(n);
        }
    }
    for (auto& [pos, cell] : world.allSolidCells())
        if (cell.stable == Unstable) world.collapse(pos); // → rubble, falls into open space below
}
```

Run this only on the chunks touched by digging since the last pass, not the whole world every tick. This is what gives you cave-ins if a colony over-mines a support column — an emergent hazard, not a scripted one.

### 5.4 Digging rules

- An ant can only target a solid cell **adjacent to an existing tunnel** (contiguous excavation, matches the "hug existing structure" finding in §2.7).
- Prefer targets that continue a straight run from the ant's current tunnel segment over branching randomly — a simple heuristic (bias toward the frontier cell most aligned with the last N cells removed) reproduces the piecewise-linear digging pattern without any real planning.
- Excavated material becomes `Carrying{Dirt}` cargo; the ant paths to the nearest surface deposit point and dumps it, incrementally building a spoil-heap mound — this is your visible ant-hill, matching the screenshots' surface mounds.

---

## 6. Colony Life Cycle

### 6.1 Phase state machine

```
Founding → Growth (ergonomic) → Reproductive (mature) → Decline (queen dead) → Extinct
```

```cpp
enum class ColonyPhase { Founding, Growth, Reproductive, Decline, Extinct };

void ColonyManager::update(Colony& c, float dt) {
    switch (c.phase) {
        case ColonyPhase::Founding:
            if (c.workerCount >= kNaniticThreshold) c.phase = ColonyPhase::Growth;
            break;
        case ColonyPhase::Growth:
            if (c.workerCount >= kMaturityThreshold && c.dayCounter >= kMinMaturityDays)
                c.phase = ColonyPhase::Reproductive;
            break;
        case ColonyPhase::Reproductive:
            maybeProduceAlates(c, dt);
            maybeTriggerNuptialFlight(c);
            break;
        case ColonyPhase::Decline:
            if (c.workerCount == 0) c.phase = ColonyPhase::Extinct;
            break;
    }
}
```

### 6.2 Founding

- Queen entity spawned with an internal `reserves` stat (replaces normal hunger during this phase).
- She digs a small claustral chamber, lays 5–20 eggs, cannot leave the chamber, consumes `reserves` instead of colony food.
- First workers eclosing get `Caste::NaniticWorker` (size multiplier ~0.6, same behavior code as regular workers).
- If `reserves` hit zero before the first nanitic ecloses, the queen dies and the colony fails — this is your founding-phase fail state, and it's biologically accurate (most founding queens fail in nature).

### 6.3 Growth

- Standard egg-laying loop, rate gated by queen's fed status (she needs to be fed via trophallaxis by nurse-task ants once workers exist).
- Population grows until it hits the maturity threshold you set in `species.json`.

### 6.4 Reproductive phase

- A configurable fraction of new brood is diverted to alate development instead of worker development (larval feeding determines this, per §2.1) — expose this as a tunable ratio, not a hard switch.
- Alates are non-working; they wait in the nest until a nuptial-flight trigger condition (simple version: a day-counter + random weather roll; realistic version: "warm, humid, low wind" if you build a weather system later).

### 6.5 Nuptial flight & new colonies

```cpp
void triggerNuptialFlight(Colony& c) {
    for (auto& alate : c.alates) {
        alate.leavesWorld();
        if (rollSurvival(kQueenSurvivalRate)) // deliberately low — see §2.6
            spawnFoundingQueen(pickLandingSpot());
    }
}
```

Most alates should simply vanish (predation/failure, matches real attrition). The ones that survive spawn a brand-new `Founding`-phase colony elsewhere on the map — this is what turns a single-colony toy into a multi-colony sandbox for free, once you want it.

### 6.6 Decline & death of the colony

No queen-replacement by default (monogyne assumption) → once the queen dies, the colony can coast for a while on existing brood/workers but population trends to zero. Leave a hook (`Colony::allowsQueenReplacement`) for polygyne species later (§8).

---

## 7. Feeding Cycle

- Food sources are entities in an above-ground "foraging fringe" area: seeds (carbs, for adults), insect prey/protein items (needed for larval growth).
- Foragers follow the trail-pheromone gradient outward, pick up a food entity, and return depositing trail pheromone heavily near the source (§2.4).
- On return, food goes into a colony `foodReserve` pool — model trophallaxis abstractly at v1 (instant redistribution from the pool to hungry ants/brood within nest range) rather than simulating individual liquid exchange. That's a fine simplification; add crop-based liquid transfer as a stretch feature (§8) if you want the mouth-to-mouth animation.
- Brood consumes protein-weighted food; adults consume carb-weighted food. Track both pools separately if you want starvation to differentiate "no workers to send out" vs. "no protein for the larvae" as distinct failure modes.

## 8. Death Cycle

| Cause | Trigger |
|---|---|
| Aging | Probability of death rises near `expectedLifespan` (Gompertz-style curve, not a hard cutoff — keeps population from dying in visible waves) |
| Starvation | `Hunger.value` hits 0 for N consecutive ticks |
| Cave-in | Ant standing in a cell the stability solver collapses |
| Predation/combat (stretch) | External threat entity |

On death → `Corpse` entity at last position, `daysSinceDeath = 0`. Undertaker-task ants only engage once the necrophoric timer crosses threshold (§2.5), then carry it to a midden pile. Decomposed corpses can just disappear at v1, or feed a nutrient/fungus mechanic later (§9).

---

## 9. Sandbox Tools & UI (the part that makes it feel like the reference account)

- **Population badge**, top-left, live count — this is literally the "ANT POP." tag in your screenshots. Cheap, high impact.
- **Speed control**: 1x / 5x / 50x time-scale slider — this is what turns "6–10 week life cycle" into something watchable.
- **Click-to-inspect**: click any ant → panel with caste, age, current task, hunger. Great for debugging *and* it's genuinely fun to watch.
- **Day/night cycle**: cheap ambient lighting pass over the sky strip, ties into brood-development temperature dependency (§2.1).
- **Place food** / **place obstacle** tools — the only two "interactions" a sandbox like this needs; everything else should be observation.
- Build all of this in ImGui first. Don't invest in custom-styled UI until the sim itself is worth watching.

---

## 10. Roadmap

| Milestone | Deliverable |
|---|---|
| M0 | Project skeleton, world grid renders, camera pan/zoom |
| M1 | One ant, manual movement, digging removes touched cells (no life cycle yet) |
| M2 | EnTT-based multi-ant population, pathfinding through tunnels, pheromone grid + static food source foraging loop |
| M3 | Response-threshold task allocation (dig vs. forage vs. idle) replaces hardcoded behavior |
| M4 | Brood system: queen lays eggs, egg→larva→pupa→adult timers, nursing task |
| M5 | Death & necrophoresis: aging, starvation, corpses, undertaker task, stability solver (cave-ins) |
| M6 | Full colony-phase FSM: founding (nanitics) → growth → reproductive → nuptial flight → new colonies |
| M6.5 | Progression layer: Genetic Legacy prestige currency + 2-branch/4-tier upgrade tree, meta-progression save file (§12) |
| M7 | Sandbox polish: ImGui panel, speed control, click-inspect, day/night, spoil-heap visuals |
| M8 (stretch) | Multiple concurrent colonies competing for territory, weather (rain flooding low tunnels), species presets via `species.json` (harvester, leafcutter, fire ant), predators |

Build order matters here: M1→M3 gets you a *system* that looks alive with almost no content; M4→M6 is where the actual "colony life cycle" requirement gets satisfied; M7 is what makes it presentable.

---

## 11. Stretch Features (post-MVP)

- **Fungus/aphid farming** (leafcutter/mutualist species) — corpses and cut vegetation feed a fungus-growth grid instead of just decomposing.
- **Multiple species presets** — swap `species.json` to change lifespans, caste ratios, digging speed, pheromone chemistry weighting.
- **Weather** — rain fills low-lying tunnels with water, forcing evacuation/re-digging; ties into the moisture field already on `Cell`.
- **Polygyne colonies** — multiple queens, queen-replacement on death, no hard extinction path.
- **Predators / army-ant raids** — external threat entities that trigger the alarm-pheromone channel (§2.4) already built for the base sim.

---

## 12. Progression Layer — Prestige + Small Upgrade Tree

Layered entirely on top of `colony_manager.cpp`. This touches no AI or behavior code — it's a parameter-modifier system plus one persistent currency, not a new simulation system. Deliberately kept small: two branches, four tiers each, eight nodes total.

### 12.1 The loop

```
found colony → grow → reach Reproductive phase → (player choice) trigger nuptial flight
→ run ends, Genetic Legacy earned from this run's lifetime stats
→ spend Legacy on permanent upgrade tiers
→ new colony founds on a fresh plot, starts measurably faster
```

Triggering nuptial flight is a **player decision**, available any time once `ColonyPhase::Reproductive` is reached — not automatic. That's the entire prestige-timing decision the genre runs on: growth flattens as you approach your chamber/food soft caps, so there's a natural point where cashing in beats grinding further.

### 12.2 Genetic Legacy currency

Earned once, at nuptial flight, from **lifetime production this run** — not peak population, so a full run is rewarded over a lucky spike:

```cpp
int computeLegacyEarned(const ColonyRunStats& stats) {
    int base = (int)std::floor(std::sqrt(stats.totalWorkersEverProduced / 500.0));
    int queenBonus = stats.survivingQueensAfterFlight * 2;
    return base + queenBonus;
}
```

`sqrt` scaling is the standard genre curve: early runs feel rewarding, later ones show real diminishing returns, so there's no incentive to leave one colony running forever instead of prestiging.

### 12.3 The tree

| Tier | Vigor (brood/queen output) | Industry (worker efficiency) | Cost (Legacy) |
|---|---|---|---|
| 1 | −10% egg incubation time | +15% dig rate | 3 |
| 2 | +2 starting nanitics | +20% pheromone persistence (slower evaporation) | 7 |
| 3 | −15% larva/pupa duration | +10% carry capacity | 15 |
| 4 (keystone) | Queen keeps a reserve buffer — founding failure becomes far less likely | Colony can work two dig frontiers at once | 32 |

Cost curve: `cost(n) = 3 * 2.2^(n-1)`, independent per branch — no shared pool competition between Vigor and Industry. Resist adding a third branch until these eight nodes are built and feel good; "small" is the point.

### 12.4 Data model

Two save files, deliberately separate — meta-progression must survive a colony reset, world state must not:

```cpp
// persists forever, independent of any single colony run
struct MetaProgression {
    int geneticLegacy = 0;
    int vigorTier = 0;
    int industryTier = 0;
};

// applied once at founding, as a multiplier pass over species.json's base values
struct EffectiveSpeciesParams : SpeciesParams {
    void applyPrestige(const MetaProgression& m) {
        eggDuration        *= kVigorEggMult[m.vigorTier];
        larvaPupaDuration  *= kVigorDevMult[m.vigorTier];
        startingNanitics   += kVigorNaniticBonus[m.vigorTier];
        foundingFailChance *= kVigorFoundingSafety[m.vigorTier];
        digRate            *= kIndustryDigMult[m.industryTier];
        pheromoneEvapRate  *= kIndustryEvapMult[m.industryTier];
        carryCapacity      *= kIndustryCarryMult[m.industryTier];
        concurrentDigFrontiers = (m.industryTier >= 4) ? 2 : 1;
    }
};
```

Serialize `MetaProgression` with a single-header JSON library (nlohmann::json, pulled in via `FetchContent` alongside EnTT/raylib) to `save/meta_progression.json`. This is the first thing in the project that needs to survive an app restart — it's your natural point to add save/load infrastructure at all, so don't build a generic save system before this milestone needs one.

### 12.5 Trigger flow

```cpp
void ColonyManager::triggerNuptialFlight(Colony& c, MetaProgression& meta) {
    ColonyRunStats stats = c.computeLifetimeStats();
    int earned = computeLegacyEarned(stats);
    meta.geneticLegacy += earned;
    saveMetaProgression(meta);

    ui.showLegacyShop(meta);                    // ImGui panel: spend Legacy on the 8 nodes above
    world.reset(newSeed());
    Colony next = spawnFoundingQueen(meta);     // applyPrestige() baked into her species params
}
```

### 12.6 Roadmap placement

Slots in as **M6.5**, right after the colony-phase FSM (M6) and before sandbox polish (M7) — it needs `ColonyPhase::Reproductive` and nuptial flight to already exist, and its shop UI can reuse the ImGui work you're already doing for M7.

### 12.7 Deliberately out of scope for this layer

No clicking, no soft-population-cap walls, no multi-colony management, no food/dirt shop — those belong to the "full incremental" version if you ever want to push further in that direction. This layer's only job is to make the biologically-accurate nuptial flight event *feel* like a reward instead of just an animation.

---

## Sources consulted

- Ant life cycle & caste biology: GeeksforGeeks "Ant Life Cycle"; UGA CAES "Ant Biology"
- Division of labor / response-threshold model: Bonabeau et al. threshold model, summarized in *Web Mining using Artificial Ant Colonies* (arXiv:1404.4139) and *Dynamical models of task organization in social insect colonies* (arXiv:1511.04769)
- Pheromone trail deposition asymmetry: Czaczkes, Olivera-Rodriguez & Poissonnier, *Insectes Sociaux* (2024), DOI 10.1007/s00040-024-00995-y
- Necrophoresis / oleic acid corpse recognition: Diez et al., *J. Chem. Ecol.* (2013), DOI 10.1007/s10886-013-0365-1; Wikipedia "Necrophoresis"
- Colony founding, claustral mode, nanitics: Watanabe et al., *Psyche* (2017), DOI 10.1155/2017/4520109; Wikipedia "Nuptial flight"
- Digging mechanics / soil arching: Caltech (Andrade, Parker et al.), covered in *Physics Today* and *PNAS*; summarized in Interesting Engineering and Earth.com coverage
- Prestige/incremental design patterns (§12): Wikipedia "Incremental game"; Alcorn, *The Math of Idle Games, Part III* (Game Developer); community write-ups on Cookie Clicker/Egg Inc/Tap Titans prestige curves

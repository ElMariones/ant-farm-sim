# A nest that improves its own journeys

Implemented scope: T014i, 2026-09-08. This is fictional colony behavior for a watchable incremental
game, not a biological model. Visual feel and production balance remain unreviewed this session.

## The working loop

1. Foragers discover finite food sites and physically carry food into granaries.
2. Food supports brood and workers; occupied cradles and fuller stores create demand for rooms.
3. Workers widen a usable chamber or connect a new one to the nearest reachable part of the nest.
4. With food and spare workers, the colony cuts a useful connection between existing rooms.
5. Shorter legal journeys can reduce future carrying and nursing travel. Productive labor earns
   the existing Work currency, and adaptations improve labor, nursing, foraging and queen output.
6. The player decides between supporting brood, gathering food, expanding the nest, or balanced
   effort, then eventually sends the existing voluntary flight for Genetic Legacy.

The addition is step 4 and its connection to step 3: development can improve a mature nest's
circulation instead of only increasing its footprint. No extra currency, construction menu,
completion reward, colony micromanagement or free terrain edits are introduced.

## What ants build

**Access passages.** Planning searches backwards from a room to connected nest air. An existing
side branch can become the anchor for the next room, rather than every room tracing a spoke toward
the queen. Stone and bedrock are excluded; roots and clay raise the planning cost. A stable cost
variation across patches of earth gives seeds different bends. The route persists through digging
and saving, including its three-cell width and the shoulders at a turn. Digging the centreline
cannot silently discard the unfinished edges. Stone at an edge leaves a narrow section.

**Cross-passages.** Compare actual cardinal walking distances between completed room centres.
Consider nearby rooms whose routes involve a substantial detour. Cut a connection only if its
centreline removes at least a quarter of the old journey and includes at least four solid cells.
This produces a loop through existing passages, rather than another room or a decorative dead end.
The 25% threshold is a planning criterion, not a measured colony-wide efficiency promise.

**Separate chambers.** Leave clearance for irregular rims using physical distance, including when
widening. If the nearest room cannot widen, try another usable room before commissioning another.
Brood and grain capacity still comes only from reachable cells in designated rooms. A passage
through unassigned earth does not create free cradles or granary space.

## Player influence and interruptions

- Balanced focus starts considering optional routes at 24 workers; Dig focus starts at 12.
- Require at least 20 food units of carbohydrate and 15 of protein (20,000 / 15,000 internal units).
- Food or Brood focus postpones optional route labor. Normal job commitments expire naturally;
  switching focus does not teleport or instantly reassign every ant.
- Required brood/storage construction preempts optional connections. Already-cut ground and the
  full connection plan remain. A worker carrying spoil still delivers it before taking new work.
- Maximum four claims per active project; claims identify actual cells and require connected air
  beside the face. Changing projects clears obsolete target paths.

The activity panel reports the active purpose, marks when a passage waits for spare labor, counts
new completed passages and records their completion. Counters exclude existing founding passages:
there is no attempt to invent their construction history from the terrain.

## Bounds, failure paths and saves

Up to 26 rooms, 52 passage records and 256 cells per centreline. Optional connections are capped at
half the room count. Evaluate them every ten simulated seconds, attempting at most eight candidates;
each terrain route search expands at most 8,192 cells. A failed new-room route is rejected before
committing the room. Isolated air pockets cannot anchor work; stone-enclosed chamber cells stay
intact. Derived connectivity and work lists are cached until terrain or intent changes.

Schema 4 stores all route intent and completion flags. Schema 3 retains its terrain and rooms and
begins with no historical passage records; unfinished rooms plan an entrance on the next update.
The detailed validation and migration contract is in [PERSISTENCE](../engineering/PERSISTENCE.md).
No exact continuation claim spans the old and new algorithms.

## Handoff

Focused checks cover rock detours, branching, retained shoulders, cell reservations, real path
shortening, project preemption, no-benefit rejection, save continuation and malformed/migrated
profiles. These are code checks, not visual approval or a new economy baseline.

Follow-up: inspect several grown nests and interrupted projects; remeasure both production policies
in T013a; implement diagonal movement separately in T003b. Routes still use cardinal movement,
shortcuts use room-centre distance rather than traffic measurements, and extreme population
performance has not been profiled. Legacy saves with a completely rock-sealed unfinished room may
still need a future explicit abandoned-project state; newly sited rooms reject unreachable routes.

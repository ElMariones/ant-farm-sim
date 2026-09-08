# Ant Farm Sim — audit and implementation guide

Reviewed 2026-09-07 against `f1ddf4c` on `main`. The starting worktree was clean. This deliverable is a source/commit audit and a design handoff; the proposed gameplay and UI changes below are **not implemented by this document**. No live UI interaction or new screenshot review was performed during this audit. Findings distinguish source-confirmed defects, reproduced contracts, and design limitations.

## Current continuation — 2026-09-08

The audit below is historical; STATUS records the later T014a–h implementation.
T015a now also optimizes pathfinding scratch and terrain chunk uploads for reported 20x slowdown;
see [PERFORMANCE_REPORT](PERFORMANCE_REPORT.md) for the measured CPU improvement and remaining gates. T014i now adds
persistent branches around rock, cross-passages that shorten journeys, construction priorities,
and schema-4 saved passage intent. See [NEST_NETWORK](../design/NEST_NETWORK.md) for the working
loop and exact limits. This session intentionally omits visual QA and a full balance sweep at the
owner's request. Next: T003b movement, then T013a production pacing with the new geometry; do not
repeat T014a or treat the old audit's tunnel description as current code.

## Assessment

The project has a real deterministic colony, resource accounting, durable saves and a generation economy. Preserve those foundations. The watchable-colony and interaction gates are weaker than the milestone labels suggest: connected excavation does not yet create a convincing network of tunnels, movement collapses onto shared routes, and picking bypasses the DPI conversion already used by the camera. Release preparation should follow repairs to this experience.

The requested direction is one fictional species with subtle individual differences, purposeful digging at visible tunnel faces, varied paths through legal space, and a UI that makes colony needs and available actions understandable. “Movement in all axes” means continuous horizontal, vertical and diagonal movement in the existing **2D cross-section**, not a 3D simulation or free flight through sky.

## What the commits delivered

Commit subjects and recorded verification were checked against the current owning code. Historical test counts below are commit evidence, not newly reproduced results.

| Commits | Delivered work | Assessment / remaining evidence |
|---|---|---|
| `c948e73`, `0b6f775` | Archived brief, design and implementation plan | Good authority structure; several active introductions still described an unimplemented project |
| `7c12137` | M0/M1: build, deterministic world, camera and food loop | Real working foundation; later DPI and movement fixes show initial visual acceptance was incomplete |
| `2ad23e2` | M2: tasks, digging, brood, mortality | Physical excavation and transport exist; the frontier policy expands a central cavity rather than planning persistent passages |
| `fdb7963` | M3: Work, upgrades, snapshot codec, atomic saves/recovery | Recorded 60 tests and app restart evidence; UI explicitly temporary |
| `bdfedd0` | M4: maturity, gynes, flight transactions, traits and balance policies | Valuable transactional tests; a tested SaveService generation loop does not establish a usable manual UI loop |
| `0d60475`, `5b0ca9e` | Oriented ant art, zoom detail, terrain baking and revision test | Bodies/cargo improved; workers retain identical body scale/color; cache uses whole-terrain rebuilds |
| `f564fb0` | Physical spoil apron, once-per-tick frontier claims | Fixes the quadratic claim scan; does not complete T015's stress, sanitizer and packaging prerequisites |
| `d420c57` | Hover, capture and tooltip primitives | Unit-tested widgets exist; world and modal routing do not yet obey their ownership |
| `670b9f6` | Executable-relative config resolution | Useful T016 prerequisite; not a packaged `.app` |
| `f1ddf4c` | Retina camera, supported surface walking, vertical movement repair, inspector | Fixes substantial defects; selection still calls the raw camera transform; new balance tables were not propagated consistently |

All twelve inspected commits use the required ElMariones author identity. No remote CI status was fetched, no history rewritten, and no publication is part of this audit.

## Defect register

Priorities: **P1** blocks reliable interaction or a normal progression flow; **P2** should be addressed before release; **P3** documentation or polish. Paths and symbols identify the reviewed source even after line numbers move.

| ID | Priority / evidence | Finding and consequence | Owning repair / verification |
|---|---|---|---|
| A01 | P1, source confirmed | `Renderer::update_selection` passes logical mouse coordinates directly to `GetScreenToWorld2D`, although `CameraController::apply_scale` scales camera offset/zoom for Retina. Picking and drawing disagree at 2x DPI | Centralize logical screen ↔ world conversion. Pick the same interpolated, visually offset center that `draw_ant` uses. Round-trip at 1x/2x DPI, pan and several zooms; click a moving cargo ant |
| A02 | P1, source confirmed | Camera pans while left is held; selection executes on both press and release. Slight hand movement pans and can select a different ant on release | Left click exclusively selects; middle/right drag pan. Use one release gesture with a small logical-pixel movement threshold and ownership captured on press |
| A03 | P1, source confirmed | `draw()` selects and draws interactive underlying cards before drawing modal overlays; `update_input()` enables camera and gameplay shortcuts regardless of an open flight panel. `UiState` ownership is not consulted by picking | Resolve scene and input owner before dispatch. A modal consumes pointer and keyboard across its whole backdrop, including disabled/empty areas; test dragging across scene boundaries |
| A04 | P1, source confirmed | Enter in the flight panel requests immediate permanent flight. No separate confirmation state or saved previous pause state exists; opening the panel does not pause simulation | Preview → explicit confirmation → commit → shop. Revalidate eligibility at confirm. Cancel restores prior pause state; write failure retains colony and preview |
| A05 | P1, source confirmed | Loading `BetweenRuns` sets a flag but only `--open-legacy` opens its panel. The visible colony is a paused backdrop and the shop requires an undisclosed L shortcut | Derive initial scene from persisted phase. Always resume BetweenRuns into the shop, with a pointer-accessible “Found colony” action |
| A06 | P2, source confirmed | No `SetExitKey` override exists; the loop uses `WindowShouldClose()` while UI promises Escape closes the panel. raylib's bundled default exit key is Escape | Disable default Escape exit, route Escape to panel/menu, preserve window-close save. Verify against bundled raylib and native interaction |
| A07 | P2, source confirmed | `S` both pans down and requests save; footer hardcodes `Generation 1` | Use a distinct save chord/button and actual `view.legacy.generation`; test second-generation UI |
| A08 | P2, source confirmed | Ant inspector begins at y=630 regardless of height; the footer begins at y=580 at 1024×640. Selected details cannot fit. Status failures are hidden below width 1100 | Layout from available bounds; scroll/collapse secondary content; persistent visible save-error region at minimum size |
| A09 | P2, reproduced contract | `Grid::walkable` distinguishes Air from unsupported Sky, but `Grid::set` changes navigation revision only when `is_passable` changes. Sky→Air can change walkability with no revision change | Audit material and neighbor-dependent support changes; invalidate navigation when effective walkability changes. Production trigger for an isolated Sky→Air edit is not established; regression repro below |
| A10 | P2, source confirmed | `draw_legacy_panel` draws text only; V/Y/C/Enter are keyboard actions, despite README claiming equivalent pointer controls | Real accessible buttons with reasons; keyboard and pointer must call the same command path |
| A11 | P2, source confirmed | `selected_id_` is not cleared on removal or new run. Stable IDs are run-local, so a new colony can silently inherit a prior selection | Clear selection on run identity change and actor removal; same-run snapshot ordering must not change it |
| A12 | P2, design/accounting gap | `process_cargo_return` clears spoil and increments delivered even when `deposit_spoil()` returns false because the apron is full/occupied | Choose explicit overflow accounting or retained cargo + retry. Do not retain indefinitely and trap every excavator. Test saturated apron, occupancy and death with cargo |
| A13 | P3, source confirmed | ART_AND_UX says dot/silhouette/detail thresholds 2.6/5.5; code uses 1.8/3.0. “Growing steadily” uses a small condition chain, not measured growth; worker IDs appear in public inspector | Reconcile actual LOD values; derive growth copy from evidence; put IDs in diagnostics |

A06's bundled dependency behavior can be inspected in `build/dev/_deps/raylib-src/src/rcore.c` (search `exitKey = KEY_ESCAPE`); native behavior still needs an end-to-end check. Unit tests of buttons cannot cover that app-loop interaction.

A09 minimal reproduction, executed against current `src/sim/grid.cpp`: create an all-Sky Grid, inspect `{10,10}`, set it to Air, inspect again. Observed `walkable_before=0 walkable_after=1 revision_before=1 revision_after=1`. This proves the revision contract gap without claiming it currently breaks an ordinary colony.

### Design limitations behind the requested behavior

- `navigation.cpp` has four neighbors; home paths take the first descending neighbor in a fixed order and A* breaks ties by cell index. Ants sharing endpoints therefore share routes. The renderer's tiny static ID offsets only separate drawings; they do not diversify paths.
- `World::move_one_tick` spends travel along X first, then Y. The recent overshoot fix is correct and must survive replacement, but it preserves angular motion.
- `recompute_needs_and_frontiers` ranks nearby cells by distance to home, doubles vertical distance and adds only 0–6 seeded noise, then keeps eight frontiers. Every worker claims the first available one. This favors broad excavation near the nursery, with little persistent branch direction.
- Digging is restricted to Manhattan distance <64 from home at y=58. Therefore the clay beginning at y=138 is outside the excavation envelope. Hardness rules exist, but ordinary digging cannot reach that layer.
- Every connected Air cell increases nursery/storage capacity. New tunnels therefore change economics even if no new chamber is created. Faster digging also raises food demand targets through expanded storage; this coupling was already observed in the trait report.
- `draw_ant` uses fixed worker scale 1.0 and one worker chitin color. Different headings and ID offsets do not meet the requested body variation.

## Target experience and implementation design

### 1. Reliable observation and input — T014a

Implement A01–A03, A06–A08 and A11 before visual embellishment. Extract pure picking/gesture logic that can be tested without raylib; keep the DPI camera adapter in presentation. Feed the picker a rendered-pose list used by drawing so interpolation and offsets cannot drift. Use a minimum 8-logical-pixel hit tolerance and account for caste/appearance bounds. Break equal-distance ties by stable ID. Empty world click clears; a panel click preserves selection. A drag beginning in UI cannot become a world gesture on release.

Layout/input/render must use the same frame's scene bounds. Suggested order: collect native events → compute layout and scene ownership → dispatch one semantic action → apply game commands → simulate → render. Existing cards register interactions during drawing; migrate deliberately instead of adding another overlapping hit-test pass. Modal priority must apply to both pointer and shortcuts.

Tests: press on card/release in world; reverse drag; disabled control; modal backdrop; short click; click after camera pan/zoom; death while selected; new run reusing actor ID; 1024×640 selected details; 1x/2x DPI. Perform native clicks at both window sizes and save annotated captures. This slice is complete only when an ant can actually be selected in the running app.

### 2. Purposeful tunnels and chambers — T006a/T006b

Keep soil cells and actual shared dig work authoritative. Add a small bounded list of active dig faces, each with an anchor in connected Air, a persistent preferred heading, corridor width and branch age. Start with at most four active faces and two workers per face; tune only with measurements. These are plain sim data, not a construction framework or player-painted blueprint system.

For each face, rank **reachable adjacent diggable cells** using forward continuation, turn cost, material effort, proximity to existing voids and deterministic low-frequency seeded variation. Initially target corridors roughly 3–5 cells wide; use cardinal stair steps to carve slanted connected passages. Direction persists across several cells so noise produces bends, not isolated pits. Penalize repeatedly widening completed corridor sides and merging parallel branches into a single cavity. Where a root blocks progress, turn to a reachable alternative; release an unreachable claim after a bounded retry interval.

Branch only after a minimum completed length (initial tuning range 12–24 cells), with space demand and separation from another face. End/retire branches that reach the nest envelope or cannot progress. Chamber widening is a separate local behavior at suitable branch ends, triggered by nursery/storage pressure, with irregular rounded boundaries. Never clear planned cells automatically: workers approach, dig, carry the final grain and return it to the surface.

T006a preserves current capacity rules to isolate geometry. T006b classifies useful chamber space separately from transit corridors; only designated connected chambers expand capacity. Existing saves retain their current capacities during migration, using a legacy-space allowance that is not granted again on reload; define the precise accounting before coding. Resolve spoil overflow explicitly in T006a: recommended bounded physical apron plus counted off-view spoil overflow, preserving transport work and mass accounting without an infinitely tall mound.

Acceptance: seeds 1/7/42 visibly form elongated connected passages and branch chambers at 5/15/30 minutes; at least one visible branch separates two chambers in a suitable fixture. Capture time sequences, not only final textures. Assert no isolated dug cell, no forbidden material removed, one grain per completion under contention, capped claims, death/task-switch release and finite blocked-face recovery. Record corridor widths, connected area and productive dig/haul time; visual approval is required even with green tests.

### 3. Varied legal paths and continuous movement — T003a/T003b

First diversify **route choice**, then change locomotion. Preserve the common home-distance field. Among equally good descending choices, rank by a stable hash of run seed, actor ID and candidate cell; distinct ants may take different shortest routes without random detours. For outward A*, apply per-ant deterministic tie preference initially. Do not add expensive per-ant full distance fields. A single-cell bottleneck still legitimately has one route.

Then add 8-direction navigation with integer costs (e.g. cardinal 1000, diagonal 1414) and a matching admissible octile heuristic. Home routing must use the same weighted topology (a bounded Dijkstra field), not the old four-neighbor BFS distances. A diagonal requires both orthogonal side cells and destination to be legal; prohibit corner cutting and unsupported sky shortcuts. Physical excavation connectivity remains cardinal so diagonal air islands never count as chambers.

Move toward subcell waypoints with a normalized fixed-point 2D displacement and remainder accounting, not X-first stepping. Limit the final step to remaining distance, carry fractional travel deterministically, and test negative directions. Add small persistent lane offsets inside wider passages (initial range ±0.15–0.25 cells), smoothly tapering to zero at tight corners, narrow corridors and interactions. Validate each segment against terrain; reject a decorative offset if it crosses soil. Do not feed frame time or renderer RNG into authoritative positions.

Use soft, bounded local crowd preference only if varied ties/lanes still look too synchronized; do not introduce hard ant-ant collision that can deadlock the nursery. Random movement must not farm Work: productive accounting continues to reflect an eligible job and meaningful progress, not aimless oscillation. Do not add arbitrary idle wandering as a substitute for route variation.

Acceptance: identical seeds/actions/ticks reproduce state and survive mid-route reload; diagonals have correct speed; all octants converge without overshoot; no corner clipping; changed terrain invalidates routes; a wide symmetric route fixture produces multiple legal routes across 32 worker IDs; a narrow passage still succeeds. Watch carrying, digging approaches and two-way traffic at 1x/5x/20x. Re-measure path budget and delivery times.

### 4. Subtle individuality — T014b

Derive a presentation-only appearance key from run seed and stable actor ID, using a separate fixed hash. Worker body length: 0.94–1.06 of base; width: 0.96–1.04. Chitin lightness: approximately ±5%, with at most a very small warm-brown hue shift. These are proposed visual tuning ranges, not measured biological traits. Keep the same silhouette, species palette and leg count. Queen/winged scale hierarchy remains explicit.

Use the same key for a small gait phase difference. Gait should follow traveled distance; idle ants should not march in place. Apply appearance to body, legs, cargo anchors and picking bounds consistently. Never change speed, carrying capacity, costs or maturation as a consequence of cosmetic size. All zoom tiers retain contrast and cargo readability. Reload or resize must not reroll appearance; appearance code must not advance sim RNG or change canonical hashes. Record close-ups and a crowded overview before/after.

### 5. UI and generation flow — T014c/T014d

Keep the warm diorama direction. A thin top strip shows workers, brood, food and Work; reproductive counts appear when relevant. The right panel contains the current bottleneck, focus, four adaptation cards and selected entity details. At minimum width, use an explicitly collapsible overlay and a scrollable panel body; fixed footer/actions remain reachable. Use 16px body text, ≥36px controls and 100/125/150% UI scaling.

Each adaptation explains current → next effect, one-level cost and unavailable reason. Show growth/food trends only after enough simulated observations; use “Measuring…” while collecting them. Avoid “Growing steadily” when starvation or decline contradicts it. Present one actionable hint at a time: inspect a carrier, save for a first adaptation, understand a shortage, prepare flight. Persist acknowledged hints without granting rewards.

Flow: Running ↔ Pause menu; Running → paused Flight preview → Confirm flight → saving → BetweenRuns shop → saving → new Running colony. Confirmation names the exact payout, run items lost and permanent items retained. Cancel restores the previous pause state. A failed write leaves the old live phase intact with Retry; do not animate success first. Shop has owned/affordable/locked tiers, next effect and remaining wallet, plus “Found colony” without mandatory spending. Load dispatches directly to the persisted phase. Provide a separate confirmed no-reward restart for Extinct; do not reuse corrupted-save recovery for ordinary defeat.

Pointer and keyboard use the same actions. Tab/Shift-Tab traverse visible enabled controls with a visible focus ring; Enter/Space activate only the focused control; Escape closes the top layer or opens Pause. Reduced motion disables decorative easing/pulses. No native screen-reader support claim without an implementation.

Review starter, 100-worker colony, cargo selection, shortage, locked upgrade, flight confirmation/cancel, save failure, shop after restart, corrupt recovery, Extinct restart and minimum size. The final flow must be completed with actual clicks and again with keyboard-only navigation.

## Ordered milestones and dependencies

Keep M0–M4 as delivered historical systems. Reopen the experience acceptance that failed; do not renumber completed work or invent an M6 release.

| Order / gate | Tasks | Dependency | Exit evidence |
|---|---|---|---|
| M5-A: trustworthy interaction | T014a | Current M4 | Native selection, modal capture, Escape and minimum-size inspector work |
| M5-B: watchable colony | T006a → T003a → T003b → T014b | M5-A; T003b follows route diversity | Connected tunnel sequence, varied legal traffic, subtle stable worker appearance; persistence regressions pass |
| M5-C: meaningful growth | T006b → T013a | New digging/movement | Useful-space accounting, affordable tier-I comparison and both policies re-baselined |
| M5-D: usable complete loop | T014c → T014d | Input foundation; final copy follows T013a | Pointer and keyboard generation/recovery/decline flows; onboarding/scaling/motion review |
| M5-E: release evidence | Remaining T015 → T016 | All prior experience gates | Population benchmarks, sanitizer checks, two-hour soak, packaged external launch/save/reopen |

Each ID is a bounded implementation slice and must get its own evidence in STATUS. If a slice is still too large, suffix it again. Do not close its parent merely because one child builds.

## Save and determinism boundaries

Cosmetic keys can be recomputed; active dig-face commitments, lane targets, movement remainder, route preference epochs and chamber accounting influence future state and must either be fully serialized or derived without lost history. Extend snapshot limits, validation, canonical hash and migration together. New navigation costs may invalidate old stored routes; define an explicit simulation-version migration that marks them stale before use. Do not promise identical continuation across two different simulation algorithms. Require uninterrupted/restored equality within the new version and safe, deterministic migration of old profiles.

Use independent seeded randomness for any new simulation decisions so adding art never changes brood/job outcomes. Retain stable IDs and existing transactional prestige receipts. Test migration with in-flight food/spoil, a partially dug face, a stale route and BetweenRuns. Never rewrite an unreadable profile automatically.

## Balance and performance gates

T013a repeats seeds 1/7/42/101/2026 under no-purchase and buy-cheapest at production settings. Record first delivery, first birth, first purchase, maturity, flight, survival, worker peak/deaths, starvation, excavated area, useful capacity and route distance. Preserve the 2–4 minute first purchase and 30–45 minute first flight targets unless a measured, recorded decision changes them.

The first flight pays 4 Legacy. Compare the actually affordable Vigor I or Industry I against base under the same policy, including a local metric directly affected by the trait. A full branch costs 3+7+15+32 = **57 Legacy**; IV-vs-base evidence is not first-reward proof. Add a wait policy for larger payouts and a no-flight two-hour colony. Label synthetic fixtures and distinguish deterministic per-seed variation from measurement noise.

T015 is partial. Measure 100/1,000/5,000 workers, 1x/5x/20x, integrated frame/tick percentiles, path queue/expansion counts, terrain uploads, memory and save/load latency. Whole-map terrain baking is the current implementation, not chunk-level texture updates; optimize only after measuring it. Record machine, compiler, revision, population fixture, seed, duration and percentile method. Run ASan/UBSan and the required two-hour soak. A sixty-minute Debug test and a fast Release headless run cannot substitute for these gates.

T016 follows those gates: local `.app`, versioned archive and attribution; launch outside the repo with arbitrary cwd, save and reopen. No public release/tag until requested. License remains an owner decision at publication time.

## Documentation corrections and validation

This audit links the guide from STATUS/BACKLOG/README, corrects the obsolete planning-only introductions, removes the stale open quadratic-scan claim and marks T015 partial. ART_AND_UX and SIMULATION link the new accepted direction while labeling it as future work. BALANCE_REPORT arithmetic is reconciled to its existing per-seed rows, without claiming new balance measurements.

**Fresh verification:** `cmake --build --preset headless -j 4` succeeded; `ctest --preset headless --output-on-failure -j 4` passed **113/113** in **118.60 s**, including the sixty-minute simulation test. The isolated A09 executable reproduced the navigation-revision contract gap. Native UI review, new production balance runs, sanitizer/stress benchmarks and runtime fixes are not part of this documentation deliverable. The next coding task is T014a.

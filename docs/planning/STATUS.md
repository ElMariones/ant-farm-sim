# Project status

Updated: 2026-09-07.

## Current state

**M0 through M4 are implemented.** The native raylib app and graphics-free headless runner share a deterministic C++20 simulation. Workers choose eligible jobs from normalized stimuli, deposit into a bounded trail field, excavate connected space, carry spoil, nurse brood, recover dropped food, clean corpses, and age or starve. The colony earns Work from productive labour and spends it on four ten-level run adaptations. Saving is durable and atomic, and reopening the game continues the same colony at the exact saved tick.

M4 completes the generation loop. A colony latches **maturity** at 100 living workers, 150 worker births and 12 simulated minutes; after that one egg in five becomes a **winged queen**, capped at ten live and developing combined. With three winged queens, a living founding queen and an unassisted run, the player can send a **nuptial flight**, which pays Genetic Legacy through a single committed profile revision carrying an immutable receipt. Between colonies, Legacy buys eight permanent trait tiers across Vigor and Industry, and founding the next colony applies the owned traits exactly once.

## Active scope

**In progress: T014a — reliable selection and modal input.** The code is written and covered by tests; the native interaction check that closes the slice is blocked. Shared rendered/picked poses, DPI-safe camera conversion, owned click/drag gestures, modal isolation, Escape/save shortcuts and a responsive inspector are implemented. Simulation and save formats are unchanged. See [NEXT_STEPS](NEXT_STEPS.md) and the T014a section below.

Suggested prompt for a smaller model:

> Read AGENTS.md, docs/planning/STATUS.md and docs/planning/NEXT_STEPS.md, then implement T014a from docs/planning/BACKLOG.md. Preserve deterministic command validation and exact integer accounting. Inspect existing changes first, run the documented headless and dev tests, update affected contracts and STATUS, and commit with the required identity and a descriptive body.

## Milestone state

| Gate | State |
|---|---|
| Planning | Complete |
| M0 Foundation | Complete — T001 |
| M1 Watchable slice | Complete — T002–T004 |
| M2 Living colony | Complete — T005–T007 |
| M3 Safe incremental slice | Complete — T008–T010 |
| M4 Complete generation loop | Complete — T011–T013 |
| M5 Release candidate | In progress — T014 partial; T015 optimization partial, full performance gate open; T016 partial |

## Environment observations

Implemented and verified on Apple Silicon macOS with Apple Clang 21 and CMake 4.4.3 using Unix Makefiles. First configure downloads pinned source dependencies; warmed headless configure/build has no graphics dependency, and the headless binary links only libc++ and libSystem. `clang-format` is not installed on this machine and CI does not gate on formatting, so no reformatting pass was run.

## Earlier implementation evidence

The entries below record earlier sessions, not a fresh audit run. Later entries supersede earlier measurements. The current audit and its validation are recorded in NEXT_STEPS.

- `ctest --preset headless`: pass, **113/113 tests including the sixty-minute soak**, in 270 s. The soak alone is 101 s in Debug, against CTest's 1,500 s limit; it previously timed out on the Linux runner.
- `cmake --preset release` and `cmake --build --preset release`: pass.
- `ant_headless --verify-round-trip` passes at 20,000 / 60,000 / 100,000 / 120,000 / 140,000 / 160,000 / 200,000 ticks on seed 42, and at 40,000 and 120,000 ticks on seeds 1, 7, 101 and 2026.

### Simulation speeds

Speeds are now **1x / 5x / 20x** (previously 1x / 2x / 5x), on the footer buttons and keys `1`, `2`, `3`. The per-frame tick budget scales with the selected speed so 20x is real at a low frame rate rather than silently clamped; speed still changes the number of whole ticks, never the size of one. `preferred_speed` validation and the saved settings accept the new set.

### T011 — Maturity and winged queens

- Every maturity boundary is covered: age alone, population and births without the age, one birth short, one worker short, and all three together. A population collapse after latching does not revoke maturity.
- No winged queens are allocated before maturity; afterwards the egg assignment counter tracks exactly the eggs laid since maturity and one in five becomes a gyne.
- The combined cap of ten counts developing brood as well as adults, both while ten are in the nursery and after they emerge.
- Gynes take exactly twice the trait-adjusted duration at every stage, never count toward worker births, and never take a job.
- The assignment sequence, the maturity latch and the winged population survive a save and continue from where they left off.
- Trait modifiers are verified individually: Vigor II founds eight workers instead of six, Vigor I shortens the egg stage to 540 ticks, Vigor III shortens larva and pupa to 1,020 and 765, and a gyne still doubles the trait-adjusted figure.

### T012 — Transactional prestige and permanent traits

- `integer_sqrt` is exact across the full 64-bit domain, checked at every perfect square and boundary up to 3,000 and at values where a double has already lost precision.
- Documented payouts reproduce exactly: 4 at eligibility, 6 at 600 births with 6 queens, 8 at 1,350 births with 10 queens, with the step boundaries at 150/600/1,350/2,400 births and 3/6/9 queens, and more than ten live queens never counting for more than ten.
- Each blocking condition is named correctly: immature, too few winged queens, dead queen, assisted run.
- A flight credits Legacy exactly once. A replay against the committed profile, a revision the caller has not seen, a command naming a different run, and a profile that already flew are all refused.
- A failed commit leaves the live colony and the profile untouched: the candidate is never adopted unless the write succeeded.
- Interrupted before replacement, the run is still eligible and still pays 4. After replacement, the reward is credited once, the receipt names the run, and loading the between-runs profile runs no payout logic.
- A repeated tier purchase cannot overspend: the second click is stale, and a fresh view has nothing left to spend. The cost table is exactly 3/7/15/32, one branch costs 57, and tiers are refused beyond four and while a run is active.
- Industry IV lowers the Work threshold from 1,200 to 960 ticks exactly once.
- The full flight → shop → new colony flow was exercised through a real `SaveService`, committing one step at a time, and the founded colony carries the bought trait with zero Work and zero adaptations.

### T013 — Seeded balance and second-run proof

Full numbers in [BALANCE_REPORT](BALANCE_REPORT.md). Summary:

- **All ten policy runs survive.** No-purchase colonies reach flight in 40.1–40.3 minutes, buy-cheapest in 30.3–30.9 minutes; both inside ECONOMY's 30–45 minute target. First investment lands at 2.7–2.9 minutes, inside the 2–4 minute target.
- Investing removes about ten minutes from the run, roughly quarters the death count, and adds about 30 workers at peak.
- **Vigor I reduces time to first birth on all five seeds** (−5 to −21 s), which is the required second-run proof.
- **Industry I does not reduce time to an excavation target**; four of five seeds get slower. The mechanism is measured and recorded rather than hidden: faster digging grows nest air, which raises store capacity, which pulls workers onto foraging. Recorded as an open balance question.
- Three constant-tuning alternatives were measured and rejected because each caused more extinctions than it fixed. No ECONOMY constant was changed in T013.

### Defects found and fixed during M4

Four real bugs surfaced by pushing further than M3 did:

1. **Foragers ignored colony need.** Source choice followed trail strength alone, so seed 1 with no purchases went extinct with its carbohydrate source untouched at 100,000, its carbohydrate store at 0 and protein pinned at capacity. Source choice now ranks by shortfall against the foraging target, counting food in transit.
2. **Workers over-committed to a nearly full store**, then held undeliverable cargo indefinitely. Reservations now subtract food already carried.
3. **Clearing a route left its cursor past the end**, producing a movement state that snapshot validation rejected — the app's exit save failed outright at 20,000 ticks. A path and its cursor are now cleared together.
4. **Save/load lost path and frontier staleness**, so a resumed colony skipped a replan it still owed itself and diverged from an uninterrupted run after roughly 20,000 ticks. Staleness is now stored as a fact and restored with revision 0, which the grid never issues.

The profile schema is **version 2**, with a tested in-memory v1 migration that fills castes, traits, the maturity latch and the flight receipt, and keeps the migrated colony's canonical hash.

### Visual pass on the real window (M5)

Reviewed by capturing the actual window with `screencapture` rather than raylib's `TakeScreenshot`,
which on this display writes a doubled canvas and cannot be trusted for layout. Five defects were
found and fixed, four of which the in-game screenshot had been hiding or that no test could catch:

1. **The world rendered at half scale on Retina.** `BeginMode2D` replaces raylib's high-DPI
   transform, so the interface drew at 2x while the world drew at 1x, leaving a wide band of clear
   colour between the terrain and the inspector. The display's backing scale is now folded into the
   camera, and pointer, pan and zoom conversions go through it. This had been wrong for the whole
   project.
2. **The camera could zoom out past the world.** There is now a minimum zoom derived from the
   viewport, so the diorama never floats on the background.
3. **The inspector panel was drawn at alpha 248**, so the world bled through and drew a seam along
   its own right edge. It is opaque now.
4. **Ants walked through open sky.** Sky is passable, so foragers flew over the ground instead of
   walking on it. Navigation now uses `Grid::walkable`, and food sources sit on the ground rather
   than a cell above it.
5. **Ants ping-ponged on the spot.** A worker whose next path cell was directly above or below
   spent its whole tick overshooting the target column and stepping back, forever. About a fifth of
   workers were stuck this way, and they counted as productive while doing it.

Also: grass grows on the real surface height so it sits on the spoil mound rather than inside it;
the selection inspector is back, filling the panel's empty lower half; Work, winged queens and
Legacy appear in the header once they mean something; and the ant detail threshold was retuned
because the DPI fix doubled apparent size, so ants now show legs and antennae at the default zoom.

### M5 in progress — T015 performance and the spoil mound

- **Excavated grains are now real terrain.** A worker that finishes a cell carries the grain out and tips it onto a surface mound that grows as a cone around the entrance, capped at five cells so it stays an apron. The entrance column and its shoulders are never used, so the nest cannot bury itself, and a grain is never placed on an occupied cell. This replaced a counter that only drove a drawn-on ellipse.
- This is the first thing in the simulation that makes a cell *impassable*, which invalidates cached paths. Snapshot validation now only requires paths still marked current to be walkable; a stale path crossing ground that closed under it is expected and is replanned before it is walked.
- **The sixty-minute soak was timing out in CI on Linux**, which is what turned M4's build red. Each excavator rescanned every worker for every frontier to count existing claims, so the tick cost was quadratic in population. Claims are now tallied once per tick and released when a worker takes a new target, finishes a cell, or switches task. Missing any of those releases leaves phantom claims that measurably slow the colony, which is how the first attempt was caught.
- Debug soak: **50 s, down from a 1,500 s CTest timeout**. Release soak 1.9 s. The full suite including the soak now runs in 183 s.
- Balance was re-measured after both changes and held: flight at 30.1–30.8 min with purchases and 40.2–40.4 min without, first investment at 2.7–2.9 min, no extinctions. First delivery moved from 52.6–75.0 s to 64.8–117.4 s, because foragers now climb the spoil their own colony piled up. Vigor I improves first birth on four of five seeds rather than five; recorded in the balance report.

### Packaging groundwork (T016, partial)

Bundled data is now resolved from the executable's own directory first, then the working
directory, then the source tree as a development fallback. The previous order tried a
working-directory-relative path and then an absolute path baked in at compile time, which happens
to work on this machine and would fail on a player's, because a packaged app is launched from
Finder with an arbitrary working directory and no source tree. The resolution order is covered by
unit tests using a fake filesystem predicate, and a missing file now reports the location a
shipped build would have used rather than a source path the player does not have.

The `.app` bundle itself, version metadata and the release archive are still open.

### T014a — reliable selection and modal input (implemented, native check outstanding)

New `presentation/interaction.{hpp,cpp}` holds the rules with no raylib dependency, so they run in
the headless build; `presentation/desktop_input.{hpp,cpp}` holds the GLFW-side event latching.

- **A01 — picking and drawing now share one transform and one pose list.** `ViewTransform` is the
  camera's own Retina transform with a logical-pixel interface, and `CameraController::screen_to_world`
  routes through it instead of calling `GetScreenToWorld2D` with unscaled coordinates. `actor_pose`
  produces the interpolated centre and per-id offset once per frame; the renderer draws that list by
  index and the picker searches the same list, so a hit box cannot drift from what is on screen.
  Hit tolerance is at least 8 logical pixels, scaled for queen and winged-queen bodies, and equal
  distances break by lowest stable id.
- **A02 — left click selects, right/middle drag pans.** Selection is one owned release gesture:
  ownership is captured on press, a drag beyond four logical pixels cancels it permanently until the
  next press, and a press that began on the interface cannot become a world click on release. Pan
  capture follows the same rule and, once captured, keeps running while the button is held even if
  the pointer leaves the viewport.
- **A03 — modal scenes own pointer and keyboard.** `SceneState` resolves the owning scene before
  dispatch. While a modal is open, `UiState::set_input_region` makes every underlying control inert
  across the whole backdrop, gameplay shortcuts are not read, the simulation is held, and any capture
  held across the boundary is cancelled. The scrolling panel body additionally clips input to its own
  rectangle, so a control scrolled out of view cannot be clicked.
- **A06 — Escape belongs to the game.** `SetExitKey(KEY_NULL)` disables raylib's bundled default
  (`exitKey = KEY_ESCAPE` in `rcore.c`), and Escape now closes the top layer or opens a pause menu.
  Window-close saving is untouched.
- **A07 — save has its own chord and the generation is real.** Plain `S` only pans; saving is
  Ctrl/Cmd+S or the footer button. The footer prints `view.legacy.generation` rather than a
  hardcoded `Generation 1`.
- **A08 — the panel is laid out from available bounds.** `interface_layout` derives header, world,
  panel body, selected-ant region and footer from the window size, and the camera viewport is
  derived from the same function so layout, input and rendering cannot disagree. The panel body
  scrolls, and `InterfaceLayout::panel_scroll_range` owns exactly how far, so selected-ant details
  stay above the footer at the 1024x640 minimum.
- **A11 — selection has a lifecycle.** `AntSelection::synchronize` clears the selection when the run
  id changes or the actor is gone, and snapshot reordering within a run does not disturb it.

`ant_farm` also now reports plainly when no window could be created instead of letting the first
component that needs one fail with an unrelated message.

**Verified:** `cmake --build --preset headless` and `--preset dev` and `--preset release` all build
clean. `ctest --preset headless --output-on-failure -j 4` passes **122/122 in 134.42 s**, including
the sixty-minute soak; that is 113 previously plus 9 new interaction cases (84 assertions) covering
the 1x/2x DPI round trip, interpolated pick centres with caste bounds and stable ties, the owned
release gesture in both drag directions, selection across death and run change, minimum-size layout,
the scroll range, Escape/recovery scene ownership, modal and clipped input rejection, and a short
native click that presses and releases in one poll.

**Not verified — the native interaction check is blocked.** The window was opened at 1440x900 and
photographed with `screencapture` (raylib's `TakeScreenshot` is not trustworthy on this display):
the new footer, real generation number, status line, upgrade tooltip and the selected-ant region
above the footer all render correctly. Clicking could not be completed. Automation could not deliver
an activating click to the raylib window in the background, and the machine's screen then locked, at
which point GLFW refuses to create a window at all (`Failed to find selected monitor`, `dpi=0x0`) and
macOS blocks synthetic input. **No ant has been selected in the running app, no pan or modal capture
has been exercised natively, and the 1024x640 window was not re-photographed.** T014a is not closable
until that is done with real clicks at both sizes. The interaction rules are unit-tested; the seam
between GLFW event latching and those rules is exactly what is still unproven.

### Interface interaction layer (T014, partial)

`UiState` owns pointer state and per-widget hover/press animation, and deliberately has no raylib
dependency so its rules are covered in the headless build. Controls are now registered where they
are drawn rather than in a separate hit-test pass, which removes the class of bug that produced the
twenty-pixel card mismatch in M3 — a hit box can no longer drift from its control because they are
the same expression.

- Hover and press are eased 0..1 weights, not booleans, so buttons lift, tint and inset smoothly.
  The easing is an exponential approach, which is **verified to give the same result at 20 Hz and
  120 Hz** so controls do not animate differently on a slower machine.
- A click completes only where it began: dragging off a control cancels it, and releasing over a
  control that was not pressed does not activate it.
- Disabled controls never hover, hold or click, but still block the world underneath, so a dead
  control is not a hole in the panel.
- Adaptation cards and the focus buttons now carry hover tooltips that say what an adaptation does,
  how much Work is still needed, or that focus unlocks at twelve workers.

**The appearance of all of this is unverified.** The machine's screen locked partway through this
work (`CGSSessionScreenIsLocked`), so GLFW refuses to create a window and no screenshot could be
taken. The interaction rules are covered by 9 unit cases and 76 assertions; the lift, tint, shadow,
tooltip placement and overall feel have not been looked at once. That review still needs to happen.

### Presentation upgrade (visual slice of T014)

Ant art and the overall diorama were reworked; the rest of T014 (final layout, onboarding, accessibility) is untouched.

- **Ants are drawn along a real heading** taken from their travel, so they face where they are going instead of always pointing left. A stopped ant keeps its last heading, and one that has never moved gets a stable heading from its id.
- Three zoom tiers as ART_AND_UX specifies: a dot with a cargo accent below zoom 2.6, a segmented silhouette from 2.6, and legs with an alternating tripod gait, elbowed antennae, mandibles and eyes from 5.5. Limbs are drawn a shade darker than the body so they read as legs.
- **Winged queens now look like winged queens** — two translucent swept wings, sized between worker and queen. They were previously indistinguishable from workers, which was a gap left by M4.
- Cargo is carried at the mandibles rather than floating above the head, and spoil and corpse loads are drawn, not just food.
- Chitin is warm brown rather than UI ink, so ants read against the near-black tunnel.
- **Terrain is baked into a texture** at three texels per cell, rebuilt only when a cell is dug. This replaced about 80,000 rectangle draws per frame and paid for sparse flecks, a depth gradient, and lit and shadowed edges that make a tunnel look carved. Grass became clumps of leaning blades; the nursery became a soft hollow instead of a drawn-on ring.
- **The camera now opens on the colony** instead of the geometric centre of the map, which was bare subsoil.

Two defects were found and fixed during this work: every ant body segment was invisible because `DrawTriangleFan` needs counter-clockwise winding and screen Y points down, so a positive parametric sweep culled every segment; and the heading cache could grow for the life of a session, so it is now bounded.

### Visual review

Reviewed at 1440x900 (zoom 4) and at the 1024x640 minimum (zoom 3.2) on Retina. The flight panel shows the readiness checklist with per-condition progress, the exact payout with its birth and queen components, and the next threshold computed from the rules. The between-colonies panel shows both trait branches with tier, cost and effect summary, and the found-next-colony action. A staged between-runs profile was opened in the real app and its exit save preserved `phase: BetweenRuns` with the receipt intact and no invented run. One layout defect was found and fixed: the focus buttons overlapped the excavation line, and the bottleneck line ran into the footer at the minimum height.

## Open work

Finish T014a's native check first: open the app at 1440x900 and 1024x640 with the screen unlocked
and confirm with real clicks that an ant can be selected while it moves, that a drag beginning on a
card does not select, that right/middle drag pans while left click does not, that Escape opens and
closes the pause menu, and that a click on the panel does not reach the world. Then continue with
the ordered M5 slices. The audit in NEXT_STEPS supersedes the old next-task order. Historical visual
reviews below do not establish that current input routing works.

- **The surface view was not re-photographed after the final art pass.** The window server stopped accepting new windows partway through review (`GLFW: Failed to determine Monitor to center Window`), so the last captures are of the nursery at zoom 11, which does show the reworked ants, brood, queen and terrain. The surface, food sources and foraging column at mid zoom were reviewed before the final colour and limb-tone tweaks but not after them.
- **Keyboard activation was not exercised interactively.** The flight, trait and found-colony key handlers were verified through unit tests against a real `SaveService` and their panels were rendered and photographed, but this session had no way to send key presses to the raylib window. The same limitation applies to the recovery prompt's `R`/`N` keys from M3.
- The quadratic frontier-claim scan was fixed in `f564fb0`; it is not an open defect. The full T015 population, memory, sanitizer and two-hour gates remain open.
- First-purchase permanent-trait value remains unproven: full-branch comparisons cost 57 Legacy and cannot stand in for the first four-Legacy reward. Payouts of 6 and 8 still need measured wait-policy runs; see NEXT_STEPS and BALANCE_REPORT.
- Player-facing UI is still temporary cards and key bindings pending T014. Trails bias source choice but are not rendered as a debug overlay. Profiling, ASan/UBSan and `.app` packaging remain in M5. No project license has been chosen.

# Visual and interaction design

## Direction

A warm, miniature natural-history diorama. The farm occupies most of the window, with a thin instrument strip and a quiet paper-colored inspector. Ants, brood, food, and moving cargo provide the animation; dense dashboards and particle effects must not obscure them.

Use procedural primitives and a tiny original sprite atlas first. No external image generation or paid assets are necessary to begin. The archived brief references screenshots that were not supplied with this planning pass; do not claim this direction reproduces an unseen image.

## Palette and visual grammar

| Role | Starting color | Shape/behavior |
|---|---|---|
| Deep soil | `#302923` | Low-contrast granular pattern |
| Sandy layer | `#8D6946` | Sparse 2x2 flecks, darker at depth |
| Tunnel interior | `#1C1E1B` | Warmed a little beside walls; a trodden `#4A3A2C` floor where ground carries it |
| Stone lens | `#606368` | Cool grey flecks against warm soil; never removable |
| Foliage | `#708457` | Irregular tufts at surface |
| Sky | `#C9D5C3` | Narrow calm band |
| UI paper | `#EEE4CC` | Flat panels and thin borders |
| UI ink | `#252D28` | Legible text, no paragraph pixel font |
| Food carbohydrate | `#DDBD67` | Round droplet cargo plus label |
| Protein | `#C77F65` | Angular fragment cargo plus label |
| Brood | `#EADBC0` | Eggs, curved larvae, larger pupae |
| Selected / Legacy | `#98CBC3` | Outline / wing motif |

Terrain is baked once into a texture at three texels per cell and rebuilt only when a cell is dug, which replaced roughly eighty thousand rectangle draws per frame. That budget pays for sparse flecks, a depth gradient that cools and darkens with depth, a lit top edge on soil under open air and a shadowed edge above it, so a tunnel reads as carved rather than painted on. Grass is drawn as clumps of leaning blades in varied tones. The nursery is a soft warm hollow, not a drawn-on marker.

These are art tokens, not verified contrast ratios. Verify actual UI text/background pairs in T014; use darker ink or lighter panel surfaces as needed. Never encode shortage or selection by hue alone.

Worker sprites should show head/thorax/abdomen and legs at close zoom; draw a clean ant silhouette at middle zoom and a moving dot with cargo accent at far zoom. The queen is approximately 2x worker length. Selection uses an outline and label. Avoid drawing every leg at all distances.

**Implemented.** Ants are built from oriented segments along a heading derived from actual travel, so an ant walking right faces right; a stopped ant keeps the heading it last had. Below zoom 1.8 an ant is a dot with its cargo accent; from 1.8 it gains gaster, petiole, thorax and head with a lighter sheen along the top; from 3.0 it gains a six-legged alternating tripod gait, elbowed antennae, mandibles and eyes, with limbs a shade darker than the body so they read as legs. Winged queens carry two translucent swept wings and sit between worker and queen in size. Cargo is held at the mandibles rather than floating overhead, and spoil and corpses are drawn as well as food. Chitin is warm brown rather than UI ink so an ant reads against the near-black tunnel.

## Desktop layout

Default window 1440x900 logical pixels; minimum supported 1024x640. UI scales independently of terrain pixels on Retina screens.

```text
┌────────────────────────────────────────────────────────────────────┐
│ ANT FARM   Workers 126  Brood 38   Carbs 84  Protein 29  Work 112    │
├───────────────────────────────────────────────┬────────────────────┤
│ sky + surface foraging                        │ Colony             │
│ ────── grass ────── spoil heap ───── food      │ Growing steadily   │
│     ╲                 ╱                       │ Focus: Balanced    │
│      ╲____ brood ____/                         │                    │
│           ╲   queen                           │ [Adaptations]      │
│            ╲_______                           │ Excavation II      │
│  living cross-section                         │ +25% / 24 Work     │
│                                               │                    │
│                                               │ [Selected ant]     │
│                                               │ Carrying protein   │
├───────────────────────────────────────────────┴────────────────────┤
│ [Pause] [1x] [5x] [20x]     Generation 1    [Colony] [Flight] [Menu] │
└────────────────────────────────────────────────────────────────────┘
```

The right inspector uses about 280 logical pixels on a wide display. At 1024x640 it becomes a collapsible overlay, never shrinks the text below the supported size. Keep the simulation visible behind the Flight preview until confirmation. Menus pause the game.

The numeric HUD is a final-state mock, not starting values. “Growing steadily” must derive from measured births/deaths and bottleneck state, not static copy. Hide winged-queen counts until relevant.

## Panels

**Implemented — food you can see.** The two forage sites are a fallen berry ringed with seed husks and a dead beetle, both shrinking as the colony carries them away. Stored food is drawn where it actually is: every heap in a store chamber grows up from the chamber floor as the pile fills, in the nutrient's own colour, and collapses to a tinted cell below zoom 2.2. Brood held by a nurse rides on her interpolated pose rather than hopping cell to cell.

**Implemented — every action is a control.** Speed, pause, save, focus, adaptations, the flight, permanent traits, founding the next colony, restoring a damaged save and abandoning a run are all buttons. Icons are drawn as vector glyphs from a unit box scaled into the control, so one definition stays sharp at any size and display scale. Hover lifts and warms a control, press settles it, and hover always names what a control does — including a control that is currently unavailable, which says why instead of staying silent. Abandoning a colony arms a confirmation before it commits. Gait and other world motion run off a clock that only advances with the simulation, so a paused ant holds still instead of treading air.

**Colony:** queen condition, workers/brood, food trend, nursery occupancy, one primary limiting condition. For example, “Larvae need protein — Foraging focus can help.” Use a rolling 30 sim-second rate after enough observations; show “Measuring…” before then.

**Adaptations:** four cards. Each shows level, current effect, next effect, cost, and why unavailable. Tooltips explain multiplicative versus additive effects in plain language. Clicking once buys one level. No hold-to-repeat purchase in v0.1.

**Inspector:** ant task and cargo; brood stage and remaining active development time; source availability/refill; nursery used/capacity. Show estimates as estimates when feeding stalls. Stable selected ID survives sorting but clears safely on death/removal.

**Flight:** maturity checklist, available winged queens, guaranteed payout, and “Next Legacy at 600 workers born” style threshold hints computed from the actual formula. Confirmation lists exactly what resets and what persists. Cancel preserves the running colony.

**Legacy shop:** Vigor and Industry in two columns, four tiers each. Owned, affordable, unaffordable, and locked states have icons/text. One shared wallet above both. Starting the next colony never requires spending all Legacy.

**Save error:** retained colony visible, concise error, Retry and choose-another-location options. Do not display a successful flight/shop if the prestige state could not be saved.

## Controls and accessibility

- Left click selects; empty space clears selection. Middle drag or right drag pans; WASD/arrow keys pan when gameplay owns focus.
- Wheel zooms toward pointer; `+`/`-` zoom about viewport center. Clamp camera to useful bounds.
- Space toggles pause; 1/2/3 choose 1x/5x/20x. Escape closes a panel or opens pause menu.
- Tab/Shift-Tab moves through controls; Enter/Space activates focused UI without also triggering gameplay pause.
- UI consumes input before world picking. A purchase can never also click terrain underneath it.
- Minimum 16 logical-pixel body text, 36 logical-pixel button height, visible keyboard focus, labeled icons, scalable UI (100/125/150%). Pixel styling applies to world art, not tiny unreadable text.
- Reduced motion disables camera easing, pulsing highlights, and decorative flight particles. Core ant motion remains.
- Simulation state is also summarized in readable text. Native screen-reader integration is not promised for the first raylib release; document this limitation rather than claiming full accessibility.

## Rendering and feedback

Terrain uses nearest-neighbor sampling and chunk dirty updates. Draw order: soil, tunnel edge detail, static chamber markers, brood/corpses, ants/cargo, surface details, selection, UI. Decorative randomness must use a separate visual seed.

Purchase feedback: card acknowledges purchase and the relevant work rate changes; no resource fountain. Birth feedback: one pale worker emerges. Flight: short winged ascent (skippable), then shop. Saving succeeds before this transition begins.

Do a camera-and-ant legibility pass in the first visual slice; custom player-facing controls are due before release. Dear ImGui, if introduced, is a developer-only diagnostic tool, not the intended final game UI.

## Required visual review scenes

Starter farm; busy 100-worker nursery; 1,000-worker overview; selected ant with cargo; food shortage; unavailable upgrade; flight preview; corrupted-save recovery; minimum-size window; Retina UI scaling; keyboard-only focus navigation. Record which scenes actually exist at each milestone.

## Accepted next visual and interaction work (2026-09-07)

[NEXT_STEPS](../planning/NEXT_STEPS.md) defines the requested subtle worker size/color variation, shared drawn/picked poses, input ownership, responsive inspector and complete generation flow. These are implementation requirements for T014a–d, not delivered features. Existing whole-map terrain baking is implemented; chunk dirty uploads above remain a performance target.

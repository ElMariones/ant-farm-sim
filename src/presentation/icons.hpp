#pragma once

#include <raylib.h>

namespace ant::presentation {

// A small vector glyph set drawn from raylib primitives. Every icon is defined inside a unit box
// and scaled into the rectangle it is given, so one definition stays sharp at any control size and
// on any display scale — the same reason interface art is normally shipped as SVG.
enum class Icon {
  Play,
  Pause,
  SpeedOne,
  SpeedFast,
  SpeedFastest,
  Save,
  Flight,
  Colony,
  Reset,
  Close,
  Mandibles,
  Nursery,
  Trail,
  Queen,
  Worker,
  Egg,
  Seed,
  Prey,
  Balance,
  Growth,
  Expansion,
  Restore,
  Check,
  Cross
};

// `bounds` is the square the glyph is fitted into; it is centred on the shorter side.
void draw_icon(Icon icon, Rectangle bounds, Color color);

} // namespace ant::presentation

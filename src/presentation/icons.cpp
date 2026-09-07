#include "presentation/icons.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace ant::presentation {
namespace {

// Every glyph is authored in a 0..1 box. This carries the mapping into the target rectangle so the
// shape definitions below read as plain geometry.
struct Canvas {
  Vector2 origin;
  float size;

  [[nodiscard]] Vector2 at(const float x, const float y) const {
    return {origin.x + x * size, origin.y + y * size};
  }
  [[nodiscard]] float length(const float value) const { return value * size; }
};

void line(const Canvas& canvas, const float x0, const float y0, const float x1, const float y1,
          const float weight, const Color color) {
  DrawLineEx(canvas.at(x0, y0), canvas.at(x1, y1), std::max(1.0F, canvas.length(weight)), color);
}

void disc(const Canvas& canvas, const float x, const float y, const float radius,
          const Color color) {
  DrawCircleV(canvas.at(x, y), canvas.length(radius), color);
}

void ring(const Canvas& canvas, const float x, const float y, const float radius,
          const float weight, const Color color) {
  DrawRing(canvas.at(x, y), canvas.length(radius - weight * 0.5F),
           canvas.length(radius + weight * 0.5F), 0.0F, 360.0F, 24, color);
}

void triangle(const Canvas& canvas, const Vector2 a, const Vector2 b, const Vector2 c,
              const Color color) {
  DrawTriangle(canvas.at(a.x, a.y), canvas.at(b.x, b.y), canvas.at(c.x, c.y), color);
}

void bar(const Canvas& canvas, const float x, const float y, const float width, const float height,
         const Color color) {
  DrawRectangleRounded({canvas.at(x, y).x, canvas.at(x, y).y, canvas.length(width),
                        canvas.length(height)},
                       0.35F, 4, color);
}

// A soft leaf shape, reused for the grass, wing and blade glyphs.
void leaf(const Canvas& canvas, const float x, const float y, const float reach, const float lean,
          const Color color) {
  std::array<Vector2, 10> points{};
  for (std::size_t index = 0; index < points.size(); ++index) {
    const float t = static_cast<float>(index) / static_cast<float>(points.size() - 1);
    const float spread = std::sin(t * PI) * reach * 0.34F;
    points[index] = canvas.at(x + lean * t + spread, y - reach * t);
  }
  for (std::size_t index = 0; index + 1 < points.size(); ++index) {
    DrawLineEx(points[index], points[index + 1], std::max(1.0F, canvas.length(0.09F)), color);
  }
}

} // namespace

void draw_icon(const Icon icon, const Rectangle bounds, const Color color) {
  const float size = std::min(bounds.width, bounds.height);
  const Canvas canvas{{bounds.x + (bounds.width - size) * 0.5F,
                       bounds.y + (bounds.height - size) * 0.5F},
                      size};

  switch (icon) {
  case Icon::Play:
    triangle(canvas, {0.30F, 0.18F}, {0.30F, 0.82F}, {0.80F, 0.50F}, color);
    break;
  case Icon::Pause:
    bar(canvas, 0.28F, 0.20F, 0.15F, 0.60F, color);
    bar(canvas, 0.57F, 0.20F, 0.15F, 0.60F, color);
    break;
  case Icon::SpeedOne:
    triangle(canvas, {0.36F, 0.20F}, {0.36F, 0.80F}, {0.74F, 0.50F}, color);
    break;
  case Icon::SpeedFast:
    triangle(canvas, {0.16F, 0.22F}, {0.16F, 0.78F}, {0.50F, 0.50F}, color);
    triangle(canvas, {0.48F, 0.22F}, {0.48F, 0.78F}, {0.82F, 0.50F}, color);
    break;
  case Icon::SpeedFastest:
    triangle(canvas, {0.06F, 0.24F}, {0.06F, 0.76F}, {0.34F, 0.50F}, color);
    triangle(canvas, {0.34F, 0.24F}, {0.34F, 0.76F}, {0.62F, 0.50F}, color);
    triangle(canvas, {0.62F, 0.24F}, {0.62F, 0.76F}, {0.90F, 0.50F}, color);
    break;
  case Icon::Save:
    // A floppy outline still reads as "write this down" better than anything else at this size.
    DrawRectangleRoundedLinesEx({canvas.at(0.18F, 0.18F).x, canvas.at(0.18F, 0.18F).y,
                                 canvas.length(0.64F), canvas.length(0.64F)},
                                0.16F, 5, std::max(1.0F, canvas.length(0.08F)), color);
    bar(canvas, 0.34F, 0.20F, 0.32F, 0.20F, color);
    bar(canvas, 0.30F, 0.52F, 0.40F, 0.26F, color);
    break;
  case Icon::Flight:
    // A winged queen taking off.
    leaf(canvas, 0.46F, 0.62F, 0.44F, -0.28F, color);
    leaf(canvas, 0.54F, 0.62F, 0.44F, 0.28F, color);
    disc(canvas, 0.50F, 0.68F, 0.11F, color);
    disc(canvas, 0.50F, 0.86F, 0.08F, color);
    break;
  case Icon::Colony:
    // A cross-section: surface line, entrance and two chambers.
    line(canvas, 0.10F, 0.28F, 0.90F, 0.28F, 0.08F, color);
    line(canvas, 0.50F, 0.28F, 0.50F, 0.56F, 0.08F, color);
    ring(canvas, 0.30F, 0.68F, 0.16F, 0.08F, color);
    ring(canvas, 0.70F, 0.68F, 0.16F, 0.08F, color);
    line(canvas, 0.30F, 0.56F, 0.70F, 0.56F, 0.07F, color);
    break;
  case Icon::Reset:
  case Icon::Restore: {
    // An arrow curling back on itself.
    DrawRing(canvas.at(0.50F, 0.52F), canvas.length(0.24F), canvas.length(0.34F), 40.0F, 320.0F,
             28, color);
    triangle(canvas, {0.62F, 0.10F}, {0.86F, 0.24F}, {0.60F, 0.36F}, color);
    break;
  }
  case Icon::Close:
  case Icon::Cross:
    line(canvas, 0.26F, 0.26F, 0.74F, 0.74F, 0.11F, color);
    line(canvas, 0.74F, 0.26F, 0.26F, 0.74F, 0.11F, color);
    break;
  case Icon::Check:
    line(canvas, 0.22F, 0.52F, 0.42F, 0.72F, 0.12F, color);
    line(canvas, 0.42F, 0.72F, 0.78F, 0.28F, 0.12F, color);
    break;
  case Icon::Mandibles:
    // Two opposed jaws.
    line(canvas, 0.50F, 0.82F, 0.50F, 0.56F, 0.10F, color);
    line(canvas, 0.50F, 0.56F, 0.22F, 0.34F, 0.10F, color);
    line(canvas, 0.22F, 0.34F, 0.34F, 0.14F, 0.10F, color);
    line(canvas, 0.50F, 0.56F, 0.78F, 0.34F, 0.10F, color);
    line(canvas, 0.78F, 0.34F, 0.66F, 0.14F, 0.10F, color);
    break;
  case Icon::Nursery:
    // Three eggs in a hollow.
    disc(canvas, 0.32F, 0.56F, 0.13F, color);
    disc(canvas, 0.58F, 0.44F, 0.13F, color);
    disc(canvas, 0.60F, 0.70F, 0.13F, color);
    DrawRing(canvas.at(0.50F, 0.56F), canvas.length(0.36F), canvas.length(0.42F), 200.0F, 520.0F,
             28, color);
    break;
  case Icon::Trail:
    // A dotted route between two points.
    disc(canvas, 0.16F, 0.76F, 0.10F, color);
    disc(canvas, 0.84F, 0.24F, 0.10F, color);
    for (int step = 1; step <= 4; ++step) {
      const float t = static_cast<float>(step) / 5.0F;
      disc(canvas, 0.16F + 0.68F * t, 0.76F - 0.52F * t + std::sin(t * PI) * 0.12F, 0.055F, color);
    }
    break;
  case Icon::Queen:
    // A crowned head.
    triangle(canvas, {0.20F, 0.56F}, {0.34F, 0.24F}, {0.46F, 0.56F}, color);
    triangle(canvas, {0.38F, 0.56F}, {0.50F, 0.16F}, {0.62F, 0.56F}, color);
    triangle(canvas, {0.54F, 0.56F}, {0.66F, 0.24F}, {0.80F, 0.56F}, color);
    bar(canvas, 0.20F, 0.58F, 0.60F, 0.18F, color);
    break;
  case Icon::Worker:
    // Three body segments and a pair of antennae.
    disc(canvas, 0.30F, 0.50F, 0.17F, color);
    disc(canvas, 0.54F, 0.50F, 0.13F, color);
    disc(canvas, 0.74F, 0.50F, 0.11F, color);
    line(canvas, 0.80F, 0.44F, 0.94F, 0.30F, 0.06F, color);
    line(canvas, 0.80F, 0.56F, 0.94F, 0.66F, 0.06F, color);
    break;
  case Icon::Egg:
    disc(canvas, 0.50F, 0.54F, 0.26F, color);
    break;
  case Icon::Seed:
    // A grain with its crease.
    disc(canvas, 0.50F, 0.50F, 0.28F, color);
    break;
  case Icon::Prey:
    // A beetle: body and legs.
    disc(canvas, 0.50F, 0.52F, 0.22F, color);
    for (int side = -1; side <= 1; side += 2) {
      const float sign = static_cast<float>(side);
      line(canvas, 0.50F + 0.18F * sign, 0.38F, 0.50F + 0.36F * sign, 0.26F, 0.07F, color);
      line(canvas, 0.50F + 0.20F * sign, 0.52F, 0.50F + 0.40F * sign, 0.52F, 0.07F, color);
      line(canvas, 0.50F + 0.18F * sign, 0.66F, 0.50F + 0.36F * sign, 0.78F, 0.07F, color);
    }
    break;
  case Icon::Balance:
    ring(canvas, 0.50F, 0.50F, 0.28F, 0.09F, color);
    disc(canvas, 0.50F, 0.50F, 0.09F, color);
    break;
  case Icon::Growth:
    leaf(canvas, 0.50F, 0.82F, 0.42F, -0.20F, color);
    leaf(canvas, 0.50F, 0.82F, 0.32F, 0.22F, color);
    line(canvas, 0.50F, 0.86F, 0.50F, 0.52F, 0.08F, color);
    break;
  case Icon::Expansion:
    // Arrows pushing outward from a centre.
    line(canvas, 0.50F, 0.50F, 0.20F, 0.50F, 0.09F, color);
    line(canvas, 0.50F, 0.50F, 0.80F, 0.50F, 0.09F, color);
    line(canvas, 0.50F, 0.50F, 0.50F, 0.80F, 0.09F, color);
    triangle(canvas, {0.08F, 0.50F}, {0.24F, 0.40F}, {0.24F, 0.60F}, color);
    triangle(canvas, {0.92F, 0.50F}, {0.76F, 0.40F}, {0.76F, 0.60F}, color);
    triangle(canvas, {0.50F, 0.92F}, {0.40F, 0.76F}, {0.60F, 0.76F}, color);
    break;
  }
}

} // namespace ant::presentation

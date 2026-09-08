#include "game/session.hpp"
#include "game/view.hpp"
#include "persistence/content_loader.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Read-only, seeded CPU benchmark. No windows, profile writes or policy changes. Per-tick timing
// lives in this executable, never in the simulation; view construction is timed independently.
int main(int argc, char** argv) {
  try {
    std::uint64_t seed = 42, warmup = 30'000, ticks = 6'000;
    for (int i = 1; i < argc; ++i) {
      const std::string option = argv[i];
      if (option == "--help") {
        std::cout << "ant_benchmark [--seed N] [--warmup N] [--ticks N]\n";
        return 0;
      }
      if (i + 1 >= argc) throw std::invalid_argument("missing benchmark value");
      const std::string value = argv[++i];
      if (value.empty() || value.front() == '-') throw std::invalid_argument("expected positive integer");
      const auto number = std::stoull(value);
      if (option == "--seed") seed = number;
      else if (option == "--warmup") warmup = number;
      else if (option == "--ticks") ticks = number;
      else throw std::invalid_argument("unknown benchmark argument");
    }
    if (ticks == 0 || ticks > 1'000'000 || warmup > 1'000'000)
      throw std::invalid_argument("benchmark ticks must be 1..1000000; warmup 0..1000000");
    ant::game::Session session(seed, ant::persistence::load_progression(
        std::string(ANT_SOURCE_CONFIG_DIR) + "/progression.json"));
    session.step_ticks(warmup);
    const int workers_before = session.world().living_workers();
    const auto requests_before = session.world().stats().path_requests;
    using Clock = std::chrono::steady_clock;
    const auto elapsed_ms = [](auto from, auto to) {
      return std::chrono::duration<double, std::milli>(to - from).count();
    };
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(ticks));
    double sim_ms = 0.0, view_ms = 0.0;
    std::size_t views = 0, actors_seen = 0;
    for (std::uint64_t tick = 0; tick < ticks; ++tick) {
      const auto start = Clock::now();
      session.step();
      const double duration = elapsed_ms(start, Clock::now());
      samples.push_back(duration);
      sim_ms += duration;
      // 400 ticks/sec at 20x, 60 views/sec: exactly three views per twenty ticks.
      if ((tick * 3 / 20) != ((tick + 1) * 3 / 20)) {
        const auto view_start = Clock::now();
        const auto view = ant::game::make_view(session);
        actors_seen += view.actors.size();
        view_ms += elapsed_ms(view_start, Clock::now());
        ++views;
      }
    }
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](std::size_t p) { return samples[(samples.size() * p + 99) / 100 - 1]; };
    std::cout << std::fixed << std::setprecision(3)
              << "{\"seed\":" << seed << ",\"warmupTicks\":" << warmup << ",\"measuredTicks\":" << ticks
              << ",\"workersBefore\":" << workers_before << ",\"workersAfter\":" << session.world().living_workers()
              << ",\"simMs\":" << sim_ms << ",\"meanTickMs\":" << sim_ms / static_cast<double>(ticks)
              << ",\"p95TickMs\":" << percentile(95) << ",\"p99TickMs\":" << percentile(99)
              << ",\"maxTickMs\":" << samples.back() << ",\"viewMs\":" << view_ms
              << ",\"views\":" << views << ",\"actorsSeen\":" << actors_seen
              << ",\"cpuMsPer20xFrame\":" << (sim_ms + view_ms) / (static_cast<double>(ticks) * 3.0 / 20.0)
              << ",\"pathRequests\":" << session.world().stats().path_requests - requests_before
              << ",\"hash\":\"" << std::hex << session.world().canonical_hash() << "\"}\n";
    return session.world().invariant_holds() ? 0 : 2;
  } catch (const std::exception& error) {
    std::cerr << "ant_benchmark: " << error.what() << '\n';
    return 1;
  }
}

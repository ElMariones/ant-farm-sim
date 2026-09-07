#pragma once

#include "game/snapshot.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace ant::game { class Session; }

namespace ant::persistence {

inline constexpr std::size_t kMaximumProfileBytes = 64U * 1024U * 1024U;

struct DecodeResult {
  std::optional<game::ProfileSnapshot> profile;
  std::string error;
};

[[nodiscard]] std::string encode_profile(const game::ProfileSnapshot& profile);
[[nodiscard]] DecodeResult decode_profile(std::string_view document);
[[nodiscard]] game::ProfileSnapshot make_new_profile(const game::Session& session,
                                                     std::uint64_t revision = 0);

} // namespace ant::persistence

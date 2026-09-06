#pragma once

#include <string_view>

namespace ant::persistence {

// M0 reserves the owning module boundary. Snapshot encoding and durable writes begin in T009.
[[nodiscard]] std::string_view capability_status();

} // namespace ant::persistence

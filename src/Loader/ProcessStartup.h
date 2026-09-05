#pragma once
#include "Loader/StartupFailure.h"
namespace gc::loader {
[[nodiscard]] std::expected<PreparedProcessConfiguration, StartupError>
PrepareProcessStartup(HMODULE loader_module) noexcept;
}

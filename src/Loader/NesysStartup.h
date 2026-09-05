#pragma once
#include "Loader/StartupFailure.h"
namespace gc::loader {
[[nodiscard]] std::expected<void, StartupError> StartNesys(HMODULE, NesysProcessConfiguration) noexcept;
}

#pragma once
#include "Loader/StartupFailure.h"
namespace gc::loader {
[[nodiscard]] std::expected<void, StartupError> StartGame(HMODULE, GameProcessConfiguration) noexcept;
}

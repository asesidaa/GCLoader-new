#pragma once

#include "Input/Types/InputTypes.h"

#include <expected>
#include <string>
#include <string_view>

namespace gc::input {

std::expected<PhysicalKey, std::string> ParsePhysicalKey(
    std::string_view token);
std::string FormatPhysicalKey(PhysicalKey key);

} // namespace gc::input

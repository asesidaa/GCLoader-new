#pragma once
#include "Config/ConfigDocument.h"
#include "Config/Validation/ValidationContext.h"
#include <vector>

namespace gc::config::validation {
// Returns leaf validity for the later experimental dependency checks.
[[nodiscard]] bool ValidateInput(const ConfigDocument&, ValidationContext&);
[[nodiscard]] std::vector<input::KeyboardBinding> CompileKeyboard(const NativeKeyboardConfig&);
}

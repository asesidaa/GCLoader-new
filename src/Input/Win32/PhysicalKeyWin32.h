#pragma once

#include "Input/Types/InputTypes.h"

#include <Windows.h>

#include <optional>
#include <string>

namespace gc::input {

struct KeyboardTransition {
    PhysicalKey key{};
    bool pressed{};

    bool operator==(const KeyboardTransition&) const = default;
};

std::optional<KeyboardTransition> DecodeRawKeyboard(
    const RAWKEYBOARD& keyboard) noexcept;
std::wstring PhysicalKeyLabel(
    PhysicalKey key,
    HKL layout = GetKeyboardLayout(0));
UINT PhysicalKeyToVirtualKey(
    PhysicalKey key,
    HKL layout = GetKeyboardLayout(0)) noexcept;

} // namespace gc::input

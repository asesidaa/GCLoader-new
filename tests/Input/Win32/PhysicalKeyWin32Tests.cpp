#include "Input/Win32/PhysicalKeyWin32.h"

#include "Input/Types/PhysicalKey.h"

#include <Windows.h>

#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

RAWKEYBOARD raw(USHORT make_code, USHORT flags)
{
    RAWKEYBOARD keyboard{};
    keyboard.MakeCode = make_code;
    keyboard.Flags = flags;
    return keyboard;
}

int expect_transition(
    const std::optional<gc::input::KeyboardTransition>& actual,
    gc::input::PhysicalKey expected_key,
    bool expected_pressed,
    std::string_view name)
{
    if (actual && actual->key == expected_key &&
        actual->pressed == expected_pressed)
    {
        return 0;
    }

    std::cerr << name << ": unexpected keyboard transition\n";
    return 1;
}

int expect_true(bool actual, std::string_view name)
{
    if (actual)
    {
        return 0;
    }
    std::cerr << name << ": expected true\n";
    return 1;
}

int expect_uint(UINT actual, UINT expected, std::string_view name)
{
    if (actual == expected)
    {
        return 0;
    }
    std::cerr << name << ": expected 0x" << std::hex << expected
              << ", got 0x" << actual << std::dec << '\n';
    return 1;
}

} // namespace

int main()
{
    using namespace gc::input;

    int failures = 0;

    failures += expect_transition(
        DecodeRawKeyboard(raw(0x14, RI_KEY_MAKE)),
        {0x14, ScanCodePrefix::None},
        true,
        "ordinary make");
    failures += expect_transition(
        DecodeRawKeyboard(raw(0x14, RI_KEY_BREAK)),
        {0x14, ScanCodePrefix::None},
        false,
        "ordinary break");
    failures += expect_transition(
        DecodeRawKeyboard(raw(0x48, RI_KEY_E0)),
        {0x48, ScanCodePrefix::E0},
        true,
        "E0 make");
    failures += expect_transition(
        DecodeRawKeyboard(raw(0x48, RI_KEY_E0 | RI_KEY_BREAK)),
        {0x48, ScanCodePrefix::E0},
        false,
        "E0 break");
    failures += expect_transition(
        DecodeRawKeyboard(raw(0x45, RI_KEY_E1)),
        {0x45, ScanCodePrefix::E1},
        true,
        "E1 make");
    failures += expect_true(
        !DecodeRawKeyboard(raw(KEYBOARD_OVERRUN_MAKE_CODE, RI_KEY_MAKE)),
        "overrun rejected");
    failures += expect_true(
        !DecodeRawKeyboard(raw(0, RI_KEY_MAKE)),
        "zero make code rejected");

    const auto first_repeat = DecodeRawKeyboard(raw(0x14, RI_KEY_MAKE));
    const auto second_repeat = DecodeRawKeyboard(raw(0x14, RI_KEY_MAKE));
    failures += expect_true(
        first_repeat == second_repeat,
        "typematic makes decode identically");

    const HKL layout = GetKeyboardLayout(0);
    const PhysicalKey t_key{0x14, ScanCodePrefix::None};
    const PhysicalKey left_control{0x1d, ScanCodePrefix::None};
    const PhysicalKey right_control{0x1d, ScanCodePrefix::E0};
    const PhysicalKey main_enter{0x1c, ScanCodePrefix::None};
    const PhysicalKey numpad_enter{0x1c, ScanCodePrefix::E0};
    const PhysicalKey arrow_up{0x48, ScanCodePrefix::E0};

    failures += expect_true(!PhysicalKeyLabel(t_key, layout).empty(), "T label");
    failures += expect_true(
        !PhysicalKeyLabel(left_control, layout).empty(),
        "left Control label");
    failures += expect_true(
        !PhysicalKeyLabel(right_control, layout).empty(),
        "right Control label");
    failures += expect_true(
        !PhysicalKeyLabel(main_enter, layout).empty(),
        "main Enter label");
    failures += expect_true(
        !PhysicalKeyLabel(numpad_enter, layout).empty(),
        "numpad Enter label");
    failures += expect_true(
        !PhysicalKeyLabel(arrow_up, layout).empty(),
        "arrow Up label");
    failures += expect_true(
        PhysicalKeyLabel({0xffff, ScanCodePrefix::None}, layout) ==
            L"sc:ffff",
        "unknown label fallback");

    failures += expect_uint(
        PhysicalKeyToVirtualKey(
            {0x3e, ScanCodePrefix::None}, layout),
        VK_F4,
        "F4 virtual key");
    failures += expect_uint(
        PhysicalKeyToVirtualKey(t_key, layout),
        MapVirtualKeyExW(0x14, MAPVK_VSC_TO_VK_EX, layout),
        "layout virtual key");
    failures += expect_uint(
        PhysicalKeyToVirtualKey({}, layout),
        0,
        "invalid virtual key");

    if (failures != 0)
    {
        return 1;
    }

    std::cout << "PhysicalKeyWin32Tests passed\n";
    return 0;
}

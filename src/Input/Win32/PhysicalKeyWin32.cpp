#include "Input/Win32/PhysicalKeyWin32.h"

#include "Input/Types/PhysicalKey.h"

#include <array>
#include <string>

namespace gc::input {
namespace {

UINT ExtendedScanCode(PhysicalKey key) noexcept
{
    UINT scan_code = key.make_code;
    switch (key.prefix)
    {
    case ScanCodePrefix::None:
        break;
    case ScanCodePrefix::E0:
        scan_code |= 0xe000u;
        break;
    case ScanCodePrefix::E1:
        scan_code |= 0xe100u;
        break;
    }
    return scan_code;
}

std::wstring CanonicalWideLabel(PhysicalKey key)
{
    const auto token = FormatPhysicalKey(key);
    return {token.begin(), token.end()};
}

} // namespace

std::optional<KeyboardTransition> DecodeRawKeyboard(
    const RAWKEYBOARD& keyboard) noexcept
{
    if (keyboard.MakeCode == 0 ||
        keyboard.MakeCode == KEYBOARD_OVERRUN_MAKE_CODE)
    {
        return std::nullopt;
    }

    ScanCodePrefix prefix = ScanCodePrefix::None;
    if ((keyboard.Flags & RI_KEY_E1) != 0)
    {
        prefix = ScanCodePrefix::E1;
    }
    else if ((keyboard.Flags & RI_KEY_E0) != 0)
    {
        prefix = ScanCodePrefix::E0;
    }

    return KeyboardTransition{
        .key = PhysicalKey{keyboard.MakeCode, prefix},
        .pressed = (keyboard.Flags & RI_KEY_BREAK) == 0,
    };
}

std::wstring PhysicalKeyLabel(PhysicalKey key, HKL layout)
{
    if (key.make_code == 0 || key.make_code > 0xff)
    {
        return CanonicalWideLabel(key);
    }

    LONG key_name_parameter =
        static_cast<LONG>(key.make_code) << 16;
    if (key.prefix != ScanCodePrefix::None)
    {
        key_name_parameter |= 1L << 24;
    }

    std::array<wchar_t, 128> label{};
    if (GetKeyNameTextW(
            key_name_parameter,
            label.data(),
            static_cast<int>(label.size())) > 0)
    {
        return label.data();
    }

    const UINT virtual_key = PhysicalKeyToVirtualKey(key, layout);
    if (virtual_key != 0)
    {
        const UINT mapped_scan =
            MapVirtualKeyExW(virtual_key, MAPVK_VK_TO_VSC_EX, layout);
        LONG mapped_parameter =
            static_cast<LONG>(mapped_scan & 0xffu) << 16;
        if ((mapped_scan & 0xff00u) != 0)
        {
            mapped_parameter |= 1L << 24;
        }
        if (GetKeyNameTextW(
                mapped_parameter,
                label.data(),
                static_cast<int>(label.size())) > 0)
        {
            return label.data();
        }
    }

    return CanonicalWideLabel(key);
}

UINT PhysicalKeyToVirtualKey(PhysicalKey key, HKL layout) noexcept
{
    if (key.make_code == 0)
    {
        return 0;
    }
    return MapVirtualKeyExW(
        ExtendedScanCode(key),
        MAPVK_VSC_TO_VK_EX,
        layout);
}

} // namespace gc::input

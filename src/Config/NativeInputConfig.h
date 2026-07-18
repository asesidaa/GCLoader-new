#pragma once

#include "Config/InputRflParsers.h"
#include "Input/Types/InputTypes.h"

#include <rfl.hpp>

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace gc::config {

inline constexpr std::uint32_t kInputSchemaVersion = 2;

struct NativeKeyboardConfig {
    rfl::Rename<"left_booster_up", input::PhysicalKey>
        left_booster_up = input::PhysicalKey{0x11, input::ScanCodePrefix::None};
    rfl::Rename<"left_booster_down", input::PhysicalKey>
        left_booster_down = input::PhysicalKey{0x1f, input::ScanCodePrefix::None};
    rfl::Rename<"left_booster_left", input::PhysicalKey>
        left_booster_left = input::PhysicalKey{0x1e, input::ScanCodePrefix::None};
    rfl::Rename<"left_booster_right", input::PhysicalKey>
        left_booster_right = input::PhysicalKey{0x20, input::ScanCodePrefix::None};
    rfl::Rename<"left_booster_button", input::PhysicalKey>
        left_booster_button = input::PhysicalKey{0x39, input::ScanCodePrefix::None};

    rfl::Rename<"right_booster_up", input::PhysicalKey>
        right_booster_up = input::PhysicalKey{0x48, input::ScanCodePrefix::E0};
    rfl::Rename<"right_booster_down", input::PhysicalKey>
        right_booster_down = input::PhysicalKey{0x50, input::ScanCodePrefix::E0};
    rfl::Rename<"right_booster_left", input::PhysicalKey>
        right_booster_left = input::PhysicalKey{0x4b, input::ScanCodePrefix::E0};
    rfl::Rename<"right_booster_right", input::PhysicalKey>
        right_booster_right = input::PhysicalKey{0x4d, input::ScanCodePrefix::E0};
    rfl::Rename<"right_booster_button", input::PhysicalKey>
        right_booster_button = input::PhysicalKey{0x25, input::ScanCodePrefix::None};

    rfl::Rename<"test", input::PhysicalKey>
        test = input::PhysicalKey{0x14, input::ScanCodePrefix::None};
    rfl::Rename<"service1", input::PhysicalKey>
        service1 = input::PhysicalKey{0x3b, input::ScanCodePrefix::None};
    rfl::Rename<"service2", input::PhysicalKey>
        service2 = input::PhysicalKey{0x17, input::ScanCodePrefix::None};
    rfl::Rename<"service3", input::PhysicalKey>
        service3 = input::PhysicalKey{0x19, input::ScanCodePrefix::None};
    rfl::Rename<"p1_start", input::PhysicalKey>
        p1_start = input::PhysicalKey{0x02, input::ScanCodePrefix::None};
    rfl::Rename<"p2_start", input::PhysicalKey>
        p2_start = input::PhysicalKey{0x03, input::ScanCodePrefix::None};
    rfl::Rename<"p2_service", input::PhysicalKey>
        p2_service = input::PhysicalKey{0x3c, input::ScanCodePrefix::None};
    rfl::Rename<"card_read", input::PhysicalKey>
        card_read = input::PhysicalKey{0x3e, input::ScanCodePrefix::None};
};

struct ControllerConfig {
    rfl::Rename<"backend", input::ControllerBackend>
        backend{input::ControllerBackend::XInput};
    rfl::Rename<"device_id", std::string> device_id{"0"};
    rfl::Rename<"bindings", std::vector<input::DigitalControlBinding>>
        bindings{};
};

std::expected<void, std::string> ValidateNativeInputFields(
    std::uint32_t schema_version,
    std::uint32_t poll_hz,
    std::uint32_t press_percent,
    std::uint32_t release_percent,
    const NativeKeyboardConfig& keyboard,
    const ControllerConfig& controller);

} // namespace gc::config

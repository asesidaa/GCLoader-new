#pragma once

#include "Rfid/Jvs/Types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace gc::rfid {

namespace taito_command {
inline constexpr jvs::CommandId query_01{0x01};
inline constexpr jvs::CommandId query_03{0x03};
inline constexpr jvs::CommandId notify_04{0x04};
inline constexpr jvs::CommandId notify_05{0x05};
} // namespace taito_command

struct TaitoCommandResult {
    std::size_t consumed{};
    jvs::Report report{jvs::report::ok};
    std::array<std::uint8_t, 1> data{};
    std::uint8_t data_size{};
};

[[nodiscard]] std::optional<TaitoCommandResult> HandleTaitoCommand(
    jvs::CommandId command,
    std::span<const std::uint8_t> bytes_after_command) noexcept;

} // namespace gc::rfid

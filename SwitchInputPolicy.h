#pragma once

#include <cstdint>
#include <span>

namespace gc::switch_input {

using LogicalInputId = std::int32_t;

inline constexpr LogicalInputId kNoDirectionAlias = -1;

using LogicalInputQuery =
    std::uint8_t (*)(void* context, LogicalInputId logical_input) noexcept;

struct AliasQueryResult {
    std::uint8_t value{0};
    LogicalInputId accepted_direction{kNoDirectionAlias};
};

std::span<const LogicalInputId> DirectionAliasesForButton(
    LogicalInputId requested_input) noexcept;

bool IsSwitchDiagonalComponent(
    LogicalInputId target_direction,
    LogicalInputId current_direction) noexcept;

AliasQueryResult QueryButtonWithDirectionAliases(
    LogicalInputId requested_input,
    void* context,
    LogicalInputQuery query) noexcept;

} // namespace gc::switch_input

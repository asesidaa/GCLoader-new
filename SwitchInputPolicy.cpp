#include "SwitchInputPolicy.h"

#include <array>

namespace gc::switch_input {
namespace {

constexpr std::array<LogicalInputId, 4> kLeftButtonDirections{0, 1, 2, 3};
constexpr std::array<LogicalInputId, 4> kRightButtonDirections{5, 6, 7, 8};

} // namespace

std::span<const LogicalInputId> DirectionAliasesForButton(
    LogicalInputId requested_input) noexcept {
    switch (requested_input) {
    case 4:
        return kLeftButtonDirections;
    case 9:
        return kRightButtonDirections;
    default:
        return {};
    }
}

bool IsSwitchDiagonalComponent(
    LogicalInputId target_direction,
    LogicalInputId current_direction) noexcept {
    switch (target_direction) {
    case 1:
        return current_direction == 2 || current_direction == 4;
    case 3:
        return current_direction == 2 || current_direction == 6;
    case 7:
        return current_direction == 8 || current_direction == 4;
    case 9:
        return current_direction == 8 || current_direction == 6;
    default:
        return false;
    }
}

AliasQueryResult QueryButtonWithDirectionAliases(
    LogicalInputId requested_input,
    void* context,
    LogicalInputQuery query) noexcept {
    if (query == nullptr) {
        return {};
    }

    const auto native_value = query(context, requested_input);
    if (native_value != 0) {
        return {native_value, kNoDirectionAlias};
    }

    for (const auto direction : DirectionAliasesForButton(requested_input)) {
        const auto direction_value = query(context, direction);
        if (direction_value != 0) {
            return {direction_value, direction};
        }
    }

    return {native_value, kNoDirectionAlias};
}

} // namespace gc::switch_input

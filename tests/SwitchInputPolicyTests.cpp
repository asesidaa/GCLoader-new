#include "SwitchInputPolicy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <span>

namespace {

using gc::switch_input::LogicalInputId;

int expect_true(bool actual, const char* name) {
    if (actual) {
        return 0;
    }
    std::cerr << "Expected true for " << name << "\n";
    return 1;
}

int expect_false(bool actual, const char* name) {
    if (!actual) {
        return 0;
    }
    std::cerr << "Expected false for " << name << "\n";
    return 1;
}

int expect_int(int actual, int expected, const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " to be " << expected
              << ", got " << actual << "\n";
    return 1;
}

int expect_ids(
    std::span<const LogicalInputId> actual,
    std::initializer_list<LogicalInputId> expected,
    const char* name) {
    if (actual.size() != expected.size()) {
        std::cerr << "Expected " << name << " size " << expected.size()
                  << ", got " << actual.size() << "\n";
        return 1;
    }

    std::size_t index = 0;
    for (const auto value : expected) {
        if (actual[index] != value) {
            std::cerr << "Expected " << name << "[" << index << "] to be "
                      << value << ", got " << actual[index] << "\n";
            return 1;
        }
        ++index;
    }
    return 0;
}

bool expected_diagonal_component(
    LogicalInputId target,
    LogicalInputId current) {
    switch (target) {
    case 1:
        return current == 2 || current == 4;
    case 3:
        return current == 2 || current == 6;
    case 7:
        return current == 8 || current == 4;
    case 9:
        return current == 8 || current == 6;
    default:
        return false;
    }
}

struct QueryProbe {
    std::array<std::uint8_t, 16> values{};
    std::array<LogicalInputId, 16> calls{};
    std::size_t call_count{0};
};

std::uint8_t probe_query(void* context, LogicalInputId logical_input) noexcept {
    auto& probe = *static_cast<QueryProbe*>(context);
    if (probe.call_count < probe.calls.size()) {
        probe.calls[probe.call_count] = logical_input;
    }
    ++probe.call_count;

    if (logical_input < 0 ||
        static_cast<std::size_t>(logical_input) >= probe.values.size()) {
        return 0;
    }
    return probe.values[static_cast<std::size_t>(logical_input)];
}

} // namespace

int main() {
    using namespace gc::switch_input;
    int failures = 0;

    failures += expect_ids(DirectionAliasesForButton(4), {0, 1, 2, 3}, "left button aliases");
    failures += expect_ids(DirectionAliasesForButton(9), {5, 6, 7, 8}, "right button aliases");
    for (LogicalInputId requested = -1; requested <= 14; ++requested) {
        if (requested != 4 && requested != 9) {
            failures += expect_ids(
                DirectionAliasesForButton(requested),
                {},
                "non-button aliases");
        }
    }

    for (LogicalInputId target = 1; target <= 9; ++target) {
        for (LogicalInputId current = 1; current <= 9; ++current) {
            const bool expected = expected_diagonal_component(target, current);
            const bool actual = IsSwitchDiagonalComponent(target, current);
            failures += expected
                ? expect_true(actual, "accepted diagonal component")
                : expect_false(actual, "rejected diagonal component");
        }
    }
    failures += expect_false(IsSwitchDiagonalComponent(0, 2), "invalid target zero");
    failures += expect_false(IsSwitchDiagonalComponent(1, 10), "invalid current ten");

    QueryProbe native_button{};
    native_button.values[4] = 7;
    native_button.values[0] = 1;
    const auto native_result =
        QueryButtonWithDirectionAliases(4, &native_button, probe_query);
    failures += expect_int(native_result.value, 7, "native button result");
    failures += expect_int(
        native_result.accepted_direction,
        kNoDirectionAlias,
        "native button accepted direction");
    failures += expect_int(
        static_cast<int>(native_button.call_count),
        1,
        "native button query count");
    failures += expect_int(native_button.calls[0], 4, "native button first query");

    QueryProbe ordinary_direction{};
    ordinary_direction.values[2] = 3;
    const auto ordinary_result =
        QueryButtonWithDirectionAliases(2, &ordinary_direction, probe_query);
    failures += expect_int(ordinary_result.value, 3, "ordinary direction result");
    failures += expect_int(
        static_cast<int>(ordinary_direction.call_count),
        1,
        "ordinary direction query count");
    failures += expect_int(ordinary_direction.calls[0], 2, "ordinary direction query");

    QueryProbe first_edge{};
    first_edge.values[0] = 1;
    const auto first_edge_result =
        QueryButtonWithDirectionAliases(4, &first_edge, probe_query);
    failures += expect_int(first_edge_result.value, 1, "first direction edge result");
    failures += expect_int(first_edge_result.accepted_direction, 0, "first direction edge");
    failures += expect_int(
        static_cast<int>(first_edge.call_count),
        2,
        "first direction edge query count");

    QueryProbe second_edge{};
    second_edge.values[1] = 1;
    const auto second_edge_result =
        QueryButtonWithDirectionAliases(4, &second_edge, probe_query);
    failures += expect_int(second_edge_result.value, 1, "second direction edge result");
    failures += expect_int(second_edge_result.accepted_direction, 1, "second direction edge");
    failures += expect_int(
        static_cast<int>(second_edge.call_count),
        3,
        "second direction edge query count");
    failures += expect_int(second_edge.calls[0], 4, "second edge native query");
    failures += expect_int(second_edge.calls[1], 0, "second edge first alias query");
    failures += expect_int(second_edge.calls[2], 1, "second edge independent alias query");

    QueryProbe right_edge{};
    right_edge.values[8] = 1;
    const auto right_edge_result =
        QueryButtonWithDirectionAliases(9, &right_edge, probe_query);
    failures += expect_int(right_edge_result.accepted_direction, 8, "right direction edge");
    failures += expect_int(
        static_cast<int>(right_edge.call_count),
        5,
        "right direction edge query count");

    QueryProbe no_match{};
    const auto no_match_result =
        QueryButtonWithDirectionAliases(4, &no_match, probe_query);
    failures += expect_int(no_match_result.value, 0, "no alias result");
    failures += expect_int(
        no_match_result.accepted_direction,
        kNoDirectionAlias,
        "no alias accepted direction");

    return failures == 0 ? 0 : 1;
}

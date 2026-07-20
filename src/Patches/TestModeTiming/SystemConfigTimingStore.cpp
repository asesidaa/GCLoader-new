#include "Patches/TestModeTiming/SystemConfigTimingStore.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gc::test_mode_timing {
namespace {

struct AssignmentToken {
    std::size_t begin{};
    std::size_t end{};
    int value{};
};

struct LineInspection {
    std::optional<AssignmentToken> token;
    bool malformed{};
};

bool IsHorizontalSpace(std::uint8_t value) noexcept {
    return value == ' ' || value == '\t';
}

bool StartsWith(
    std::span<const std::uint8_t> input,
    std::size_t position,
    std::size_t end,
    std::string_view text) noexcept {
    if (position > end || text.size() > end - position) {
        return false;
    }
    return std::equal(
        text.begin(), text.end(), input.begin() + position);
}

std::size_t FindBlockCommentEnd(
    std::span<const std::uint8_t> input,
    std::size_t position,
    std::size_t line_end) noexcept {
    while (position + 1 < line_end) {
        if (input[position] == '*' && input[position + 1] == '/') {
            return position;
        }
        ++position;
    }
    return line_end;
}

void ScanIgnoredLineComments(
    std::span<const std::uint8_t> input,
    std::size_t position,
    std::size_t line_end,
    bool& in_block_comment) noexcept {
    while (position < line_end) {
        if (in_block_comment) {
            const auto close = FindBlockCommentEnd(
                input, position, line_end);
            if (close == line_end) {
                return;
            }
            in_block_comment = false;
            position = close + 2;
            continue;
        }

        if (StartsWith(input, position, line_end, "//")) {
            return;
        }
        if (StartsWith(input, position, line_end, "/*")) {
            in_block_comment = true;
            position += 2;
            continue;
        }
        ++position;
    }
}

bool ValidateTrailingComment(
    std::span<const std::uint8_t> input,
    std::size_t position,
    std::size_t line_end,
    bool& in_block_comment) noexcept {
    while (position < line_end) {
        while (position < line_end && IsHorizontalSpace(input[position])) {
            ++position;
        }
        if (position == line_end) {
            return true;
        }
        if (!in_block_comment &&
            StartsWith(input, position, line_end, "//")) {
            return true;
        }
        if (!in_block_comment &&
            StartsWith(input, position, line_end, "/*")) {
            in_block_comment = true;
            position += 2;
        }
        if (!in_block_comment) {
            return false;
        }

        const auto close = FindBlockCommentEnd(input, position, line_end);
        if (close == line_end) {
            return true;
        }
        in_block_comment = false;
        position = close + 2;
    }
    return true;
}

std::optional<int> ParseSignedInt(
    std::span<const std::uint8_t> input,
    std::size_t digits_begin,
    std::size_t digits_end,
    bool negative) noexcept {
    const std::uint64_t limit = negative
        ? static_cast<std::uint64_t>(INT_MAX) + 1U
        : static_cast<std::uint64_t>(INT_MAX);
    std::uint64_t magnitude = 0;
    for (auto position = digits_begin; position < digits_end; ++position) {
        const auto digit = static_cast<std::uint64_t>(input[position] - '0');
        if (magnitude > (limit - digit) / 10U) {
            return std::nullopt;
        }
        magnitude = magnitude * 10U + digit;
    }

    if (!negative) {
        return static_cast<int>(magnitude);
    }
    if (magnitude == static_cast<std::uint64_t>(INT_MAX) + 1U) {
        return INT_MIN;
    }
    return -static_cast<int>(magnitude);
}

LineInspection InspectLine(
    std::span<const std::uint8_t> input,
    std::size_t line_begin,
    std::size_t line_end,
    std::string_view key,
    bool& in_block_comment) noexcept {
    auto position = line_begin;
    while (position < line_end) {
        if (in_block_comment) {
            const auto close = FindBlockCommentEnd(
                input, position, line_end);
            if (close == line_end) {
                return {};
            }
            in_block_comment = false;
            position = close + 2;
            continue;
        }

        while (position < line_end && IsHorizontalSpace(input[position])) {
            ++position;
        }
        if (position == line_end) {
            return {};
        }
        if (StartsWith(input, position, line_end, "//")) {
            return {};
        }
        if (StartsWith(input, position, line_end, "/*")) {
            in_block_comment = true;
            position += 2;
            continue;
        }

        if (!StartsWith(input, position, line_end, key)) {
            ScanIgnoredLineComments(
                input, position, line_end, in_block_comment);
            return {};
        }

        const auto after_key = position + key.size();
        if (after_key < line_end &&
            !IsHorizontalSpace(input[after_key]) &&
            input[after_key] != '=') {
            ScanIgnoredLineComments(
                input, after_key, line_end, in_block_comment);
            return {};
        }

        position = after_key;
        while (position < line_end && IsHorizontalSpace(input[position])) {
            ++position;
        }
        if (position == line_end || input[position] != '=') {
            return {.malformed = true};
        }
        ++position;
        while (position < line_end && IsHorizontalSpace(input[position])) {
            ++position;
        }

        const auto token_begin = position;
        bool negative = false;
        if (position < line_end &&
            (input[position] == '+' || input[position] == '-')) {
            negative = input[position] == '-';
            ++position;
        }
        const auto digits_begin = position;
        while (position < line_end &&
               input[position] >= '0' && input[position] <= '9') {
            ++position;
        }
        if (position == digits_begin) {
            return {.malformed = true};
        }

        const auto value = ParseSignedInt(
            input, digits_begin, position, negative);
        if (!value) {
            return {.malformed = true};
        }
        const auto token_end = position;
        if (!ValidateTrailingComment(
                input, position, line_end, in_block_comment)) {
            return {.malformed = true};
        }
        return {
            .token = AssignmentToken{token_begin, token_end, *value},
        };
    }
    return {};
}

std::expected<AssignmentToken, ConfigEditError> FindAssignmentToken(
    std::span<const std::uint8_t> input,
    std::string_view key_text,
    ConfigKey key) noexcept {
    std::optional<AssignmentToken> found;
    bool in_block_comment = false;
    std::size_t line_begin = 0;
    while (line_begin < input.size()) {
        auto line_end = line_begin;
        while (line_end < input.size() &&
               input[line_end] != '\r' && input[line_end] != '\n') {
            ++line_end;
        }

        auto content_begin = line_begin;
        if (line_begin == 0 && line_end >= 3 &&
            input[0] == 0xEF && input[1] == 0xBB && input[2] == 0xBF) {
            content_begin = 3;
        }

        const auto inspection = InspectLine(
            input, content_begin, line_end, key_text, in_block_comment);
        if (inspection.malformed) {
            return std::unexpected(ConfigEditError{
                ConfigEditStage::Malformed,
                key,
            });
        }
        if (inspection.token) {
            if (found) {
                return std::unexpected(ConfigEditError{
                    ConfigEditStage::Duplicate,
                    key,
                });
            }
            found = inspection.token;
        }

        line_begin = line_end;
        if (line_begin < input.size() && input[line_begin] == '\r') {
            ++line_begin;
        }
        if (line_begin < input.size() && input[line_begin] == '\n') {
            ++line_begin;
        }
    }

    if (!found) {
        return std::unexpected(ConfigEditError{
            ConfigEditStage::Missing,
            key,
        });
    }
    return *found;
}

} // namespace

std::expected<RewrittenConfig, ConfigEditError> RewriteTimingAssignments(
    std::span<const std::uint8_t> input,
    TimingOffsets offsets) {
    auto game = FindAssignmentToken(
        input, "GameTimeOffset", ConfigKey::GameTimeOffset);
    if (!game) {
        return std::unexpected(game.error());
    }
    auto judge = FindAssignmentToken(
        input, "JudgTimeOffset", ConfigKey::JudgTimeOffset);
    if (!judge) {
        return std::unexpected(judge.error());
    }

    RewrittenConfig result{{input.begin(), input.end()}, false};
    struct Replacement {
        AssignmentToken token;
        int value{};
    };
    std::array replacements{
        Replacement{*game, offsets.game_ms},
        Replacement{*judge, offsets.judge_ms},
    };
    std::sort(
        replacements.begin(), replacements.end(),
        [](const Replacement& left, const Replacement& right) {
            return left.token.begin > right.token.begin;
        });

    for (const auto& replacement : replacements) {
        if (replacement.token.value == replacement.value) {
            continue;
        }
        const auto text = std::to_string(replacement.value);
        const auto first = result.bytes.begin() + replacement.token.begin;
        const auto last = result.bytes.begin() + replacement.token.end;
        result.bytes.erase(first, last);
        result.bytes.insert(
            result.bytes.begin() + replacement.token.begin,
            text.begin(), text.end());
        result.changed = true;
    }
    return result;
}

} // namespace gc::test_mode_timing

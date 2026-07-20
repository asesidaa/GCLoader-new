#include "Patches/TestModeTiming/SystemConfigTimingStore.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using gc::test_mode_timing::ConfigEditError;
using gc::test_mode_timing::ConfigEditStage;
using gc::test_mode_timing::ConfigKey;
using gc::test_mode_timing::RewriteTimingAssignments;
using gc::test_mode_timing::TimingOffsets;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

std::vector<std::uint8_t> Bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

void Append(std::vector<std::uint8_t>& bytes, std::string_view text) {
    bytes.insert(bytes.end(), text.begin(), text.end());
}

bool Contains(
    std::span<const std::uint8_t> bytes,
    std::string_view text) {
    return std::search(
               bytes.begin(), bytes.end(),
               text.begin(), text.end()) != bytes.end();
}

bool ContainsBytes(
    std::span<const std::uint8_t> bytes,
    std::initializer_list<std::uint8_t> expected) {
    return std::search(
               bytes.begin(), bytes.end(),
               expected.begin(), expected.end()) != bytes.end();
}

bool HasError(
    std::string_view text,
    TimingOffsets offsets,
    ConfigEditError expected) {
    const auto result = RewriteTimingAssignments(Bytes(text), offsets);
    return !result && result.error() == expected;
}

} // namespace

int main() {
    int failures = 0;

    std::vector<std::uint8_t> input;
    Append(input, "/*\r\nGameTimeOffset = 99\r\n*/\r\n");
    Append(input, "//JudgTimeOffset=99\r\n");
    input.insert(input.end(), {0x83, 0x51, 0x83, 0x5B});
    Append(input, "\r\nJudgTimeOffset\t= -16\r\n");
    Append(input, "GameTimeOffset\t= 0\r\n");

    std::vector<std::uint8_t> expected;
    Append(expected, "/*\r\nGameTimeOffset = 99\r\n*/\r\n");
    Append(expected, "//JudgTimeOffset=99\r\n");
    expected.insert(expected.end(), {0x83, 0x51, 0x83, 0x5B});
    Append(expected, "\r\nJudgTimeOffset\t= 12\r\n");
    Append(expected, "GameTimeOffset\t= -5\r\n");

    const auto rewritten = RewriteTimingAssignments(
        input, {.game_ms = -5, .judge_ms = 12});
    failures += Expect(rewritten.has_value(), "valid assignments rewrite");
    if (rewritten) {
        failures += Expect(rewritten->changed, "different values report changed");
        failures += Expect(
            rewritten->bytes == expected,
            "only active numeric tokens change");
        failures += Expect(
            Contains(rewritten->bytes, "GameTimeOffset\t= -5\r\n"),
            "game token changes sign and width");
        failures += Expect(
            Contains(rewritten->bytes, "JudgTimeOffset\t= 12\r\n"),
            "judge token changes sign and width");
        failures += Expect(
            ContainsBytes(rewritten->bytes, {0x83, 0x51, 0x83, 0x5B}),
            "Shift-JIS bytes are retained");
        failures += Expect(
            Contains(rewritten->bytes, "GameTimeOffset = 99\r\n"),
            "block-comment assignment is ignored");
        failures += Expect(
            Contains(rewritten->bytes, "//JudgTimeOffset=99\r\n"),
            "line-comment assignment is ignored");
    }

    const auto unchanged = RewriteTimingAssignments(
        input, {.game_ms = 0, .judge_ms = -16});
    failures += Expect(
        unchanged && !unchanged->changed && unchanged->bytes == input,
        "unchanged rewrite is byte identical");

    const auto semantic_input = Bytes(
        "GameTimeOffset = +0\r\nJudgTimeOffset = -016\r\n");
    const auto semantic_unchanged = RewriteTimingAssignments(
        semantic_input, {.game_ms = 0, .judge_ms = -16});
    failures += Expect(
        semantic_unchanged && !semantic_unchanged->changed &&
            semantic_unchanged->bytes == semantic_input,
        "equivalent signed tokens remain byte identical");

    std::vector<std::uint8_t> bom_input{0xEF, 0xBB, 0xBF};
    Append(bom_input, "GameTimeOffset=0\r\nJudgTimeOffset=-16\r\n");
    std::vector<std::uint8_t> bom_expected{0xEF, 0xBB, 0xBF};
    Append(bom_expected, "GameTimeOffset=1\r\nJudgTimeOffset=-15\r\n");
    const auto bom_rewritten = RewriteTimingAssignments(
        bom_input, {.game_ms = 1, .judge_ms = -15});
    failures += Expect(
        bom_rewritten && bom_rewritten->changed &&
            bom_rewritten->bytes == bom_expected,
        "UTF-8 BOM is preserved before the first assignment");

    const auto comments_input = Bytes(
        "GameTimeOffsetBackup = 77\n"
        "  /* prefix */ GameTimeOffset = +4 /* keep */\n"
        "\tJudgTimeOffset\t=\t-3 // keep too\n"
        "JudgTimeOffsetSuffix = 88\n");
    const auto comments_unchanged = RewriteTimingAssignments(
        comments_input, {.game_ms = 4, .judge_ms = -3});
    failures += Expect(
        comments_unchanged && !comments_unchanged->changed &&
            comments_unchanged->bytes == comments_input,
        "LF comments whitespace and similar keys are preserved");

    failures += Expect(
        HasError(
            "JudgTimeOffset=0\n",
            {},
            {ConfigEditStage::Missing, ConfigKey::GameTimeOffset}),
        "missing game assignment is typed");
    failures += Expect(
        HasError(
            "GameTimeOffset=0\n",
            {},
            {ConfigEditStage::Missing, ConfigKey::JudgTimeOffset}),
        "missing judge assignment is typed");
    failures += Expect(
        HasError(
            "GameTimeOffset=0\nGameTimeOffset=1\nJudgTimeOffset=0\n",
            {},
            {ConfigEditStage::Duplicate, ConfigKey::GameTimeOffset}),
        "duplicate game assignment is typed");
    failures += Expect(
        HasError(
            "GameTimeOffset=0\nJudgTimeOffset=0\nJudgTimeOffset=1\n",
            {},
            {ConfigEditStage::Duplicate, ConfigKey::JudgTimeOffset}),
        "duplicate judge assignment is typed");
    failures += Expect(
        HasError(
            "GameTimeOffset=+\nJudgTimeOffset=0\n",
            {},
            {ConfigEditStage::Malformed, ConfigKey::GameTimeOffset}),
        "sign without digits is malformed");
    failures += Expect(
        HasError(
            "GameTimeOffset=12x\nJudgTimeOffset=0\n",
            {},
            {ConfigEditStage::Malformed, ConfigKey::GameTimeOffset}),
        "numeric suffix is malformed");
    failures += Expect(
        HasError(
            "GameTimeOffset 12\nJudgTimeOffset=0\n",
            {},
            {ConfigEditStage::Malformed, ConfigKey::GameTimeOffset}),
        "missing equals is malformed");
    failures += Expect(
        HasError(
            "GameTimeOffset=2147483648\nJudgTimeOffset=0\n",
            {},
            {ConfigEditStage::Malformed, ConfigKey::GameTimeOffset}),
        "positive integer overflow is malformed");
    failures += Expect(
        HasError(
            "GameTimeOffset=0\nJudgTimeOffset=-2147483649\n",
            {},
            {ConfigEditStage::Malformed, ConfigKey::JudgTimeOffset}),
        "negative integer overflow is malformed");

    return failures == 0 ? 0 : 1;
}

#include "Input/Switch/SwitchInputPatch.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

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

struct FakeStack {
    std::uint8_t native_match{0};
    std::int32_t target_direction{0};
    std::int32_t current_direction{0};
    bool fail_reads{false};
    bool fail_writes{false};
    int read_count{0};
    int write_count{0};
};

bool fake_read(
    void* context,
    std::ptrdiff_t offset,
    void* output,
    std::size_t size) noexcept {
    auto& stack = *static_cast<FakeStack*>(context);
    ++stack.read_count;
    if (stack.fail_reads || output == nullptr) {
        return false;
    }

    if (offset == gc::switch_input::kDiagonalNativeMatchOffset &&
        size == sizeof(stack.native_match)) {
        std::memcpy(output, &stack.native_match, size);
        return true;
    }
    if (offset == gc::switch_input::kDiagonalTargetDirectionOffset &&
        size == sizeof(stack.target_direction)) {
        std::memcpy(output, &stack.target_direction, size);
        return true;
    }
    if (offset == gc::switch_input::kDiagonalCurrentDirectionOffset &&
        size == sizeof(stack.current_direction)) {
        std::memcpy(output, &stack.current_direction, size);
        return true;
    }
    return false;
}

bool fake_write(
    void* context,
    std::ptrdiff_t offset,
    const void* input,
    std::size_t size) noexcept {
    auto& stack = *static_cast<FakeStack*>(context);
    ++stack.write_count;
    if (stack.fail_writes || input == nullptr) {
        return false;
    }

    if (offset == gc::switch_input::kDiagonalNativeMatchOffset &&
        size == sizeof(stack.native_match)) {
        std::memcpy(&stack.native_match, input, size);
        return true;
    }
    return false;
}

gc::switch_input::StackAccessor accessor(FakeStack& stack) {
    return {&stack, fake_read, fake_write};
}

} // namespace

int main() {
    using namespace gc::switch_input;
    int failures = 0;

    auto pressed = kGameplayQueryEntrySignature;
    auto held = kGameplayQueryEntrySignature;
    auto diagonal = kDiagonalMatchSignature;
    SwitchHookSite mismatch = SwitchHookSite::None;
    failures += expect_true(
        ValidateSwitchInputSignatures(
            {pressed, held, diagonal},
            &mismatch),
        "all signatures");
    failures += expect_int(
        static_cast<int>(mismatch),
        static_cast<int>(SwitchHookSite::None),
        "no signature mismatch site");

    auto bad_pressed = pressed;
    bad_pressed[0] ^= 0xFF;
    failures += expect_false(
        ValidateSwitchInputSignatures(
            {bad_pressed, held, diagonal},
            &mismatch),
        "pressed signature mismatch");
    failures += expect_int(
        static_cast<int>(mismatch),
        static_cast<int>(SwitchHookSite::PressedEdge),
        "pressed mismatch site");

    auto bad_held = held;
    bad_held[5] ^= 0xFF;
    failures += expect_false(
        ValidateSwitchInputSignatures(
            {pressed, bad_held, diagonal},
            &mismatch),
        "held signature mismatch");
    failures += expect_int(
        static_cast<int>(mismatch),
        static_cast<int>(SwitchHookSite::HeldState),
        "held mismatch site");

    auto bad_diagonal = diagonal;
    bad_diagonal[8] ^= 0xFF;
    failures += expect_false(
        ValidateSwitchInputSignatures(
            {pressed, held, bad_diagonal},
            &mismatch),
        "diagonal signature mismatch");
    failures += expect_int(
        static_cast<int>(mismatch),
        static_cast<int>(SwitchHookSite::DiagonalMatch),
        "diagonal mismatch site");

    failures += expect_true(
        ResolveSwitchPatchState({true, true, true}) ==
            SwitchPatchState::Switch,
        "complete hook set activates Switch");
    failures += expect_true(
        ResolveSwitchPatchState({false, true, true}) ==
            SwitchPatchState::Arcade,
        "pressed creation failure rolls back");
    failures += expect_true(
        ResolveSwitchPatchState({true, false, true}) ==
            SwitchPatchState::Arcade,
        "held creation failure rolls back");
    failures += expect_true(
        ResolveSwitchPatchState({true, true, false}) ==
            SwitchPatchState::Arcade,
        "diagonal creation failure rolls back");

    FakeStack native_success{1, 1, 2};
    failures += expect_false(
        TryApplySwitchDiagonalMatch(accessor(native_success)),
        "native success unchanged");
    failures += expect_int(native_success.native_match, 1, "native success value");
    failures += expect_int(native_success.write_count, 0, "native success writes");

    FakeStack promoted{0, 1, 2};
    failures += expect_true(
        TryApplySwitchDiagonalMatch(accessor(promoted)),
        "adjacent cardinal promoted");
    failures += expect_int(promoted.native_match, 1, "promoted local value");
    failures += expect_int(promoted.write_count, 1, "promoted write count");

    FakeStack unrelated{0, 1, 6};
    failures += expect_false(
        TryApplySwitchDiagonalMatch(accessor(unrelated)),
        "unrelated cardinal unchanged");
    failures += expect_int(unrelated.native_match, 0, "unrelated local value");
    failures += expect_int(unrelated.write_count, 0, "unrelated write count");

    FakeStack invalid_read{0, 1, 2};
    invalid_read.fail_reads = true;
    failures += expect_false(
        TryApplySwitchDiagonalMatch(accessor(invalid_read)),
        "invalid local read");
    failures += expect_int(invalid_read.native_match, 0, "invalid-read local value");
    failures += expect_int(invalid_read.write_count, 0, "invalid-read writes");

    FakeStack invalid_write{0, 9, 8};
    invalid_write.fail_writes = true;
    failures += expect_false(
        TryApplySwitchDiagonalMatch(accessor(invalid_write)),
        "invalid local write");
    failures += expect_int(invalid_write.native_match, 0, "invalid-write local value");
    failures += expect_int(invalid_write.write_count, 1, "invalid-write attempts");

    return failures == 0 ? 0 : 1;
}

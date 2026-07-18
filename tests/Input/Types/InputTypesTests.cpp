#include "Input/Types/DigitalLatch.h"
#include "Input/Types/InputTypes.h"
#include "Input/Types/PhysicalKey.h"

#include <expected>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using gc::input::PhysicalKey;
using gc::input::ScanCodePrefix;

int expect_key(
    const std::expected<PhysicalKey, std::string>& actual,
    PhysicalKey expected,
    std::string_view name)
{
    if (actual && actual.value() == expected)
    {
        return 0;
    }

    std::cerr << name << ": expected key " << expected.make_code
              << ", got ";
    if (actual)
    {
        std::cerr << actual->make_code;
    }
    else
    {
        std::cerr << "error: " << actual.error();
    }
    std::cerr << '\n';
    return 1;
}

int expect_string(
    std::string_view actual,
    std::string_view expected,
    std::string_view name)
{
    if (actual == expected)
    {
        return 0;
    }

    std::cerr << name << ": expected '" << expected << "', got '"
              << actual << "'\n";
    return 1;
}

int expect_parse_failure(std::string_view token)
{
    if (!gc::input::ParsePhysicalKey(token))
    {
        return 0;
    }

    std::cerr << "expected parse failure for '" << token << "'\n";
    return 1;
}

int expect_bool(bool actual, bool expected, std::string_view name)
{
    if (actual == expected)
    {
        return 0;
    }

    std::cerr << name << ": expected " << expected << ", got "
              << actual << '\n';
    return 1;
}

} // namespace

int main()
{
    using namespace gc::input;

    int failures = 0;

    failures += expect_key(
        ParsePhysicalKey("sc:0014"),
        {0x0014, ScanCodePrefix::None},
        "ordinary scan code");
    failures += expect_key(
        ParsePhysicalKey("e0:0048"),
        {0x0048, ScanCodePrefix::E0},
        "E0 scan code");
    failures += expect_key(
        ParsePhysicalKey("e1:0045"),
        {0x0045, ScanCodePrefix::E1},
        "E1 scan code");
    failures += expect_key(
        ParsePhysicalKey("sc:00AF"),
        {0x00af, ScanCodePrefix::None},
        "uppercase hex digits");

    failures += expect_string(
        FormatPhysicalKey({0x0014, ScanCodePrefix::None}),
        "sc:0014",
        "ordinary canonical token");
    failures += expect_string(
        FormatPhysicalKey({0x0048, ScanCodePrefix::E0}),
        "e0:0048",
        "E0 canonical token");
    failures += expect_string(
        FormatPhysicalKey({0x0045, ScanCodePrefix::E1}),
        "e1:0045",
        "E1 canonical token");
    failures += expect_string(
        FormatPhysicalKey({0x00af, ScanCodePrefix::None}),
        "sc:00af",
        "lowercase canonical hex");

    failures += expect_parse_failure("");
    failures += expect_parse_failure("t");
    failures += expect_parse_failure("sc:014");
    failures += expect_parse_failure("SC:0014");
    failures += expect_parse_failure("e2:0014");
    failures += expect_parse_failure("sc:0000");
    failures += expect_parse_failure("sc:zzzz");
    failures += expect_parse_failure("sc:10000");

    const auto latch_result = DigitalLatch::Create(50, 40);
    if (!latch_result)
    {
        std::cerr << "valid latch rejected: " << latch_result.error() << '\n';
        return 1;
    }

    auto latch = latch_result.value();
    failures += expect_bool(latch.Update(0.10), false, "inactive noise");
    failures += expect_bool(latch.Update(0.50), true, "press boundary");
    failures += expect_bool(latch.Update(0.40), true, "release boundary held");
    failures += expect_bool(latch.Update(0.399), false, "below release");
    failures += expect_bool(latch.Update(0.49), false, "hysteresis gap");
    failures += expect_bool(latch.Update(0.50), true, "re-press boundary");
    failures += expect_bool(latch.Update(2.0), true, "activation clamps high");
    failures += expect_bool(latch.Update(-1.0), false, "activation clamps low");
    latch.Reset();
    failures += expect_bool(latch.pressed(), false, "reset clears latch");

    failures += expect_bool(
        DigitalLatch::Create(101, 40).has_value(),
        false,
        "press above range");
    failures += expect_bool(
        DigitalLatch::Create(50, 101).has_value(),
        false,
        "release above range");
    failures += expect_bool(
        DigitalLatch::Create(50, 50).has_value(),
        false,
        "equal thresholds");
    failures += expect_bool(
        DigitalLatch::Create(40, 50).has_value(),
        false,
        "release above press");
    failures += expect_bool(
        DigitalLatch::Create(1, 0).has_value(),
        true,
        "minimum valid gap");

    static_assert(IsGameplayAction(LogicalAction::LeftBoosterUp));
    static_assert(IsGameplayAction(LogicalAction::RightBoosterButton));
    static_assert(!IsGameplayAction(LogicalAction::Service1));

    if (failures != 0)
    {
        return 1;
    }

    std::cout << "InputTypesTests passed\n";
    return 0;
}

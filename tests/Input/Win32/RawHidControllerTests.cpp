#include "Input/Win32/HidApi.h"
#include "Input/Win32/RawHidController.h"

#include <Windows.h>
#include <hidpi.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr ULONG kReportSize = 18;

struct FakeHidFixture {
    HIDP_CAPS caps{};
    std::vector<HIDP_BUTTON_CAPS> button_caps;
    std::vector<HIDP_VALUE_CAPS> value_caps;
    bool fail_usages{};
    bool fail_values{};
};

FakeHidFixture* fixture{};

HIDP_BUTTON_CAPS button_range(
    UCHAR report_id,
    USAGE first,
    USAGE last,
    USHORT link_collection = 1)
{
    HIDP_BUTTON_CAPS cap{};
    cap.UsagePage = 0x09;
    cap.ReportID = report_id;
    cap.LinkCollection = link_collection;
    cap.IsRange = TRUE;
    cap.Range.UsageMin = first;
    cap.Range.UsageMax = last;
    return cap;
}

HIDP_VALUE_CAPS value_cap(
    UCHAR report_id,
    USAGE usage,
    LONG logical_min,
    LONG logical_max,
    USHORT bit_size,
    bool has_null = false,
    USHORT link_collection = 1)
{
    HIDP_VALUE_CAPS cap{};
    cap.UsagePage = 0x01;
    cap.ReportID = report_id;
    cap.LinkCollection = link_collection;
    cap.IsRange = FALSE;
    cap.HasNull = has_null ? TRUE : FALSE;
    cap.BitSize = bit_size;
    cap.ReportCount = 1;
    cap.LogicalMin = logical_min;
    cap.LogicalMax = logical_max;
    cap.NotRange.Usage = usage;
    return cap;
}

void finish_fixture(FakeHidFixture& value)
{
    value.caps.UsagePage = 0x01;
    value.caps.Usage = 0x05;
    value.caps.InputReportByteLength = kReportSize;
    value.caps.NumberInputButtonCaps =
        static_cast<USHORT>(value.button_caps.size());
    value.caps.NumberInputValueCaps =
        static_cast<USHORT>(value.value_caps.size());
}

UINT WINAPI fake_get_raw_input_device_info(
    HANDLE,
    UINT command,
    LPVOID data,
    PUINT size)
{
    if (command != RIDI_PREPARSEDDATA)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return UINT_MAX;
    }
    constexpr UINT preparsed_size = 8;
    if (data == nullptr)
    {
        *size = preparsed_size;
        return 0;
    }
    if (*size < preparsed_size)
    {
        *size = preparsed_size;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return UINT_MAX;
    }
    std::memset(data, 0xa5, preparsed_size);
    *size = preparsed_size;
    return preparsed_size;
}

NTSTATUS __stdcall fake_get_caps(
    PHIDP_PREPARSED_DATA,
    PHIDP_CAPS caps)
{
    *caps = fixture->caps;
    return HIDP_STATUS_SUCCESS;
}

NTSTATUS __stdcall fake_get_button_caps(
    HIDP_REPORT_TYPE report_type,
    PHIDP_BUTTON_CAPS caps,
    PUSHORT count,
    PHIDP_PREPARSED_DATA)
{
    if (report_type != HidP_Input || *count < fixture->button_caps.size())
    {
        return HIDP_STATUS_BUFFER_TOO_SMALL;
    }
    std::ranges::copy(fixture->button_caps, caps);
    *count = static_cast<USHORT>(fixture->button_caps.size());
    return HIDP_STATUS_SUCCESS;
}

NTSTATUS __stdcall fake_get_value_caps(
    HIDP_REPORT_TYPE report_type,
    PHIDP_VALUE_CAPS caps,
    PUSHORT count,
    PHIDP_PREPARSED_DATA)
{
    if (report_type != HidP_Input || *count < fixture->value_caps.size())
    {
        return HIDP_STATUS_BUFFER_TOO_SMALL;
    }
    std::ranges::copy(fixture->value_caps, caps);
    *count = static_cast<USHORT>(fixture->value_caps.size());
    return HIDP_STATUS_SUCCESS;
}

bool report_id_matches(UCHAR cap_report_id, const char* report)
{
    return cap_report_id == 0 ||
        cap_report_id == static_cast<UCHAR>(report[0]);
}

NTSTATUS __stdcall fake_get_usages(
    HIDP_REPORT_TYPE report_type,
    USAGE usage_page,
    USHORT link_collection,
    PUSAGE usage_list,
    PULONG usage_count,
    PHIDP_PREPARSED_DATA,
    PCHAR report,
    ULONG report_length)
{
    if (fixture->fail_usages)
    {
        return HIDP_STATUS_INVALID_REPORT_LENGTH;
    }
    if (report_type != HidP_Input || report_length != kReportSize)
    {
        return HIDP_STATUS_INVALID_REPORT_LENGTH;
    }

    bool matching_address = false;
    bool matching_report = false;
    std::vector<USAGE> pressed;
    for (const auto& cap : fixture->button_caps)
    {
        if (cap.UsagePage != usage_page ||
            cap.LinkCollection != link_collection)
        {
            continue;
        }
        matching_address = true;
        if (!report_id_matches(cap.ReportID, report))
        {
            continue;
        }
        matching_report = true;
        const USAGE first = cap.IsRange
            ? cap.Range.UsageMin
            : cap.NotRange.Usage;
        const USAGE last = cap.IsRange
            ? cap.Range.UsageMax
            : cap.NotRange.Usage;
        for (USAGE usage = first; usage <= last; ++usage)
        {
            if ((static_cast<unsigned char>(report[1]) &
                 (1u << (usage - 1))) != 0)
            {
                pressed.push_back(usage);
            }
        }
    }
    if (matching_address && !matching_report)
    {
        return HIDP_STATUS_INCOMPATIBLE_REPORT_ID;
    }
    if (!matching_address)
    {
        return HIDP_STATUS_USAGE_NOT_FOUND;
    }
    if (*usage_count < pressed.size())
    {
        return HIDP_STATUS_BUFFER_TOO_SMALL;
    }
    std::ranges::copy(pressed, usage_list);
    *usage_count = static_cast<ULONG>(pressed.size());
    return HIDP_STATUS_SUCCESS;
}

std::size_t value_offset(USAGE usage)
{
    switch (usage)
    {
    case 0x30:
        return 2;
    case 0x31:
        return 6;
    case 0x32:
        return 10;
    case 0x39:
        return 14;
    default:
        return kReportSize;
    }
}

NTSTATUS __stdcall fake_get_usage_value(
    HIDP_REPORT_TYPE report_type,
    USAGE usage_page,
    USHORT link_collection,
    USAGE usage,
    PULONG raw_value,
    PHIDP_PREPARSED_DATA,
    PCHAR report,
    ULONG report_length)
{
    if (fixture->fail_values)
    {
        return HIDP_STATUS_INVALID_REPORT_LENGTH;
    }
    if (report_type != HidP_Input || report_length != kReportSize)
    {
        return HIDP_STATUS_INVALID_REPORT_LENGTH;
    }

    bool matching_address = false;
    for (const auto& cap : fixture->value_caps)
    {
        const USAGE first = cap.IsRange
            ? cap.Range.UsageMin
            : cap.NotRange.Usage;
        const USAGE last = cap.IsRange
            ? cap.Range.UsageMax
            : cap.NotRange.Usage;
        if (cap.UsagePage != usage_page ||
            cap.LinkCollection != link_collection ||
            usage < first || usage > last)
        {
            continue;
        }
        matching_address = true;
        if (!report_id_matches(cap.ReportID, report))
        {
            return HIDP_STATUS_INCOMPATIBLE_REPORT_ID;
        }
        const auto offset = value_offset(usage);
        if (offset + sizeof(ULONG) > report_length)
        {
            return HIDP_STATUS_USAGE_NOT_FOUND;
        }
        std::memcpy(raw_value, report + offset, sizeof(ULONG));
        return HIDP_STATUS_SUCCESS;
    }
    return matching_address
        ? HIDP_STATUS_INCOMPATIBLE_REPORT_ID
        : HIDP_STATUS_USAGE_NOT_FOUND;
}

gc::input::HidApi fake_hid_api()
{
    return {
        .get_raw_input_device_info = fake_get_raw_input_device_info,
        .get_caps = fake_get_caps,
        .get_button_caps = fake_get_button_caps,
        .get_value_caps = fake_get_value_caps,
        .get_usages = fake_get_usages,
        .get_usage_value = fake_get_usage_value,
    };
}

using Report = std::array<unsigned char, kReportSize>;

void write_value(Report& report, std::size_t offset, std::uint32_t value)
{
    std::memcpy(report.data() + offset, &value, sizeof(value));
}

Report make_report(
    UCHAR report_id = 0,
    UCHAR buttons = 0,
    std::uint32_t axis = 0,
    std::uint32_t trigger = 0,
    std::uint32_t centered = 500,
    std::uint32_t hat = 0)
{
    Report report{};
    report[0] = report_id;
    report[1] = buttons;
    write_value(report, 2, axis);
    write_value(report, 6, trigger);
    write_value(report, 10, centered);
    write_value(report, 14, hat);
    return report;
}

std::vector<std::byte> make_packet(std::span<const Report> reports)
{
    const std::size_t header_size = offsetof(RAWHID, bRawData);
    std::vector<std::byte> bytes(
        header_size + reports.size() * kReportSize);
    auto* hid = reinterpret_cast<RAWHID*>(bytes.data());
    hid->dwSizeHid = kReportSize;
    hid->dwCount = static_cast<DWORD>(reports.size());
    auto* destination = reinterpret_cast<unsigned char*>(hid->bRawData);
    for (const auto& report : reports)
    {
        std::ranges::copy(report, destination);
        destination += report.size();
    }
    return bytes;
}

std::vector<std::byte> make_packet(const Report& report)
{
    return make_packet(std::span<const Report>(&report, 1));
}

const RAWHID& packet_hid(const std::vector<std::byte>& packet)
{
    return *reinterpret_cast<const RAWHID*>(packet.data());
}

gc::input::RawHidDeviceInfo test_device(HANDLE handle)
{
    return {
        .raw_device = handle,
        .device_path = R"(\\?\HID#VID_1234&PID_5678#TEST)",
        .product_name = L"Fake HID Controller",
        .vendor_id = 0x1234,
        .product_id = 0x5678,
        .usage_page = 0x01,
        .usage = 0x05,
    };
}

std::optional<gc::input::DigitalControlBinding> find_binding(
    const gc::input::ControllerStateView& controller,
    gc::input::DigitalControlType type,
    std::uint32_t usage,
    std::optional<gc::input::ControlDirection> direction = std::nullopt)
{
    for (const auto& descriptor : controller.controls())
    {
        const auto& binding = descriptor.binding;
        if (binding.type == type && binding.usage == usage &&
            (!direction || binding.direction == direction))
        {
            return binding;
        }
    }
    return std::nullopt;
}

double activation(
    const gc::input::ControllerStateView& controller,
    const gc::input::DigitalControlBinding& binding)
{
    return controller.Activation(binding).value_or(-1.0);
}

int expect_true(bool actual, std::string_view name)
{
    if (actual)
    {
        return 0;
    }
    std::cerr << name << ": expected true\n";
    return 1;
}

int expect_near(double actual, double expected, std::string_view name)
{
    if (std::abs(actual - expected) < 0.00001)
    {
        return 0;
    }
    std::cerr << name << ": expected " << expected
              << ", got " << actual << '\n';
    return 1;
}

} // namespace

int main()
{
    using namespace gc::input;

    int failures = 0;
    const HANDLE selected = reinterpret_cast<HANDLE>(0x44);

    {
        FakeHidFixture buttons;
        buttons.button_caps.push_back(button_range(0, 1, 2));
        finish_fixture(buttons);
        fixture = &buttons;
        auto opened = RawHidController::Open(
            test_device(selected), fake_hid_api());
        failures += expect_true(opened.has_value(), "button controller opens");
        if (!opened)
        {
            return 1;
        }
        auto controller = std::move(*opened);
        const auto first = find_binding(
            controller, DigitalControlType::RawHidButton, 1);
        const auto second = find_binding(
            controller, DigitalControlType::RawHidButton, 2);
        failures += expect_true(first && second, "button range expands");
        const auto* stable_controls = controller.controls().data();

        const std::array both_reports{make_report(0, 0x03)};
        auto both_packet = make_packet(both_reports);
        const auto both_changed = controller.Apply(
            selected, packet_hid(both_packet));
        failures += expect_true(
            both_changed && *both_changed,
            "two buttons update independently");
        failures += expect_near(activation(controller, *first), 1.0, "button 1 on");
        failures += expect_near(activation(controller, *second), 1.0, "button 2 on");

        const std::array first_only_reports{make_report(0, 0x01)};
        auto first_only_packet = make_packet(first_only_reports);
        failures += expect_true(
            controller.Apply(selected, packet_hid(first_only_packet)).value_or(false),
            "missing button clears");
        failures += expect_near(activation(controller, *first), 1.0, "button 1 remains");
        failures += expect_near(activation(controller, *second), 0.0, "button 2 clears");
        failures += expect_true(
            controller.controls().data() == stable_controls,
            "descriptors remain stable while packets apply");

        const std::array ignored_reports{make_report(0, 0x02)};
        auto ignored_packet = make_packet(ignored_reports);
        failures += expect_true(
            !controller.Apply(
                reinterpret_cast<HANDLE>(0x55),
                packet_hid(ignored_packet)).value_or(true),
            "unselected source ignored");
        failures += expect_near(
            activation(controller, *first), 1.0, "ignored source preserves state");

        controller.Clear();
        failures += expect_near(
            activation(controller, *first), 0.0, "device removal clears button 1");
        failures += expect_near(
            activation(controller, *second), 0.0, "device removal clears button 2");

        auto unknown = *first;
        unknown.usage = 99;
        failures += expect_true(
            !controller.ValidateBinding(unknown),
            "unknown HID address rejected");
        failures += expect_true(
            controller.ValidateBinding(*first).has_value(),
            "valid HID address remains available");
    }

    {
        FakeHidFixture values;
        values.value_caps = {
            value_cap(0, 0x30, -32768, 32767, 16),
            value_cap(0, 0x31, 0, 255, 8),
            value_cap(0, 0x32, 100, 900, 16),
        };
        finish_fixture(values);
        fixture = &values;
        auto opened = RawHidController::Open(
            test_device(selected), fake_hid_api());
        failures += expect_true(opened.has_value(), "value controller opens");
        if (!opened)
        {
            return 1;
        }
        auto controller = std::move(*opened);
        auto axis = *find_binding(
            controller, DigitalControlType::RawHidValue, 0x30);
        auto trigger = *find_binding(
            controller, DigitalControlType::RawHidValue, 0x31);
        auto centered = *find_binding(
            controller, DigitalControlType::RawHidValue, 0x32);
        axis.neutral_value = 0;
        trigger.neutral_value = 0;
        centered.neutral_value = 500;

        axis.direction = ControlDirection::Negative;
        const std::array negative_reports{
            make_report(0, 0, 0x00008000u, 0, 500)};
        auto negative_packet = make_packet(negative_reports);
        failures += expect_true(
            controller.Apply(selected, packet_hid(negative_packet)).has_value(),
            "signed negative report parses");
        failures += expect_near(
            activation(controller, axis), 1.0, "signed negative normalizes");
        failures += expect_true(
            controller.RawValue(axis) == -32768,
            "signed value is sign extended");

        axis.direction = ControlDirection::Positive;
        const std::array positive_reports{
            make_report(0, 0, 32767, 255, 900)};
        auto positive_packet = make_packet(positive_reports);
        (void)controller.Apply(selected, packet_hid(positive_packet));
        failures += expect_near(
            activation(controller, axis), 1.0, "signed positive normalizes");

        trigger.direction = ControlDirection::Positive;
        failures += expect_near(
            activation(controller, trigger), 1.0, "unsigned trigger full press");
        const std::array neutral_reports{make_report(0, 0, 0, 0, 500)};
        auto neutral_packet = make_packet(neutral_reports);
        (void)controller.Apply(selected, packet_hid(neutral_packet));
        failures += expect_near(
            activation(controller, trigger), 0.0, "unsigned trigger neutral");

        centered.direction = ControlDirection::Positive;
        const std::array centered_positive_reports{
            make_report(0, 0, 0, 0, 900)};
        auto centered_positive_packet = make_packet(centered_positive_reports);
        (void)controller.Apply(selected, packet_hid(centered_positive_packet));
        failures += expect_near(
            activation(controller, centered), 1.0, "unusual positive range");
        centered.direction = ControlDirection::Negative;
        const std::array centered_negative_reports{
            make_report(0, 0, 0, 0, 100)};
        auto centered_negative_packet = make_packet(centered_negative_reports);
        (void)controller.Apply(selected, packet_hid(centered_negative_packet));
        failures += expect_near(
            activation(controller, centered), 1.0, "unusual negative range");

        auto invalid_neutral = centered;
        invalid_neutral.neutral_value = 901;
        failures += expect_true(
            !controller.ValidateBinding(invalid_neutral),
            "neutral outside logical range rejected");
        failures += expect_true(
            controller.ValidateBinding(centered).has_value(),
            "valid centered binding accepted");
    }

    {
        FakeHidFixture four_way_hat;
        four_way_hat.value_caps = {
            value_cap(0, 0x39, 0, 3, 4, true),
        };
        finish_fixture(four_way_hat);
        fixture = &four_way_hat;
        auto opened = RawHidController::Open(
            test_device(selected), fake_hid_api());
        if (!opened)
        {
            std::cerr << opened.error() << '\n';
            return 1;
        }
        auto controller = std::move(*opened);
        const auto up = *find_binding(
            controller,
            DigitalControlType::RawHidHat,
            0x39,
            ControlDirection::Up);
        const auto right = *find_binding(
            controller,
            DigitalControlType::RawHidHat,
            0x39,
            ControlDirection::Right);
        const auto down = *find_binding(
            controller,
            DigitalControlType::RawHidHat,
            0x39,
            ControlDirection::Down);
        const auto left = *find_binding(
            controller,
            DigitalControlType::RawHidHat,
            0x39,
            ControlDirection::Left);
        const std::array directions{up, right, down, left};
        for (std::uint32_t value = 0; value < directions.size(); ++value)
        {
            const std::array reports{make_report(0, 0, 0, 0, 500, value)};
            auto packet = make_packet(reports);
            (void)controller.Apply(selected, packet_hid(packet));
            for (std::size_t index = 0; index < directions.size(); ++index)
            {
                failures += expect_near(
                    activation(controller, directions[index]),
                    index == value ? 1.0 : 0.0,
                    "four-way hat cardinal");
            }
        }
        const std::array null_reports{make_report(0, 0, 0, 0, 500, 15)};
        auto null_packet = make_packet(null_reports);
        (void)controller.Apply(selected, packet_hid(null_packet));
        for (const auto& direction : directions)
        {
            failures += expect_near(
                activation(controller, direction), 0.0, "hat null clears");
        }
    }

    {
        FakeHidFixture eight_way_hat;
        eight_way_hat.value_caps = {
            value_cap(0, 0x39, 0, 7, 4, true),
        };
        finish_fixture(eight_way_hat);
        fixture = &eight_way_hat;
        auto controller = std::move(*RawHidController::Open(
            test_device(selected), fake_hid_api()));
        const auto up = *find_binding(
            controller,
            DigitalControlType::RawHidHat,
            0x39,
            ControlDirection::Up);
        const auto right = *find_binding(
            controller,
            DigitalControlType::RawHidHat,
            0x39,
            ControlDirection::Right);
        const std::array diagonal_reports{
            make_report(0, 0, 0, 0, 500, 1)};
        auto diagonal_packet = make_packet(diagonal_reports);
        (void)controller.Apply(selected, packet_hid(diagonal_packet));
        failures += expect_true(
            activation(controller, up) > 0.0 &&
                activation(controller, right) > 0.0,
            "diagonal hat activates both cardinals");
    }

    {
        FakeHidFixture report_ids;
        report_ids.button_caps = {
            button_range(1, 1, 1),
            button_range(2, 2, 2),
        };
        finish_fixture(report_ids);
        fixture = &report_ids;
        auto controller = std::move(*RawHidController::Open(
            test_device(selected), fake_hid_api()));
        const auto first = *find_binding(
            controller, DigitalControlType::RawHidButton, 1);
        const auto second = *find_binding(
            controller, DigitalControlType::RawHidButton, 2);

        const std::array id_one_reports{make_report(1, 0x01)};
        auto id_one_packet = make_packet(id_one_reports);
        (void)controller.Apply(selected, packet_hid(id_one_packet));
        const std::array id_two_reports{make_report(2, 0x02)};
        auto id_two_packet = make_packet(id_two_reports);
        (void)controller.Apply(selected, packet_hid(id_two_packet));
        failures += expect_near(
            activation(controller, first), 1.0, "report 2 preserves report 1");
        failures += expect_near(
            activation(controller, second), 1.0, "report 2 updates its control");

        controller.Clear();
        (void)controller.Apply(selected, packet_hid(id_two_packet));
        const std::array batch_reports{
            make_report(2, 0x00),
            make_report(2, 0x02),
            make_report(2, 0x02),
        };
        auto batch_packet = make_packet(batch_reports);
        const auto batch_changed = controller.Apply(
            selected, packet_hid(batch_packet));
        failures += expect_true(
            batch_changed && *batch_changed,
            "all three reports apply in order");
        failures += expect_near(
            activation(controller, second), 1.0, "batch final state retained");

        const std::array pressed_reports{make_report(1, 0x01)};
        auto pressed_packet = make_packet(pressed_reports);
        (void)controller.Apply(selected, packet_hid(pressed_packet));
        auto malformed = make_packet(pressed_reports);
        reinterpret_cast<RAWHID*>(malformed.data())->dwSizeHid =
            kReportSize - 1;
        failures += expect_true(
            !controller.Apply(selected, packet_hid(malformed)),
            "malformed report rejected");
        failures += expect_near(
            activation(controller, first), 0.0, "malformed report clears state");

        (void)controller.Apply(selected, packet_hid(pressed_packet));
        report_ids.fail_usages = true;
        failures += expect_true(
            !controller.Apply(selected, packet_hid(pressed_packet)),
            "HidP failure returned");
        failures += expect_near(
            activation(controller, first), 0.0, "HidP failure clears state");
    }

    if (failures != 0)
    {
        return 1;
    }

    std::cout << "RawHidControllerTests passed\n";
    return 0;
}

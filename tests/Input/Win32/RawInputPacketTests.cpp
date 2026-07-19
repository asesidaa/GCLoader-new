#include "Input/Win32/RawInputPacket.h"

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

struct FakeRawInputState {
    std::vector<std::byte> packet;
    UINT first_query_size{};
    bool short_read{};
    UINT observed_command{};
    UINT observed_header_size{};
    int query_count{};
    int read_count{};
};

FakeRawInputState* fake_state{};

UINT WINAPI fake_get_raw_input_data(
    HRAWINPUT,
    UINT command,
    LPVOID data,
    PUINT size,
    UINT header_size)
{
    fake_state->observed_command = command;
    fake_state->observed_header_size = header_size;
    if (data == nullptr)
    {
        ++fake_state->query_count;
        *size = fake_state->first_query_size != 0
            ? fake_state->first_query_size
            : static_cast<UINT>(fake_state->packet.size());
        return 0;
    }

    ++fake_state->read_count;
    const UINT packet_size = static_cast<UINT>(fake_state->packet.size());
    if (*size < packet_size)
    {
        *size = packet_size;
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return UINT_MAX;
    }

    std::memcpy(data, fake_state->packet.data(), packet_size);
    if (fake_state->short_read)
    {
        *size = packet_size - 1;
        return packet_size - 1;
    }
    *size = packet_size;
    return packet_size;
}

std::vector<std::byte> keyboard_packet()
{
    std::vector<std::byte> bytes(sizeof(RAWINPUT));
    auto* input = reinterpret_cast<RAWINPUT*>(bytes.data());
    input->header.dwType = RIM_TYPEKEYBOARD;
    input->header.dwSize = static_cast<DWORD>(bytes.size());
    input->data.keyboard.MakeCode = 0x14;
    return bytes;
}

std::vector<std::byte> hid_packet(
    DWORD report_size,
    DWORD report_count,
    std::initializer_list<unsigned char> data)
{
    const std::size_t data_offset =
        sizeof(RAWINPUTHEADER) + offsetof(RAWHID, bRawData);
    std::vector<std::byte> bytes(data_offset + data.size());
    auto* input = reinterpret_cast<RAWINPUT*>(bytes.data());
    input->header.dwType = RIM_TYPEHID;
    input->header.dwSize = static_cast<DWORD>(bytes.size());
    input->data.hid.dwSizeHid = report_size;
    input->data.hid.dwCount = report_count;
    std::ranges::copy(
        data,
        reinterpret_cast<unsigned char*>(input->data.hid.bRawData));
    return bytes;
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

gc::input::RawInputPacketBuffer make_reader()
{
    return gc::input::RawInputPacketBuffer(
        gc::input::RawInputApi{.get_raw_input_data = fake_get_raw_input_data});
}

} // namespace

int main()
{
    using namespace gc::input;

    int failures = 0;

    FakeRawInputState valid{.packet = keyboard_packet()};
    fake_state = &valid;
    auto reader = make_reader();
    const auto valid_result = reader.Read(reinterpret_cast<HRAWINPUT>(1));
    failures += expect_true(
        valid_result && (*valid_result)->header.dwType == RIM_TYPEKEYBOARD,
        "valid keyboard packet");
    failures += expect_true(
        valid.observed_command == RID_INPUT &&
            valid.observed_header_size == sizeof(RAWINPUTHEADER),
        "Raw Input API contract");

    FakeRawInputState resized{
        .packet = keyboard_packet(),
        .first_query_size = sizeof(RAWINPUTHEADER),
    };
    fake_state = &resized;
    reader = make_reader();
    const auto resized_result = reader.Read(reinterpret_cast<HRAWINPUT>(2));
    failures += expect_true(
        resized_result.has_value() && resized.read_count == 2,
        "packet growth retried");

    FakeRawInputState short_read{
        .packet = keyboard_packet(),
        .short_read = true,
    };
    fake_state = &short_read;
    reader = make_reader();
    failures += expect_true(
        !reader.Read(reinterpret_cast<HRAWINPUT>(3)),
        "short read rejected");

    FakeRawInputState bad_header{.packet = keyboard_packet()};
    reinterpret_cast<RAWINPUT*>(bad_header.packet.data())->header.dwSize =
        sizeof(RAWINPUTHEADER) - 1;
    fake_state = &bad_header;
    reader = make_reader();
    failures += expect_true(
        !reader.Read(reinterpret_cast<HRAWINPUT>(4)),
        "undersized header rejected");

    FakeRawInputState unsupported{.packet = keyboard_packet()};
    reinterpret_cast<RAWINPUT*>(unsupported.packet.data())->header.dwType =
        RIM_TYPEMOUSE;
    fake_state = &unsupported;
    reader = make_reader();
    failures += expect_true(
        !reader.Read(reinterpret_cast<HRAWINPUT>(5)),
        "unsupported packet type rejected");

    auto zero_size_packet = hid_packet(0, 1, {});
    const auto* zero_size =
        reinterpret_cast<const RAWINPUT*>(zero_size_packet.data());
    failures += expect_true(
        !HidReports(zero_size->data.hid),
        "zero report size rejected");

    auto zero_count_packet = hid_packet(1, 0, {});
    const auto* zero_count =
        reinterpret_cast<const RAWINPUT*>(zero_count_packet.data());
    failures += expect_true(
        !HidReports(zero_count->data.hid),
        "zero report count rejected");

    RAWHID overflow{};
    overflow.dwSizeHid = std::numeric_limits<DWORD>::max();
    overflow.dwCount = std::numeric_limits<DWORD>::max();
    failures += expect_true(
        !HidReports(overflow),
        "report byte count overflow rejected");

    auto valid_hid_packet = hid_packet(2, 3, {1, 2, 3, 4, 5, 6});
    const auto* valid_hid =
        reinterpret_cast<const RAWINPUT*>(valid_hid_packet.data());
    const auto reports = HidReports(valid_hid->data.hid);
    failures += expect_true(
        reports && reports->size() == 3,
        "three HID reports exposed");
    if (reports && reports->size() == 3)
    {
        const auto second = (*reports)[1];
        failures += expect_true(
            second.size() == 2 &&
                std::to_integer<unsigned char>(second[0]) == 3 &&
                std::to_integer<unsigned char>(second[1]) == 4,
            "HID reports are non-allocating slices");
    }

    if (failures != 0)
    {
        return 1;
    }

    std::cout << "RawInputPacketTests passed\n";
    return 0;
}

#pragma once

#include <Windows.h>

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace gc::input {

struct RawInputApi {
    decltype(&GetRawInputData) get_raw_input_data{::GetRawInputData};
};

class RawInputPacketBuffer {
public:
    explicit RawInputPacketBuffer(RawInputApi api = {});

    [[nodiscard]] std::expected<const RAWINPUT*, std::string> Read(
        HRAWINPUT handle);

private:
    RawInputApi api_;
    std::vector<std::byte> bytes_;
};

class HidReportView {
public:
    HidReportView(
        std::span<const std::byte> bytes,
        std::size_t report_size,
        std::size_t report_count) noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::span<const std::byte> operator[](
        std::size_t index) const noexcept;

private:
    std::span<const std::byte> bytes_;
    std::size_t report_size_{};
    std::size_t report_count_{};
};

[[nodiscard]] std::expected<HidReportView, std::string> HidReports(
    const RAWHID& hid) noexcept;

} // namespace gc::input

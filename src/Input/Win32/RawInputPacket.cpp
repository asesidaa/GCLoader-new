#include "Input/Win32/RawInputPacket.h"

#include <format>
#include <limits>

namespace gc::input
{
    namespace
    {
        std::string Win32Failure(const char* operation)
        {
            return std::format(
                "{} failed with Win32 error {}", operation, GetLastError());
        }

        std::expected<void, std::string> ValidatePacket(
            const RAWINPUT& input,
            std::size_t byte_count)
        {
            if (byte_count < sizeof(RAWINPUTHEADER) ||
                input.header.dwSize != byte_count)
            {
                return std::unexpected("Raw Input packet has an invalid header size");
            }

            if (input.header.dwType == RIM_TYPEKEYBOARD)
            {
                constexpr std::size_t minimum_size =
                    sizeof(RAWINPUTHEADER) + sizeof(RAWKEYBOARD);
                if (byte_count < minimum_size)
                {
                    return std::unexpected("Raw keyboard packet is truncated");
                }
                return {};
            }

            if (input.header.dwType == RIM_TYPEHID)
            {
                constexpr std::size_t data_offset =
                    sizeof(RAWINPUTHEADER) + offsetof(RAWHID, bRawData);
                if (byte_count < data_offset)
                {
                    return std::unexpected("Raw HID packet is truncated");
                }
                const auto reports = HidReports(input.data.hid);
                if (!reports)
                {
                    return std::unexpected(reports.error());
                }
                const std::size_t report_bytes =
                    static_cast<std::size_t>(input.data.hid.dwSizeHid) *
                    static_cast<std::size_t>(input.data.hid.dwCount);
                if (report_bytes > byte_count - data_offset)
                {
                    return std::unexpected("Raw HID report data is truncated");
                }
                return {};
            }

            return std::unexpected("Unsupported Raw Input packet type");
        }
    } // namespace

    std::expected<const RAWINPUT*, std::string> RawInputPacketBuffer::Read(
        HRAWINPUT handle)
    {
        if (handle == nullptr)
        {
            return std::unexpected("Raw Input packet reader is not initialized");
        }

        UINT required_size = 0;
        const UINT query_result = ::GetRawInputData(
            handle,
            RID_INPUT,
            nullptr,
            &required_size,
            sizeof(RAWINPUTHEADER));
        if (query_result == UINT_MAX)
        {
            return std::unexpected(Win32Failure("GetRawInputData(size)"));
        }
        if (query_result != 0 || required_size < sizeof(RAWINPUTHEADER))
        {
            return std::unexpected("GetRawInputData returned an invalid size");
        }

        for (int attempt = 0; attempt < 4; ++attempt)
        {
            if (bytes_.size() < required_size)
            {
                bytes_.resize(required_size);
            }
            if (bytes_.size() > std::numeric_limits<UINT>::max())
            {
                return std::unexpected("Raw Input packet is too large");
            }

            UINT read_size = static_cast<UINT>(bytes_.size());
            const UINT read_result = ::GetRawInputData(
                handle,
                RID_INPUT,
                bytes_.data(),
                &read_size,
                sizeof(RAWINPUTHEADER));
            if (read_result == UINT_MAX)
            {
                if (GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
                    read_size > bytes_.size())
                {
                    required_size = read_size;
                    continue;
                }
                return std::unexpected(Win32Failure("GetRawInputData(data)"));
            }
            if (read_result != read_size || read_result < sizeof(RAWINPUTHEADER) ||
                read_result > bytes_.size())
            {
                return std::unexpected("GetRawInputData returned a short read");
            }

            const auto* input = reinterpret_cast<const RAWINPUT*>(bytes_.data());
            const auto valid = ValidatePacket(*input, read_result);
            if (!valid)
            {
                return std::unexpected(valid.error());
            }
            return input;
        }

        return std::unexpected("Raw Input packet size did not stabilize");
    }

    HidReportView::HidReportView(
        std::span<const std::byte> bytes,
        std::size_t report_size,
        std::size_t report_count) noexcept
        : bytes_(bytes),
          report_size_(report_size),
          report_count_(report_count)
    {
    }

    std::size_t HidReportView::size() const noexcept
    {
        return report_count_;
    }

    std::span<const std::byte> HidReportView::operator[](
        std::size_t index) const noexcept
    {
        if (index >= report_count_)
        {
            return {};
        }
        return bytes_.subspan(index * report_size_, report_size_);
    }

    std::expected<HidReportView, std::string> HidReports(
        const RAWHID& hid) noexcept
    {
        const std::size_t report_size = hid.dwSizeHid;
        const std::size_t report_count = hid.dwCount;
        if (report_size == 0 || report_count == 0)
        {
            return std::unexpected("Raw HID report size and count must be nonzero");
        }
        if (report_size >
            std::numeric_limits<std::size_t>::max() / report_count)
        {
            return std::unexpected("Raw HID report byte count overflowed");
        }

        const std::size_t byte_count = report_size * report_count;
        return HidReportView(
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(hid.bRawData),
                byte_count),
            report_size,
            report_count);
    }
} // namespace gc::input

#include "Patches/SongUnlock/SongUnlockPatch.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <iterator>
#include <limits>
#include <span>
#include <string>

#include "Patches/GameCompatibility/GameBinaryPatch.h"
#include "plog/Log.h"

namespace gc::song_unlock
{
    namespace
    {
        constexpr std::uint32_t kAvailabilityBranchRva = 0x00257854U;
        constexpr std::array<std::byte, 6> kCleanBytes{
            std::byte{0x0F},
            std::byte{0x85},
            std::byte{0x1D},
            std::byte{0x02},
            std::byte{0x00},
            std::byte{0x00},
        };
        constexpr std::array<std::byte, 6> kPatchedBytes{
            std::byte{0xE9},
            std::byte{0x1E},
            std::byte{0x02},
            std::byte{0x00},
            std::byte{0x00},
            std::byte{0x90},
        };

        [[nodiscard]] bool CheckedPatchAddress(
            std::uintptr_t image_base,
            std::uintptr_t& address) noexcept
        {
            if (image_base == 0)
            {
                return false;
            }

            constexpr auto maximum = std::numeric_limits<std::uintptr_t>::max();
            if (kAvailabilityBranchRva > maximum - image_base)
            {
                return false;
            }

            address = image_base + kAvailabilityBranchRva;
            return kCleanBytes.size() - 1 <= maximum - address;
        }

        [[nodiscard]] std::string FormatBytes(
            std::span<const std::byte> bytes)
        {
            std::string text;
            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                if (index != 0)
                {
                    text.push_back(' ');
                }
                std::format_to(
                    std::back_inserter(text),
                    "{:02X}",
                    std::to_integer<unsigned int>(bytes[index]));
            }
            return text;
        }

        void LogMemoryFailure(
            const char* stage,
            const gc::game_compatibility::GameBinaryMemoryError& error)
        {
            PLOG_ERROR
        << "SongUnlockPatch: startup failed stage=" << stage
        << " rva=0x" << std::hex << std::uppercase
        << kAvailabilityBranchRva << std::dec
        << " memory_stage="
        << gc::game_compatibility::GameBinaryMemoryStageName(error.stage)
        << " win32_error=" << error.win32_error;
        }
    } // namespace

    bool SongUnlockPatchInit(bool enabled) noexcept
    {
        try
        {
            if (!enabled)
            {
                PLOG_INFO << "SongUnlockPatch: state=disabled";
                return true;
            }

            const auto module = GetModuleHandleW(nullptr);
            if (module == nullptr)
            {
                PLOG_ERROR
                << "SongUnlockPatch: startup failed stage=resolve_module"
                << " win32_error=" << GetLastError();
                return false;
            }

            const auto image_base = reinterpret_cast<std::uintptr_t>(module);
            std::uintptr_t address{};
            if (!CheckedPatchAddress(image_base, address))
            {
                PLOG_ERROR
                << "SongUnlockPatch: startup failed stage=address_range"
                << " rva=0x" << std::hex << std::uppercase
                << kAvailabilityBranchRva << std::dec;
                return false;
            }

            const auto actions =
                gc::game_compatibility::ProductionGameBinaryPatchActions();
            if (actions.read == nullptr || actions.write == nullptr)
            {
                PLOG_ERROR
                << "SongUnlockPatch: startup failed stage=invalid_actions"
                << " rva=0x" << std::hex << std::uppercase
                << kAvailabilityBranchRva << std::dec;
                return false;
            }

            std::array<std::byte, kCleanBytes.size()> actual{};
            const auto read = actions.read(
                actions.context,
                address,
                std::span<std::byte>{actual});
            if (!read)
            {
                LogMemoryFailure("site_read", read.error());
                return false;
            }

            if (actual == kPatchedBytes)
            {
                PLOG_INFO
                << "SongUnlockPatch: state=already_patched"
                << " rva=0x" << std::hex << std::uppercase
                << kAvailabilityBranchRva << std::dec;
                return true;
            }

            if (actual != kCleanBytes)
            {
                PLOG_ERROR
                << "SongUnlockPatch: startup failed stage=unknown_bytes"
                << " rva=0x" << std::hex << std::uppercase
                << kAvailabilityBranchRva << std::dec
                << " expected_clean=" << FormatBytes(kCleanBytes)
                << " expected_patched=" << FormatBytes(kPatchedBytes)
                << " actual=" << FormatBytes(actual);
                return false;
            }

            const auto write = actions.write(
                actions.context,
                address,
                std::span<const std::byte>{kPatchedBytes});
            if (!write)
            {
                LogMemoryFailure("site_write", write.error());
                return false;
            }

            PLOG_INFO
            << "SongUnlockPatch: state=patched"
            << " rva=0x" << std::hex << std::uppercase
            << kAvailabilityBranchRva << std::dec;
            return true;
        }
        catch (const std::exception& error)
        {
            try
            {
                PLOG_ERROR
                << "SongUnlockPatch: startup failed stage=exception"
                << " message=" << error.what();
            }
            catch (...)
            {
            }
            return false;
        }
        catch (...)
        {
            try
            {
                PLOG_ERROR
                << "SongUnlockPatch: startup failed stage=exception"
                << " message=unknown";
            }
            catch (...)
            {
            }
            return false;
        }
    }
} // namespace gc::song_unlock

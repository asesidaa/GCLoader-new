#include "Patches/TestModeTiming/SystemConfigTimingStore.h"

#include <algorithm>
#include <array>
#include <climits>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
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

std::unexpected<SystemConfigError> FileError(
    SystemConfigStage stage,
    DWORD error,
    DWORD cleanup_error = ERROR_SUCCESS) {
    return std::unexpected(SystemConfigError{
        .stage = stage,
        .win32_error = error,
        .cleanup_error = cleanup_error,
    });
}

std::expected<std::vector<std::uint8_t>, SystemConfigError> ReadAllBytes(
    const std::filesystem::path& path,
    const Win32FileApi& api) {
    const HANDLE handle = api.create_file(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return FileError(SystemConfigStage::TargetOpen, api.get_last_error());
    }

    const auto fail_and_close = [&](SystemConfigStage stage, DWORD error)
        -> std::expected<std::vector<std::uint8_t>, SystemConfigError> {
        DWORD cleanup_error = ERROR_SUCCESS;
        if (!api.close_handle(handle)) {
            cleanup_error = api.get_last_error();
        }
        return FileError(stage, error, cleanup_error);
    };

    LARGE_INTEGER size{};
    if (!api.get_file_size(handle, &size)) {
        const DWORD error = api.get_last_error();
        return fail_and_close(SystemConfigStage::TargetSize, error);
    }
    if (size.QuadPart < 0 ||
        static_cast<unsigned long long>(size.QuadPart) >
            std::numeric_limits<DWORD>::max()) {
        return fail_and_close(
            SystemConfigStage::TargetSize, ERROR_FILE_TOO_LARGE);
    }

    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(size.QuadPart));
    DWORD bytes_read = 0;
    if (!bytes.empty() &&
        !api.read_file(
            handle,
            bytes.data(),
            static_cast<DWORD>(bytes.size()),
            &bytes_read,
            nullptr)) {
        const DWORD error = api.get_last_error();
        return fail_and_close(SystemConfigStage::TargetRead, error);
    }
    if (bytes_read != bytes.size()) {
        return fail_and_close(
            SystemConfigStage::TargetRead, ERROR_HANDLE_EOF);
    }
    if (!api.close_handle(handle)) {
        return FileError(
            SystemConfigStage::TargetClose, api.get_last_error());
    }
    return bytes;
}

std::expected<void, SystemConfigError> WriteFlushClose(
    HANDLE handle,
    std::span<const std::uint8_t> bytes,
    const Win32FileApi& api) {
    std::optional<SystemConfigError> primary;
    std::size_t offset = 0;
    while (!primary && offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            remaining,
            std::numeric_limits<DWORD>::max()));
        DWORD written = 0;
        if (!api.write_file(
                handle,
                bytes.data() + offset,
                requested,
                &written,
                nullptr)) {
            primary = SystemConfigError{
                .stage = SystemConfigStage::TempWrite,
                .win32_error = api.get_last_error(),
            };
            break;
        }
        if (written == 0 || written > requested) {
            primary = SystemConfigError{
                .stage = SystemConfigStage::TempWrite,
                .win32_error = ERROR_WRITE_FAULT,
            };
            break;
        }
        offset += written;
    }

    if (!primary && !api.flush_file(handle)) {
        primary = SystemConfigError{
            .stage = SystemConfigStage::TempFlush,
            .win32_error = api.get_last_error(),
        };
    }

    if (!api.close_handle(handle)) {
        const DWORD close_error = api.get_last_error();
        if (primary) {
            primary->cleanup_error = close_error;
        } else {
            primary = SystemConfigError{
                .stage = SystemConfigStage::TempClose,
                .win32_error = close_error,
            };
        }
    }

    if (primary) {
        return std::unexpected(*primary);
    }
    return {};
}

std::filesystem::path BuildTemporaryPath(
    const std::filesystem::path& target,
    DWORD process_id,
    unsigned attempt) {
    auto filename = target.filename().wstring();
    filename += L".gcloader.";
    filename += std::to_wstring(process_id);
    filename += L".";
    filename += std::to_wstring(attempt);
    filename += L".tmp";
    return target.parent_path() / filename;
}

DWORD DeleteTemporaryFile(
    const std::filesystem::path& path,
    const Win32FileApi& api) noexcept {
    if (api.delete_file(path.c_str())) {
        return ERROR_SUCCESS;
    }
    return api.get_last_error();
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
    std::ranges::sort(
        replacements,
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

Win32FileApi ProductionWin32FileApi() noexcept {
    return {};
}

SystemConfigTimingStore::SystemConfigTimingStore(
    std::filesystem::path path,
    const Win32FileApi& api)
    : path_(std::move(path)),
      api_(api) {}

std::expected<SaveOutcome, SystemConfigError>
// Saving mutates the persisted system config through the file API.
// ReSharper disable once CppMemberFunctionMayBeConst
SystemConfigTimingStore::Save(TimingOffsets offsets) noexcept {
    try {
        auto input = ReadAllBytes(path_, api_);
        if (!input) {
            return std::unexpected(input.error());
        }

        auto rewritten = RewriteTimingAssignments(*input, offsets);
        if (!rewritten) {
            return std::unexpected(SystemConfigError{
                .stage = SystemConfigStage::Assignment,
                .edit_error = rewritten.error(),
            });
        }
        if (!rewritten->changed) {
            return SaveOutcome::Unchanged;
        }

        for (unsigned attempt = 0; attempt != 16; ++attempt) {
            const auto temp = BuildTemporaryPath(
                path_, api_.get_process_id(), attempt);
            const HANDLE handle = api_.create_file(
                temp.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                const DWORD error = api_.get_last_error();
                if (error == ERROR_FILE_EXISTS) {
                    continue;
                }
                return FileError(SystemConfigStage::TempCreate, error);
            }

            auto staged = WriteFlushClose(
                handle, rewritten->bytes, api_);
            if (!staged) {
                auto error = staged.error();
                const DWORD delete_error = DeleteTemporaryFile(temp, api_);
                if (delete_error != ERROR_SUCCESS) {
                    error.cleanup_error = delete_error;
                }
                return std::unexpected(error);
            }

            if (!api_.replace_file(
                    path_.c_str(),
                    temp.c_str(),
                    nullptr,
                    REPLACEFILE_WRITE_THROUGH,
                    nullptr,
                    nullptr)) {
                const DWORD primary = api_.get_last_error();
                const DWORD cleanup = DeleteTemporaryFile(temp, api_);
                return FileError(
                    SystemConfigStage::Replace, primary, cleanup);
            }
            return SaveOutcome::Changed;
        }
        return FileError(
            SystemConfigStage::TempCreate, ERROR_FILE_EXISTS);
    } catch (const std::bad_alloc&) {
        return FileError(
            SystemConfigStage::Internal, ERROR_OUTOFMEMORY);
    } catch (...) {
        return FileError(
            SystemConfigStage::Internal, ERROR_GEN_FAILURE);
    }
}

} // namespace gc::test_mode_timing

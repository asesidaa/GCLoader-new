#include "Locale/FilesystemDiagnostics.h"

#include <plog/Log.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <type_traits>

namespace gc::locale_compatibility {
namespace {

constexpr UINT kJapaneseCodePage = 932;
constexpr std::string_view kCapMessage =
    "filesystem diagnostic category cap reached; additional events in "
    "capped categories suppressed";

thread_local bool g_inside_filesystem_diagnostics{};

class DiagnosticScope {
public:
    DiagnosticScope() noexcept
        : incoming_error_{GetLastError()} {
        if (!g_inside_filesystem_diagnostics) {
            g_inside_filesystem_diagnostics = true;
            active_ = true;
        }
    }

    ~DiagnosticScope() {
        if (active_) {
            g_inside_filesystem_diagnostics = false;
        }
        SetLastError(incoming_error_);
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return active_;
    }

private:
    DWORD incoming_error_{};
    bool active_{};
};

template <std::size_t Capacity>
class FixedText {
public:
    void Append(char value) noexcept {
        if (size_ + 1 < Capacity) {
            storage_[size_++] = value;
            storage_[size_] = '\0';
        } else {
            truncated_ = true;
        }
    }

    void Append(std::string_view text) noexcept {
        for (const char value : text) {
            Append(value);
        }
    }

    template <typename Integer>
    void AppendInteger(Integer value) noexcept {
        std::array<char, 32> digits{};
        const auto [end, error] = std::to_chars(
            digits.data(),
            digits.data() + digits.size(),
            value);
        if (error == std::errc{}) {
            Append(std::string_view{
                digits.data(),
                static_cast<std::size_t>(end - digits.data())});
        }
    }

    [[nodiscard]] std::string_view View() const noexcept {
        return {storage_.data(), size_};
    }

    [[nodiscard]] bool Truncated() const noexcept {
        return truncated_;
    }

private:
    std::array<char, Capacity> storage_{};
    std::size_t size_{};
    bool truncated_{};
};

struct BoundedPath {
    LPCSTR data{};
    std::size_t size{};
    bool inspection_truncated{};
};

BoundedPath InspectPath(LPCSTR path) noexcept {
    if (path == nullptr) {
        return {};
    }
    for (std::size_t size = 0;
         size < kFilesystemInspectionLimit;
         ++size) {
        if (path[size] == '\0') {
            return {path, size, false};
        }
    }
    return {path, kFilesystemInspectionLimit, true};
}

char AsciiLower(char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

bool EqualsAsciiInsensitive(
    std::string_view left,
    std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (AsciiLower(left[index]) != AsciiLower(right[index])) {
            return false;
        }
    }
    return true;
}

bool StartsWithAsciiInsensitive(
    std::string_view text,
    std::string_view prefix) noexcept {
    return text.size() >= prefix.size() &&
        EqualsAsciiInsensitive(text.substr(0, prefix.size()), prefix);
}

std::string_view PathView(const BoundedPath& path) noexcept {
    return path.data != nullptr
        ? std::string_view{path.data, path.size}
        : std::string_view{};
}

std::string_view Basename(std::string_view path) noexcept {
    const auto separator = path.find_last_of("\\/");
    return separator == std::string_view::npos
        ? path
        : path.substr(separator + 1);
}

bool IsComPort(std::string_view path) noexcept {
    const auto basename = Basename(path);
    if (basename.size() <= 3 ||
        !StartsWithAsciiInsensitive(basename, "com")) {
        return false;
    }
    return std::all_of(
        basename.begin() + 3,
        basename.end(),
        [](char value) { return value >= '0' && value <= '9'; });
}

bool IsExcluded(const BoundedPath& bounded) noexcept {
    if (bounded.data == nullptr) {
        return false;
    }
    const auto path = PathView(bounded);
    if (StartsWithAsciiInsensitive(path, "\\\\.\\") ||
        StartsWithAsciiInsensitive(path, "\\\\?\\pipe\\") ||
        StartsWithAsciiInsensitive(path, "\\Device\\")) {
        return true;
    }
    if (IsComPort(path)) {
        return true;
    }
    const auto basename = Basename(path);
    return EqualsAsciiInsensitive(basename, "loader-log.txt") ||
        EqualsAsciiInsensitive(basename, "loader-service-log.txt");
}

bool HasHighByte(const BoundedPath& path) noexcept {
    for (std::size_t index = 0; index < path.size; ++index) {
        if (static_cast<unsigned char>(path.data[index]) >= 0x80) {
            return true;
        }
    }
    return false;
}

std::string_view RoleName(FilesystemDiagnosticRole role) noexcept {
    return role == FilesystemDiagnosticRole::service
        ? "service"
        : "game";
}

std::string_view ApiName(AnsiFilesystemApi api) noexcept {
    switch (api) {
    case AnsiFilesystemApi::create_file:
        return "create_file";
    case AnsiFilesystemApi::get_file_attributes:
        return "get_file_attributes";
    case AnsiFilesystemApi::find_first_file:
        return "find_first_file";
    case AnsiFilesystemApi::find_next_file:
        return "find_next_file";
    case AnsiFilesystemApi::create_directory:
        return "create_directory";
    case AnsiFilesystemApi::delete_file:
        return "delete_file";
    case AnsiFilesystemApi::move_file:
        return "move_file";
    case AnsiFilesystemApi::copy_file:
        return "copy_file";
    }
    return "unknown";
}

std::string_view ProbeName(WideProbeOutcome outcome) noexcept {
    switch (outcome) {
    case WideProbeOutcome::not_run:
        return "not_run";
    case WideProbeOutcome::invalid_cp932:
        return "invalid_cp932";
    case WideProbeOutcome::exists:
        return "exists";
    case WideProbeOutcome::missing:
        return "missing";
    case WideProbeOutcome::inaccessible:
        return "inaccessible";
    }
    return "not_run";
}

bool DecodeCp932(
    const BoundedPath& path,
    std::array<wchar_t, kFilesystemInspectionLimit + 1>& storage,
    std::wstring_view& decoded) noexcept {
    decoded = {};
    if (path.data == nullptr || path.size == 0) {
        return true;
    }
    const int converted = MultiByteToWideChar(
        kJapaneseCodePage,
        MB_ERR_INVALID_CHARS,
        path.data,
        static_cast<int>(path.size),
        storage.data(),
        static_cast<int>(kFilesystemInspectionLimit));
    if (converted <= 0) {
        return false;
    }
    storage[static_cast<std::size_t>(converted)] = L'\0';
    decoded = {storage.data(), static_cast<std::size_t>(converted)};
    return true;
}

template <std::size_t Capacity>
void AppendHexByte(FixedText<Capacity>& output, unsigned char value) noexcept {
    constexpr std::string_view digits = "0123456789ABCDEF";
    output.Append("\\x");
    output.Append(digits[(value >> 4U) & 0x0FU]);
    output.Append(digits[value & 0x0FU]);
}

template <std::size_t Capacity>
void AppendEscapedBytes(
    FixedText<Capacity>& output,
    const char* bytes,
    std::size_t size,
    bool preserve_utf8) noexcept {
    for (std::size_t index = 0; index < size; ++index) {
        const auto value = static_cast<unsigned char>(bytes[index]);
        if (value == '\\') {
            output.Append("\\\\");
        } else if (value == '"') {
            output.Append("\\\"");
        } else if (value >= 0x20 &&
                   (value < 0x7F || preserve_utf8)) {
            output.Append(static_cast<char>(value));
        } else {
            AppendHexByte(output, value);
        }
    }
}

std::size_t DecodeRenderedPrefix(
    const BoundedPath& path,
    std::array<wchar_t, kFilesystemRenderedPathLimit + 1>& output) noexcept {
    std::size_t input_size = (std::min)(
        path.size,
        kFilesystemRenderedPathLimit);
    while (input_size != 0) {
        const int converted = MultiByteToWideChar(
            kJapaneseCodePage,
            MB_ERR_INVALID_CHARS,
            path.data,
            static_cast<int>(input_size),
            output.data(),
            static_cast<int>(kFilesystemRenderedPathLimit));
        if (converted > 0) {
            output[static_cast<std::size_t>(converted)] = L'\0';
            return static_cast<std::size_t>(converted);
        }
        if (input_size == path.size) {
            break;
        }
        --input_size;
    }
    return 0;
}

template <std::size_t Capacity>
void AppendPath(
    FixedText<Capacity>& output,
    std::string_view field,
    const BoundedPath& path,
    bool valid_cp932) noexcept {
    const std::size_t rendered_bytes = (std::min)(
        path.size,
        kFilesystemRenderedPathLimit);
    output.Append(" ");
    output.Append(field);
    output.Append("_raw=\"");
    if (path.data != nullptr) {
        AppendEscapedBytes(
            output,
            path.data,
            rendered_bytes,
            false);
    }
    output.Append("\" ");
    output.Append(field);
    output.Append("_decoded=\"");
    if (!valid_cp932) {
        output.Append("<invalid_cp932>");
    } else if (path.data != nullptr && path.size != 0) {
        std::array<wchar_t, kFilesystemRenderedPathLimit + 1> wide{};
        const auto wide_size = DecodeRenderedPrefix(path, wide);
        if (wide_size != 0) {
            std::array<char, kFilesystemRenderedPathLimit * 3 + 1>
                utf8{};
            const int utf8_size = WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                wide.data(),
                static_cast<int>(wide_size),
                utf8.data(),
                static_cast<int>(utf8.size() - 1),
                nullptr,
                nullptr);
            if (utf8_size > 0) {
                AppendEscapedBytes(
                    output,
                    utf8.data(),
                    static_cast<std::size_t>(utf8_size),
                    true);
            }
        }
    }
    output.Append("\" ");
    output.Append(field);
    output.Append("_rendered_bytes=");
    output.AppendInteger(rendered_bytes);
    output.Append(" ");
    output.Append(field);
    output.Append("_truncated=");
    output.Append(
        path.inspection_truncated ||
                path.size > kFilesystemRenderedPathLimit
            ? "true"
            : "false");
}

class Fnv64 {
public:
    void AddByte(std::uint8_t value) noexcept {
        value_ ^= value;
        value_ *= 1099511628211ULL;
    }

    template <typename Value>
    void AddValue(Value value) noexcept {
        if constexpr (std::is_enum_v<Value>) {
            AddValue(static_cast<std::underlying_type_t<Value>>(value));
        } else if constexpr (std::is_same_v<Value, bool>) {
            AddByte(value ? 1U : 0U);
        } else {
            using Unsigned = std::make_unsigned_t<Value>;
            auto remaining = static_cast<Unsigned>(value);
            for (std::size_t index = 0;
                 index < sizeof(Unsigned);
                 ++index) {
                AddByte(static_cast<std::uint8_t>(remaining & 0xFFU));
                remaining >>= 8U;
            }
        }
    }

    void AddPath(const BoundedPath& path) noexcept {
        AddValue(path.data != nullptr);
        AddValue(path.size);
        AddValue(path.inspection_truncated);
        for (std::size_t index = 0; index < path.size; ++index) {
            AddByte(static_cast<std::uint8_t>(path.data[index]));
        }
    }

    [[nodiscard]] std::uint64_t Finish() const noexcept {
        return value_ == 0 ? 1 : value_;
    }

private:
    std::uint64_t value_{14695981039346656037ULL};
};

enum class EventClass : std::uint8_t {
    non_ascii,
    failure,
};

enum class InsertResult {
    inserted,
    duplicate,
    full,
};

InsertResult InsertHash(
    std::array<std::atomic_uint64_t, kFilesystemCategoryCapacity>& table,
    std::uint64_t hash) noexcept {
    for (auto& slot : table) {
        auto existing = slot.load(std::memory_order_relaxed);
        if (existing == hash) {
            return InsertResult::duplicate;
        }
        if (existing == 0) {
            std::uint64_t expected = 0;
            if (slot.compare_exchange_strong(
                    expected,
                    hash,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed)) {
                return InsertResult::inserted;
            }
            if (expected == hash) {
                return InsertResult::duplicate;
            }
        }
    }
    return InsertResult::full;
}

WideProbeOutcome ErrorOutcome(DWORD error) noexcept {
    return error == ERROR_FILE_NOT_FOUND ||
            error == ERROR_PATH_NOT_FOUND
        ? WideProbeOutcome::missing
        : WideProbeOutcome::inaccessible;
}

WideProbeOutcome ProbeAttributes(std::wstring_view path) noexcept {
    if (path.empty()) {
        return WideProbeOutcome::not_run;
    }
    if (GetFileAttributesW(path.data()) != INVALID_FILE_ATTRIBUTES) {
        return WideProbeOutcome::exists;
    }
    return ErrorOutcome(GetLastError());
}

WideProbeOutcome CombineProbeOutcomes(
    WideProbeOutcome first,
    WideProbeOutcome second) noexcept {
    if (first == WideProbeOutcome::exists ||
        second == WideProbeOutcome::exists) {
        return WideProbeOutcome::exists;
    }
    if (first == WideProbeOutcome::inaccessible ||
        second == WideProbeOutcome::inaccessible) {
        return WideProbeOutcome::inaccessible;
    }
    if (first == WideProbeOutcome::missing ||
        second == WideProbeOutcome::missing) {
        return WideProbeOutcome::missing;
    }
    return WideProbeOutcome::not_run;
}

WideProbeOutcome ProductionProbe(
    void*,
    AnsiFilesystemApi api,
    std::wstring_view first,
    std::wstring_view second) noexcept {
    switch (api) {
    case AnsiFilesystemApi::create_file:
    case AnsiFilesystemApi::get_file_attributes:
        return ProbeAttributes(first);
    case AnsiFilesystemApi::find_first_file: {
        if (first.empty()) {
            return WideProbeOutcome::not_run;
        }
        WIN32_FIND_DATAW data{};
        const HANDLE found = FindFirstFileW(first.data(), &data);
        if (found == INVALID_HANDLE_VALUE) {
            return ErrorOutcome(GetLastError());
        }
        FindClose(found);
        return WideProbeOutcome::exists;
    }
    case AnsiFilesystemApi::find_next_file:
        return WideProbeOutcome::not_run;
    case AnsiFilesystemApi::create_directory:
    case AnsiFilesystemApi::delete_file:
    case AnsiFilesystemApi::move_file:
    case AnsiFilesystemApi::copy_file:
        return CombineProbeOutcomes(
            ProbeAttributes(first),
            ProbeAttributes(second));
    }
    return WideProbeOutcome::not_run;
}

void ProductionEmit(void*, std::string_view message) noexcept {
    try {
        PLOG_INFO << std::string{message};
    } catch (...) {
    }
}

} // namespace

FilesystemDiagnosticActions
ProductionFilesystemDiagnosticActions() noexcept {
    return {
        .context = nullptr,
        .probe = &ProductionProbe,
        .emit = &ProductionEmit,
    };
}

FilesystemDiagnostics::FilesystemDiagnostics(
    FilesystemDiagnosticRole role,
    FilesystemDiagnosticActions actions) noexcept
    : role_{role},
      actions_{actions} {
}

void FilesystemDiagnostics::Start(
    std::span<const AnsiFilesystemApi> apis) noexcept {
    DiagnosticScope scope;
    if (!scope || started_.exchange(true, std::memory_order_relaxed)) {
        return;
    }
    if (actions_.emit == nullptr) {
        return;
    }
    try {
        FixedText<768> message;
        message.Append("FilesystemDiagnostics: active role=");
        message.Append(RoleName(role_));
        message.Append(" apis=");
        for (std::size_t index = 0; index < apis.size(); ++index) {
            if (index != 0) {
                message.Append(",");
            }
            message.Append(ApiName(apis[index]));
        }
        message.Append(" non_ascii_capacity=");
        message.AppendInteger(kFilesystemCategoryCapacity);
        message.Append(" failure_capacity=");
        message.AppendInteger(kFilesystemCategoryCapacity);
        message.Append(" rendered_path_limit=");
        message.AppendInteger(kFilesystemRenderedPathLimit);
        message.Append(" inspection_limit=");
        message.AppendInteger(kFilesystemInspectionLimit);
        actions_.emit(actions_.context, message.View());
    } catch (...) {
    }
}

void FilesystemDiagnostics::Observe(
    const AnsiFilesystemObservation& observation) noexcept {
    DiagnosticScope scope;
    if (!scope) {
        return;
    }

    try {
        const auto first = InspectPath(observation.first_path);
        const auto second = InspectPath(observation.second_path);
        if (IsExcluded(first) || IsExcluded(second)) {
            return;
        }

        const bool non_ascii = HasHighByte(first) || HasHighByte(second);
        const auto event_class = non_ascii
            ? EventClass::non_ascii
            : EventClass::failure;
        if (!non_ascii) {
            if (observation.succeeded ||
                (observation.api == AnsiFilesystemApi::find_next_file &&
                 observation.last_error == ERROR_NO_MORE_FILES)) {
                return;
            }
        }

        std::array<wchar_t, kFilesystemInspectionLimit + 1> first_wide{};
        std::array<wchar_t, kFilesystemInspectionLimit + 1> second_wide{};
        std::wstring_view decoded_first;
        std::wstring_view decoded_second;
        const bool valid_first = DecodeCp932(
            first, first_wide, decoded_first);
        const bool valid_second = DecodeCp932(
            second, second_wide, decoded_second);
        const bool valid_cp932 = valid_first && valid_second;

        WideProbeOutcome probe = WideProbeOutcome::not_run;
        if (!valid_cp932) {
            probe = WideProbeOutcome::invalid_cp932;
        } else if (
            observation.api != AnsiFilesystemApi::find_next_file &&
            actions_.probe != nullptr) {
            probe = actions_.probe(
                actions_.context,
                observation.api,
                decoded_first,
                decoded_second);
        }

        Fnv64 hash;
        hash.AddValue(role_);
        hash.AddValue(observation.api);
        hash.AddValue(event_class);
        hash.AddPath(first);
        hash.AddPath(second);
        hash.AddValue(observation.succeeded);
        hash.AddValue(observation.last_error);
        hash.AddValue(probe);

        auto& category = event_class == EventClass::non_ascii
            ? non_ascii_
            : failures_;
        const auto inserted = InsertHash(category, hash.Finish());
        if (inserted == InsertResult::duplicate) {
            return;
        }
        if (inserted == InsertResult::full) {
            if (actions_.emit != nullptr &&
                !cap_logged_.exchange(true, std::memory_order_relaxed)) {
                actions_.emit(actions_.context, kCapMessage);
            }
            return;
        }
        if (actions_.emit == nullptr) {
            return;
        }

        FixedText<4096> message;
        message.Append("FilesystemDiagnostics: role=");
        message.Append(RoleName(role_));
        message.Append(" api=");
        message.Append(ApiName(observation.api));
        message.Append(" class=");
        message.Append(event_class == EventClass::non_ascii
            ? "non_ascii"
            : "failure");
        message.Append(" succeeded=");
        message.Append(observation.succeeded ? "true" : "false");
        message.Append(" error=");
        message.AppendInteger(observation.last_error);
        message.Append(" probe=");
        message.Append(ProbeName(probe));
        AppendPath(message, "first", first, valid_first);
        if (observation.second_path != nullptr) {
            AppendPath(message, "second", second, valid_second);
        }
        message.Append(" truncated=");
        message.Append(
            first.inspection_truncated || second.inspection_truncated ||
                    first.size > kFilesystemRenderedPathLimit ||
                    second.size > kFilesystemRenderedPathLimit ||
                    message.Truncated()
                ? "true"
                : "false");
        actions_.emit(actions_.context, message.View());
    } catch (...) {
    }
}

} // namespace gc::locale_compatibility

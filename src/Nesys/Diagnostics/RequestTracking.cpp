#include "Nesys/Diagnostics/RequestTracking.h"

#include <WinHttp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <iomanip>
#include <iterator>
#include <limits>



namespace gc::nesys_service::diagnostics {
namespace detail {
constexpr char kPipeNameA[] = R"(\\.\pipe\nesys_games)";
constexpr wchar_t kPipeNameW[] = LR"(\\.\pipe\nesys_games)";
constexpr std::uint64_t kLogAggregateWindowMs = 10'000;
constexpr std::uint64_t kSlowLogRateWindowMs = 60'000;
constexpr std::uint32_t kSlowLogRateLimit = 8;





class SharedLock {
public:
    explicit SharedLock(SRWLOCK& lock) noexcept : lock_{lock} {
        AcquireSRWLockShared(&lock_);
    }
    ~SharedLock() { ReleaseSRWLockShared(&lock_); }

private:
    SRWLOCK& lock_;
};

class ExclusiveLock {
public:
    explicit ExclusiveLock(SRWLOCK& lock) noexcept : lock_{lock} {
        AcquireSRWLockExclusive(&lock_);
    }
    ~ExclusiveLock() { ReleaseSRWLockExclusive(&lock_); }

private:
    SRWLOCK& lock_;
};

struct NesysMessageHeader {
    std::uint32_t command{};
    std::uint32_t payload_size{};
    std::uint32_t total_size{};
};



struct PipeHandleState {
    HANDLE handle{};
    PipeEndpoint endpoint{PipeEndpoint::Game};
    PendingPipeMessage pending{};
};



struct VendorLogHandleState {
    HANDLE handle{};
    std::uint64_t sequence{};
    std::uint64_t started_ms{};
    std::uint64_t open_ms{};
    std::uint64_t seek_ms{};
    std::uint64_t write_ms{};
    std::uint32_t write_calls{};
    std::uint64_t bytes{};
    bool failed{false};
    DWORD error{};
    std::array<char, 96> file{};
};







struct VendorLogAggregateState {
    std::uint64_t started_ms{};
    std::uint64_t records{};
    std::uint64_t bytes{};
    std::uint64_t api_ms{};
    std::uint64_t total_ms{};
    std::uint64_t max_ms{};
    std::uint64_t slow_records{};
    std::uint64_t failures{};
    std::uint64_t suppressed_slow{};
};

struct HttpSessionState {
    HINTERNET handle{};
    std::uint64_t started_ms{};
    std::uint64_t open_ms{};
    std::uint64_t timeout_ms{};
    int resolve_timeout{};
    int connect_timeout{};
    int send_timeout{};
    int receive_timeout{};
    bool failed{false};
    DWORD error{};
};

struct HttpConnectionState {
    HINTERNET handle{};
    HINTERNET session{};
    std::uint64_t connect_ms{};
    bool failed{false};
    DWORD error{};
};





SRWLOCK g_state_lock = SRWLOCK_INIT;
std::array<PipeHandleState, 8> g_pipe_handles{};
std::array<VendorLogHandleState, 32> g_vendor_log_handles{};
std::array<HttpSessionState, 32> g_http_sessions{};
std::array<HttpConnectionState, 32> g_http_connections{};
std::array<HttpRequestState, 32> g_http_requests{};
VendorLogAggregateState g_log_aggregate{};
std::uint64_t g_slow_rate_started_ms{};
std::uint32_t g_slow_rate_emitted{};
bool g_vendor_log_path_emitted{false};
std::atomic<std::uint64_t> g_pipe_sequence{0};
std::atomic<std::uint64_t> g_vendor_log_sequence{0};
std::atomic<std::uint64_t> g_http_sequence{0};


template <typename Character>
bool equals_ignore_case(
    const Character* left,
    const Character* right) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    while (*left != 0 && *right != 0) {
        const auto l = static_cast<Character>(
            std::tolower(static_cast<unsigned char>(*left)));
        const auto r = static_cast<Character>(
            std::tolower(static_cast<unsigned char>(*right)));
        if (l != r) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}

template <>
bool equals_ignore_case<wchar_t>(
    const wchar_t* left,
    const wchar_t* right) noexcept {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    while (*left != 0 && *right != 0) {
        if (std::towlower(*left) != std::towlower(*right)) {
            return false;
        }
        ++left;
        ++right;
    }
    return *left == 0 && *right == 0;
}

char normalized_path_character(char value) noexcept {
    if (value == '/') {
        return '\\';
    }
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(value)));
}

bool is_vendor_log_path(LPCSTR path) noexcept {
    constexpr char needle[] = "cmdfile\\log\\";
    if (path == nullptr) {
        return false;
    }
    const std::size_t length = std::strlen(path);
    constexpr std::size_t needle_length = sizeof(needle) - 1;
    if (length < needle_length) {
        return false;
    }
    for (std::size_t offset = 0;
         offset + needle_length <= length;
         ++offset) {
        bool matched = true;
        for (std::size_t index = 0; index < needle_length; ++index) {
            if (normalized_path_character(path[offset + index]) !=
                needle[index]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

bool is_card_http_path(LPCWSTR path) noexcept {
    constexpr wchar_t needle[] = L"/service/card/";
    if (path == nullptr) {
        return false;
    }
    const std::size_t length = std::wcslen(path);
    constexpr std::size_t needle_length = std::size(needle) - 1;
    if (length < needle_length) {
        return false;
    }
    for (std::size_t offset = 0;
         offset + needle_length <= length;
         ++offset) {
        bool matched = true;
        for (std::size_t index = 0; index < needle_length; ++index) {
            wchar_t value = path[offset + index];
            if (value == L'\\') {
                value = L'/';
            }
            if (std::towlower(value) != needle[index]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return true;
        }
    }
    return false;
}

template <std::size_t Size>
void copy_ascii(
    std::array<char, Size>& destination,
    const char* source) noexcept {
    destination.fill(0);
    if (source == nullptr || Size == 0) {
        return;
    }
    std::size_t index = 0;
    while (source[index] != 0 && index + 1 < Size) {
        destination[index] = source[index];
        ++index;
    }
}

template <std::size_t Size>
void copy_file_name(
    std::array<char, Size>& destination,
    const char* path) noexcept {
    if (path == nullptr) {
        destination.fill(0);
        return;
    }
    const char* file = path;
    for (const char* cursor = path; *cursor != 0; ++cursor) {
        if (*cursor == '\\' || *cursor == '/') {
            file = cursor + 1;
        }
    }
    copy_ascii(destination, file);
}

template <std::size_t Size>
void copy_ascii_from_wide(
    std::array<char, Size>& destination,
    LPCWSTR source) noexcept {
    destination.fill(0);
    if (source == nullptr || Size == 0) {
        return;
    }
    std::size_t index = 0;
    while (source[index] != 0 && index + 1 < Size) {
        const wchar_t value = source[index];
        destination[index] = value >= 0x20 && value <= 0x7E
            ? static_cast<char>(value)
            : '?';
        ++index;
    }
}

bool is_card_command(std::uint32_t command) noexcept {
    // NesysService.exe dispatches card requests 0x09..0x12. The matching
    // response command family adds 0x100, as seen in its pipe writer.
    return (command >= 0x09 && command <= 0x12) ||
        (command >= 0x109 && command <= 0x112);
}

bool parse_nesys_message(
    LPCVOID buffer,
    DWORD bytes,
    NesysMessageHeader* result) noexcept {
    if (buffer == nullptr || result == nullptr || bytes < 8) {
        return false;
    }
    std::uint32_t command{};
    std::uint32_t payload_size{};
    std::memcpy(&command, buffer, sizeof(command));
    std::memcpy(
        &payload_size,
        static_cast<const std::byte*>(buffer) + sizeof(command),
        sizeof(payload_size));
    const std::uint64_t total =
        static_cast<std::uint64_t>(payload_size) + 8;
    if (total > bytes || total > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    *result = {
        command,
        payload_size,
        static_cast<std::uint32_t>(total),
    };
    return true;
}

PipeHandleState* find_pipe_locked(HANDLE handle) noexcept {
    for (auto& entry : g_pipe_handles) {
        if (entry.handle == handle) {
            return &entry;
        }
    }
    return nullptr;
}

void track_pipe_locked(HANDLE handle, PipeEndpoint endpoint) noexcept {
    PipeHandleState* entry = find_pipe_locked(handle);
    if (entry == nullptr) {
        for (auto& candidate : g_pipe_handles) {
            if (candidate.handle == nullptr) {
                entry = &candidate;
                break;
            }
        }
    }
    if (entry != nullptr) {
        *entry = {
            .handle = handle,
            .endpoint = endpoint,
        };
    }
}

VendorLogHandleState* find_vendor_log_locked(HANDLE handle) noexcept {
    for (auto& entry : g_vendor_log_handles) {
        if (entry.handle == handle) {
            return &entry;
        }
    }
    return nullptr;
}

HttpSessionState* find_session_locked(HINTERNET handle) noexcept {
    for (auto& entry : g_http_sessions) {
        if (entry.handle == handle) {
            return &entry;
        }
    }
    return nullptr;
}

HttpConnectionState* find_connection_locked(HINTERNET handle) noexcept {
    for (auto& entry : g_http_connections) {
        if (entry.handle == handle) {
            return &entry;
        }
    }
    return nullptr;
}

HttpRequestState* find_request_locked(HINTERNET handle) noexcept {
    for (auto& entry : g_http_requests) {
        if (entry.handle == handle) {
            return &entry;
        }
    }
    return nullptr;
}

TrackedHandleKind tracked_handle_kind(HANDLE handle) noexcept {
    SharedLock lock{g_state_lock};
    if (find_pipe_locked(handle) != nullptr) {
        return TrackedHandleKind::Pipe;
    }
    if (find_vendor_log_locked(handle) != nullptr) {
        return TrackedHandleKind::VendorLog;
    }
    return TrackedHandleKind::None;
}

void mark_failure(bool& failed, DWORD& error, DWORD value) noexcept {
    failed = true;
    if (error == ERROR_SUCCESS) {
        error = value;
    }
}

void observe_pipe_open(
    PipeEndpoint endpoint,
    HANDLE handle,
    std::uint64_t started_ms,
    std::uint64_t finished_ms,
    DWORD last_error) noexcept {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
        ExclusiveLock lock{g_state_lock};
        track_pipe_locked(handle, endpoint);
    }
    log_pipe_open(
        endpoint,
        handle,
        finished_ms >= started_ms ? finished_ms - started_ms : 0,
        last_error);
}

void observe_pipe_write(
    HANDLE handle,
    LPCVOID buffer,
    DWORD bytes_to_write,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    NesysMessageHeader header{};
    if (!parse_nesys_message(buffer, bytes_to_write, &header) ||
        !is_card_command(header.command)) {
        return;
    }

    PipeEmission failure{};
    {
        ExclusiveLock lock{g_state_lock};
        auto* pipe = find_pipe_locked(handle);
        if (pipe == nullptr) {
            return;
        }
        pipe->pending = {
            .active = true,
            .sequence =
                g_pipe_sequence.fetch_add(1, std::memory_order_relaxed) + 1,
            .started_ms = started_ms,
            .write_finished_ms = finished_ms,
            .write_ms = finished_ms >= started_ms
                ? finished_ms - started_ms
                : 0,
            .command = header.command,
            .payload_size = header.payload_size,
            .total_size = header.total_size,
            .thread_id = GetCurrentThreadId(),
            .write_result = result,
            .write_error = result ? ERROR_SUCCESS : last_error,
        };
        if (!result && last_error != ERROR_IO_PENDING) {
            failure = {
                .emit = true,
                .endpoint = pipe->endpoint,
                .message = pipe->pending,
                .terminal_stage = "write",
                .terminal_result = result,
                .terminal_error = last_error,
                .terminal_ms = pipe->pending.write_ms,
                .total_ms = pipe->pending.write_ms,
            };
            pipe->pending = {};
        }
    }
    log_pipe_emission(failure);
}

void observe_pipe_flush(
    HANDLE handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    PipeEmission event{};
    {
        ExclusiveLock lock{g_state_lock};
        auto* pipe = find_pipe_locked(handle);
        if (pipe == nullptr || !pipe->pending.active) {
            return;
        }
        const auto pending = pipe->pending;
        event = {
            .emit = true,
            .endpoint = pipe->endpoint,
            .message = pending,
            .terminal_stage = "flush",
            .terminal_result = result,
            .terminal_error = result ? ERROR_SUCCESS : last_error,
            .wait_ms = started_ms >= pending.write_finished_ms
                ? started_ms - pending.write_finished_ms
                : 0,
            .terminal_ms = finished_ms >= started_ms
                ? finished_ms - started_ms
                : 0,
            .total_ms = finished_ms >= pending.started_ms
                ? finished_ms - pending.started_ms
                : 0,
        };
        pipe->pending = {};
    }
    log_pipe_emission(event);
}

void forget_pipe(HANDLE handle) noexcept {
    PipeEmission event{};
    {
        ExclusiveLock lock{g_state_lock};
        auto* pipe = find_pipe_locked(handle);
        if (pipe == nullptr) {
            return;
        }
        if (pipe->pending.active) {
            const auto now = MonotonicMilliseconds();
            event = {
                .emit = true,
                .endpoint = pipe->endpoint,
                .message = pipe->pending,
                .terminal_stage = "close_without_flush",
                .terminal_result = FALSE,
                .terminal_error = ERROR_OPERATION_ABORTED,
                .wait_ms = now >= pipe->pending.write_finished_ms
                    ? now - pipe->pending.write_finished_ms
                    : 0,
                .total_ms = now >= pipe->pending.started_ms
                    ? now - pipe->pending.started_ms
                    : 0,
            };
        }
        *pipe = {};
    }
    log_pipe_emission(event);
}

VendorLogAggregate snapshot_aggregate(
    const VendorLogAggregateState& state,
    std::uint64_t finished_ms) noexcept {
    return {
        .emit = state.records != 0,
        .window_ms = finished_ms >= state.started_ms
            ? finished_ms - state.started_ms
            : 0,
        .records = state.records,
        .bytes = state.bytes,
        .api_ms = state.api_ms,
        .total_ms = state.total_ms,
        .max_ms = state.max_ms,
        .slow_records = state.slow_records,
        .failures = state.failures,
        .suppressed_slow = state.suppressed_slow,
    };
}

VendorLogEmission process_log_record_locked(
    const VendorLogRecord& record,
    std::uint64_t finished_ms) noexcept {
    VendorLogEmission emission{};
    if (g_log_aggregate.records != 0 &&
        finished_ms >= g_log_aggregate.started_ms &&
        finished_ms - g_log_aggregate.started_ms >=
            kLogAggregateWindowMs) {
        emission.aggregate =
            snapshot_aggregate(g_log_aggregate, finished_ms);
        g_log_aggregate = {};
    }
    if (g_log_aggregate.records == 0) {
        g_log_aggregate.started_ms = finished_ms;
    }

    const std::uint64_t api_ms = record.open_ms + record.seek_ms +
        record.write_ms + record.close_ms;
    const std::uint64_t max_stage_ms = std::max(
        std::max(record.open_ms, record.seek_ms),
        std::max(record.write_ms, record.close_ms));
    const bool slow = record.failed ||
        record.total_ms >= kSlowLogRecordMs ||
        max_stage_ms >= kSlowLogRecordMs;

    if (g_slow_rate_started_ms == 0 ||
        finished_ms < g_slow_rate_started_ms ||
        finished_ms - g_slow_rate_started_ms >=
            kSlowLogRateWindowMs) {
        g_slow_rate_started_ms = finished_ms;
        g_slow_rate_emitted = 0;
    }
    bool suppressed = false;
    if (slow) {
        if (g_slow_rate_emitted < kSlowLogRateLimit) {
            ++g_slow_rate_emitted;
            emission.emit_record = true;
            emission.record = record;
        } else {
            suppressed = true;
        }
    }

    ++g_log_aggregate.records;
    g_log_aggregate.bytes += record.bytes;
    g_log_aggregate.api_ms += api_ms;
    g_log_aggregate.total_ms += record.total_ms;
    g_log_aggregate.max_ms =
        std::max(g_log_aggregate.max_ms, record.total_ms);
    g_log_aggregate.slow_records += slow ? 1 : 0;
    g_log_aggregate.failures += record.failed ? 1 : 0;
    g_log_aggregate.suppressed_slow += suppressed ? 1 : 0;
    return emission;
}

void observe_vendor_log_open(
    LPCSTR path,
    HANDLE handle,
    std::uint64_t started_ms,
    std::uint64_t finished_ms,
    DWORD last_error) noexcept {
    VendorLogEmission emission{};
    {
        ExclusiveLock lock{g_state_lock};
        if (!g_vendor_log_path_emitted) {
            g_vendor_log_path_emitted = true;
            emission.emit_path = true;
            copy_ascii(emission.first_path, path);
        }
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
            VendorLogRecord failure{};
            failure.sequence =
                g_vendor_log_sequence.fetch_add(
                    1,
                    std::memory_order_relaxed) + 1;
            failure.total_ms = finished_ms >= started_ms
                ? finished_ms - started_ms
                : 0;
            failure.open_ms = failure.total_ms;
            failure.failed = true;
            failure.error = last_error;
            copy_file_name(failure.file, path);
            const auto failure_emission =
                process_log_record_locked(failure, finished_ms);
            emission.emit_record = failure_emission.emit_record;
            emission.record = failure_emission.record;
            emission.aggregate = failure_emission.aggregate;
        } else {
            VendorLogHandleState* entry = nullptr;
            for (auto& candidate : g_vendor_log_handles) {
                if (candidate.handle == nullptr) {
                    entry = &candidate;
                    break;
                }
            }
            if (entry != nullptr) {
                *entry = {
                    .handle = handle,
                    .sequence =
                        g_vendor_log_sequence.fetch_add(
                            1,
                            std::memory_order_relaxed) + 1,
                    .started_ms = started_ms,
                    .open_ms = finished_ms >= started_ms
                        ? finished_ms - started_ms
                        : 0,
                };
                copy_file_name(entry->file, path);
            }
        }
    }
    log_vendor_log_emission(emission);
}

void observe_vendor_log_seek(
    HANDLE handle,
    DWORD result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    ExclusiveLock lock{g_state_lock};
    auto* entry = find_vendor_log_locked(handle);
    if (entry == nullptr) {
        return;
    }
    entry->seek_ms += finished_ms >= started_ms
        ? finished_ms - started_ms
        : 0;
    if (result == INVALID_SET_FILE_POINTER &&
        last_error != ERROR_SUCCESS) {
        mark_failure(entry->failed, entry->error, last_error);
    }
}

void observe_vendor_log_write(
    HANDLE handle,
    DWORD requested,
    DWORD transferred,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    ExclusiveLock lock{g_state_lock};
    auto* entry = find_vendor_log_locked(handle);
    if (entry == nullptr) {
        return;
    }
    entry->write_ms += finished_ms >= started_ms
        ? finished_ms - started_ms
        : 0;
    ++entry->write_calls;
    entry->bytes += result ? transferred : requested;
    if (!result) {
        mark_failure(entry->failed, entry->error, last_error);
    }
}

void observe_vendor_log_close(
    HANDLE handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    VendorLogEmission emission{};
    {
        ExclusiveLock lock{g_state_lock};
        auto* entry = find_vendor_log_locked(handle);
        if (entry == nullptr) {
            return;
        }
        VendorLogRecord record{
            .sequence = entry->sequence,
            .total_ms = finished_ms >= entry->started_ms
                ? finished_ms - entry->started_ms
                : 0,
            .open_ms = entry->open_ms,
            .seek_ms = entry->seek_ms,
            .write_ms = entry->write_ms,
            .close_ms = finished_ms >= started_ms
                ? finished_ms - started_ms
                : 0,
            .write_calls = entry->write_calls,
            .bytes = entry->bytes,
            .failed = entry->failed || !result,
            .error = entry->error,
            .file = entry->file,
        };
        if (!result) {
            mark_failure(record.failed, record.error, last_error);
        }
        *entry = {};
        emission = process_log_record_locked(record, finished_ms);
    }
    log_vendor_log_emission(emission);
}

void observe_http_open(
    HINTERNET handle,
    BOOL success,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    const auto duration = finished_ms >= started_ms
        ? finished_ms - started_ms
        : 0;
    if (handle != nullptr) {
        ExclusiveLock lock{g_state_lock};
        for (auto& entry : g_http_sessions) {
            if (entry.handle == nullptr) {
                entry = {
                    .handle = handle,
                    .started_ms = started_ms,
                    .open_ms = duration,
                    .failed = !success,
                    .error = success ? ERROR_SUCCESS : last_error,
                };
                break;
            }
        }
    }
    log_http_stage("WinHttpOpen", duration, success != FALSE, last_error);
}

void observe_http_timeouts(
    HINTERNET handle,
    int resolve_timeout,
    int connect_timeout,
    int send_timeout,
    int receive_timeout,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    ExclusiveLock lock{g_state_lock};
    auto* session = find_session_locked(handle);
    if (session == nullptr) {
        return;
    }
    session->timeout_ms += finished_ms >= started_ms
        ? finished_ms - started_ms
        : 0;
    session->resolve_timeout = resolve_timeout;
    session->connect_timeout = connect_timeout;
    session->send_timeout = send_timeout;
    session->receive_timeout = receive_timeout;
    if (!result) {
        mark_failure(session->failed, session->error, last_error);
    }
}

void observe_http_connect(
    HINTERNET session_handle,
    HINTERNET connection_handle,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    const auto duration = finished_ms >= started_ms
        ? finished_ms - started_ms
        : 0;
    if (connection_handle != nullptr) {
        ExclusiveLock lock{g_state_lock};
        for (auto& entry : g_http_connections) {
            if (entry.handle == nullptr) {
                entry = {
                    .handle = connection_handle,
                    .session = session_handle,
                    .connect_ms = duration,
                };
                break;
            }
        }
    }
    log_http_stage(
        "WinHttpConnect",
        duration,
        connection_handle != nullptr,
        last_error);
}

void observe_http_open_request(
    HINTERNET connection_handle,
    LPCWSTR path,
    HINTERNET request_handle,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    if (!is_card_http_path(path)) {
        return;
    }
    HttpEmission failure{};
    {
        ExclusiveLock lock{g_state_lock};
        auto* connection = find_connection_locked(connection_handle);
        auto* session = connection != nullptr
            ? find_session_locked(connection->session)
            : nullptr;
        HttpRequestState state{
            .handle = request_handle,
            .session = session != nullptr ? session->handle : nullptr,
            .connection = connection_handle,
            .sequence =
                g_http_sequence.fetch_add(1, std::memory_order_relaxed) + 1,
            .thread_id = GetCurrentThreadId(),
            .started_ms = session != nullptr
                ? session->started_ms
                : started_ms,
            .session_open_ms = session != nullptr
                ? session->open_ms
                : 0,
            .timeout_ms = session != nullptr
                ? session->timeout_ms
                : 0,
            .connect_ms = connection != nullptr
                ? connection->connect_ms
                : 0,
            .request_open_ms = finished_ms >= started_ms
                ? finished_ms - started_ms
                : 0,
            .request_open_finished_ms = finished_ms,
            .last_api_finished_ms = finished_ms,
            .resolve_timeout = session != nullptr
                ? session->resolve_timeout
                : 0,
            .connect_timeout = session != nullptr
                ? session->connect_timeout
                : 0,
            .send_timeout = session != nullptr
                ? session->send_timeout
                : 0,
            .receive_timeout = session != nullptr
                ? session->receive_timeout
                : 0,
            .failed = request_handle == nullptr ||
                (session != nullptr && session->failed) ||
                (connection != nullptr && connection->failed),
            .error = request_handle == nullptr
                ? last_error
                : (session != nullptr && session->failed
                       ? session->error
                       : (connection != nullptr && connection->failed
                              ? connection->error
                              : ERROR_SUCCESS)),
        };
        copy_ascii_from_wide(state.path, path);
        if (request_handle != nullptr) {
            for (auto& entry : g_http_requests) {
                if (entry.handle == nullptr) {
                    entry = state;
                    return;
                }
            }
        }
        failure = {
            .emit = true,
            .request = state,
            .total_ms = finished_ms >= state.started_ms
                ? finished_ms - state.started_ms
                : 0,
        };
    }
    log_http_emission(failure);
}

void observe_http_send(
    HINTERNET handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    ExclusiveLock lock{g_state_lock};
    auto* request = find_request_locked(handle);
    if (request == nullptr) {
        return;
    }
    if (request->send_calls == 0) {
        request->pre_send_ms =
            started_ms >= request->request_open_finished_ms
            ? started_ms - request->request_open_finished_ms
            : 0;
    }
    ++request->send_calls;
    request->send_ms += finished_ms >= started_ms
        ? finished_ms - started_ms
        : 0;
    request->send_finished_ms = finished_ms;
    request->last_api_finished_ms = finished_ms;
    if (!result) {
        mark_failure(request->failed, request->error, last_error);
    }
}

void observe_http_receive(
    HINTERNET handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    ExclusiveLock lock{g_state_lock};
    auto* request = find_request_locked(handle);
    if (request == nullptr) {
        return;
    }
    if (request->receive_calls == 0) {
        request->pre_receive_ms =
            started_ms >= request->send_finished_ms
            ? started_ms - request->send_finished_ms
            : 0;
    }
    ++request->receive_calls;
    request->receive_ms += finished_ms >= started_ms
        ? finished_ms - started_ms
        : 0;
    request->last_api_finished_ms = finished_ms;
    if (!result) {
        mark_failure(request->failed, request->error, last_error);
    }
}

void observe_http_query(
    HINTERNET handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    ExclusiveLock lock{g_state_lock};
    auto* request = find_request_locked(handle);
    if (request == nullptr) {
        return;
    }
    ++request->query_calls;
    request->query_ms += finished_ms >= started_ms
        ? finished_ms - started_ms
        : 0;
    request->last_api_finished_ms = finished_ms;
    if (!result) {
        mark_failure(request->failed, request->error, last_error);
    }
}

void observe_http_read(
    HINTERNET handle,
    DWORD transferred,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    ExclusiveLock lock{g_state_lock};
    auto* request = find_request_locked(handle);
    if (request == nullptr) {
        return;
    }
    ++request->read_calls;
    request->read_ms += finished_ms >= started_ms
        ? finished_ms - started_ms
        : 0;
    request->bytes += transferred;
    request->last_api_finished_ms = finished_ms;
    if (!result) {
        mark_failure(request->failed, request->error, last_error);
    }
}

void observe_http_close(
    HINTERNET handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    HttpEmission emission{};
    {
        ExclusiveLock lock{g_state_lock};
        if (auto* request = find_request_locked(handle);
            request != nullptr) {
            if (!result) {
                mark_failure(request->failed, request->error, last_error);
            }
            emission = {
                .emit = true,
                .request = *request,
                .post_receive_ms =
                    started_ms >= request->last_api_finished_ms
                    ? started_ms - request->last_api_finished_ms
                    : 0,
                .close_ms = finished_ms >= started_ms
                    ? finished_ms - started_ms
                    : 0,
                .total_ms = finished_ms >= request->started_ms
                    ? finished_ms - request->started_ms
                    : 0,
            };
            *request = {};
        } else if (auto* connection = find_connection_locked(handle);
                   connection != nullptr) {
            *connection = {};
        } else if (auto* session = find_session_locked(handle);
                   session != nullptr) {
            *session = {};
        }
    }
    log_http_emission(emission);
}
}
using namespace detail;
std::uint64_t MonotonicMilliseconds() noexcept {
    return GetTickCount64();
}

bool IsNesysPipeNameA(LPCSTR path) noexcept {
    return equals_ignore_case(path, kPipeNameA);
}

bool IsNesysPipeNameW(LPCWSTR path) noexcept {
    return equals_ignore_case(path, kPipeNameW);
}

bool IsTrackedNesysPipeHandle(HANDLE handle) noexcept {
    SharedLock lock{g_state_lock};
    return find_pipe_locked(handle) != nullptr;
}

void ObserveGamePipeOpenA(
    LPCSTR path,
    HANDLE handle,
    std::uint64_t started_ms,
    std::uint64_t finished_ms,
    DWORD last_error) noexcept {
    if (IsNesysPipeNameA(path)) {
        observe_pipe_open(
            PipeEndpoint::Game,
            handle,
            started_ms,
            finished_ms,
            last_error);
    }
}

void ObserveGamePipeOpenW(
    LPCWSTR path,
    HANDLE handle,
    std::uint64_t started_ms,
    std::uint64_t finished_ms,
    DWORD last_error) noexcept {
    if (IsNesysPipeNameW(path)) {
        observe_pipe_open(
            PipeEndpoint::Game,
            handle,
            started_ms,
            finished_ms,
            last_error);
    }
}

void ObserveGamePipeWrite(
    HANDLE handle,
    LPCVOID buffer,
    DWORD bytes_to_write,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    observe_pipe_write(
        handle,
        buffer,
        bytes_to_write,
        result,
        last_error,
        started_ms,
        finished_ms);
}

void ObserveGamePipeFlush(
    HANDLE handle,
    BOOL result,
    DWORD last_error,
    std::uint64_t started_ms,
    std::uint64_t finished_ms) noexcept {
    observe_pipe_flush(
        handle,
        result,
        last_error,
        started_ms,
        finished_ms);
}

void ObserveGameHandleClose(HANDLE handle) noexcept {
    forget_pipe(handle);
}


}

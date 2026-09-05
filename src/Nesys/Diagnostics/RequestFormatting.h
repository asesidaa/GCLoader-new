#pragma once
#include <Windows.h>
#include <WinHttp.h>
#include <array>
#include <cstdint>

namespace gc::nesys_service::diagnostics::detail {
inline constexpr std::uint64_t kSlowLogRecordMs = 100;
enum class PipeEndpoint : std::uint8_t {
    Game,
    Nesys,
};

struct PendingPipeMessage {
    bool active{false};
    std::uint64_t sequence{};
    std::uint64_t started_ms{};
    std::uint64_t write_finished_ms{};
    std::uint64_t write_ms{};
    std::uint32_t command{};
    std::uint32_t payload_size{};
    std::uint32_t total_size{};
    DWORD thread_id{};
    BOOL write_result{};
    DWORD write_error{};
};

struct PipeEmission {
    bool emit{false};
    PipeEndpoint endpoint{PipeEndpoint::Game};
    PendingPipeMessage message{};
    const char* terminal_stage{"flush"};
    BOOL terminal_result{};
    DWORD terminal_error{};
    std::uint64_t wait_ms{};
    std::uint64_t terminal_ms{};
    std::uint64_t total_ms{};
};

struct VendorLogRecord {
    std::uint64_t sequence{};
    std::uint64_t total_ms{};
    std::uint64_t open_ms{};
    std::uint64_t seek_ms{};
    std::uint64_t write_ms{};
    std::uint64_t close_ms{};
    std::uint32_t write_calls{};
    std::uint64_t bytes{};
    bool failed{false};
    DWORD error{};
    std::array<char, 96> file{};
};

struct VendorLogAggregate {
    bool emit{false};
    std::uint64_t window_ms{};
    std::uint64_t records{};
    std::uint64_t bytes{};
    std::uint64_t api_ms{};
    std::uint64_t total_ms{};
    std::uint64_t max_ms{};
    std::uint64_t slow_records{};
    std::uint64_t failures{};
    std::uint64_t suppressed_slow{};
};

struct VendorLogEmission {
    bool emit_path{false};
    std::array<char, MAX_PATH> first_path{};
    bool emit_record{false};
    VendorLogRecord record{};
    VendorLogAggregate aggregate{};
};

struct HttpRequestState {
    HINTERNET handle{};
    HINTERNET session{};
    HINTERNET connection{};
    std::uint64_t sequence{};
    DWORD thread_id{};
    std::array<char, 160> path{};
    std::uint64_t started_ms{};
    std::uint64_t session_open_ms{};
    std::uint64_t timeout_ms{};
    std::uint64_t connect_ms{};
    std::uint64_t request_open_ms{};
    std::uint64_t request_open_finished_ms{};
    std::uint64_t pre_send_ms{};
    std::uint64_t send_ms{};
    std::uint64_t send_finished_ms{};
    std::uint64_t pre_receive_ms{};
    std::uint64_t receive_ms{};
    std::uint64_t query_ms{};
    std::uint64_t read_ms{};
    std::uint64_t last_api_finished_ms{};
    std::uint32_t send_calls{};
    std::uint32_t receive_calls{};
    std::uint32_t query_calls{};
    std::uint32_t read_calls{};
    std::uint64_t bytes{};
    int resolve_timeout{};
    int connect_timeout{};
    int send_timeout{};
    int receive_timeout{};
    bool failed{false};
    DWORD error{};
};

struct HttpEmission {
    bool emit{false};
    HttpRequestState request{};
    std::uint64_t post_receive_ms{};
    std::uint64_t close_ms{};
    std::uint64_t total_ms{};
};


void log_pipe_open(
    PipeEndpoint endpoint,
    HANDLE handle,
    std::uint64_t duration_ms,
    DWORD last_error) noexcept;
void log_pipe_emission(const PipeEmission& event) noexcept;
void log_vendor_log_emission(const VendorLogEmission& emission) noexcept;
void log_http_stage(
    const char* api,
    std::uint64_t duration_ms,
    bool success,
    DWORD error) noexcept;
void log_http_emission(const HttpEmission& emission) noexcept;
}

#include "Nesys/Diagnostics/RequestFormatting.h"
#include <plog/Log.h>
#include <iomanip>

namespace gc::nesys_service::diagnostics::detail {
void log_pipe_open(
    PipeEndpoint endpoint,
    HANDLE handle,
    std::uint64_t duration_ms,
    DWORD last_error) noexcept {
    try {
        PLOG_INFO
            << "NesysPipeline: pipe_open"
            << " endpoint="
            << (endpoint == PipeEndpoint::Game ? "game" : "nesys")
            << " duration_ms=" << duration_ms
            << " result="
            << (handle != nullptr && handle != INVALID_HANDLE_VALUE
                    ? "success"
                    : "failure")
            << " error="
            << (handle != nullptr && handle != INVALID_HANDLE_VALUE
                    ? ERROR_SUCCESS
                    : last_error);
    } catch (...) {
    }
}

void log_pipe_emission(const PipeEmission& event) noexcept {
    if (!event.emit) {
        return;
    }
    const bool write_pending =
        !event.message.write_result &&
        event.message.write_error == ERROR_IO_PENDING;
    try {
        PLOG_INFO
            << "NesysPipeline: pipe_tx"
            << " endpoint="
            << (event.endpoint == PipeEndpoint::Game ? "game" : "nesys")
            << " seq=" << event.message.sequence
            << " thread=" << event.message.thread_id
            << " command=0x" << std::hex << event.message.command
            << std::dec
            << " payload_bytes=" << event.message.payload_size
            << " total_bytes=" << event.message.total_size
            << " write_ms=" << event.message.write_ms
            << " wait_ms=" << event.wait_ms
            << " " << event.terminal_stage
            << "_ms=" << event.terminal_ms
            << " total_ms=" << event.total_ms
            << " write_result="
            << (event.message.write_result
                    ? "success"
                    : (write_pending ? "pending" : "failure"))
            << " write_error=" << event.message.write_error
            << " " << event.terminal_stage << "_result="
            << (event.terminal_result ? "success" : "failure")
            << " " << event.terminal_stage
            << "_error=" << event.terminal_error;
    } catch (...) {
    }
}

void log_vendor_log_emission(const VendorLogEmission& emission) noexcept {
    try {
        if (emission.emit_path) {
            PLOG_INFO
                << "NesysPipeline: vendor_log_path"
                << " first_path=" << emission.first_path.data();
        }
        if (emission.emit_record) {
            const auto& record = emission.record;
            PLOG_WARNING
                << "NesysPipeline: vendor_log_io"
                << " seq=" << record.sequence
                << " file=" << record.file.data()
                << " total_ms=" << record.total_ms
                << " open_ms=" << record.open_ms
                << " seek_ms=" << record.seek_ms
                << " write_ms=" << record.write_ms
                << " close_ms=" << record.close_ms
                << " write_calls=" << record.write_calls
                << " bytes=" << record.bytes
                << " result="
                << (record.failed ? "failure" : "success")
                << " error=" << record.error;
        }
        if (emission.aggregate.emit) {
            const auto& aggregate = emission.aggregate;
            PLOG_INFO
                << "NesysPipeline: vendor_log_aggregate"
                << " window_ms=" << aggregate.window_ms
                << " records=" << aggregate.records
                << " bytes=" << aggregate.bytes
                << " api_ms=" << aggregate.api_ms
                << " total_ms=" << aggregate.total_ms
                << " max_ms=" << aggregate.max_ms
                << " slow_records=" << aggregate.slow_records
                << " failures=" << aggregate.failures
                << " suppressed_slow="
                << aggregate.suppressed_slow;
        }
    } catch (...) {
    }
}

void log_http_stage(
    const char* api,
    std::uint64_t duration_ms,
    bool success,
    DWORD error) noexcept {
    if (success && duration_ms < kSlowLogRecordMs) {
        return;
    }
    try {
        PLOG_WARNING
            << "NesysPipeline: http_stage"
            << " api=" << api
            << " duration_ms=" << duration_ms
            << " result=" << (success ? "success" : "failure")
            << " error=" << (success ? ERROR_SUCCESS : error);
    } catch (...) {
    }
}

void log_http_emission(const HttpEmission& emission) noexcept {
    if (!emission.emit) {
        return;
    }
    const auto& request = emission.request;
    try {
        PLOG_INFO
            << "NesysPipeline: card_http"
            << " seq=" << request.sequence
            << " thread=" << request.thread_id
            << " path=" << request.path.data()
            << " session_open_ms=" << request.session_open_ms
            << " timeout_set_ms=" << request.timeout_ms
            << " connect_ms=" << request.connect_ms
            << " request_open_ms=" << request.request_open_ms
            << " pre_send_ms=" << request.pre_send_ms
            << " send_ms=" << request.send_ms
            << " pre_receive_ms=" << request.pre_receive_ms
            << " receive_ms=" << request.receive_ms
            << " query_ms=" << request.query_ms
            << " read_ms=" << request.read_ms
            << " post_receive_ms=" << emission.post_receive_ms
            << " close_ms=" << emission.close_ms
            << " total_ms=" << emission.total_ms
            << " send_calls=" << request.send_calls
            << " receive_calls=" << request.receive_calls
            << " query_calls=" << request.query_calls
            << " read_calls=" << request.read_calls
            << " bytes=" << request.bytes
            << " timeout_resolve_ms=" << request.resolve_timeout
            << " timeout_connect_ms=" << request.connect_timeout
            << " timeout_send_ms=" << request.send_timeout
            << " timeout_receive_ms=" << request.receive_timeout
            << " result="
            << (request.failed ? "failure" : "success")
            << " error=" << request.error;
    } catch (...) {
    }
}
}

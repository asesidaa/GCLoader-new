#include "Logging/SessionLog.h"

#include "plog/Converters/NativeEOLConverter.h"
#include "plog/Converters/UTF8Converter.h"
#include "plog/Formatters/TxtFormatter.h"

#include <algorithm>
#include <limits>

namespace gc::session_log {

const wchar_t* ProcessLogFileName(
    nesys_service::ProcessRole role) noexcept {
    return role == nesys_service::ProcessRole::Service
        ? L"loader-service-log.txt"
        : L"loader-log.txt";
}

BoundedSessionFile::BoundedSessionFile(
    const wchar_t* file_name,
    std::uint64_t max_bytes) noexcept
    : max_bytes_(max_bytes) {
    file_ = CreateFileW(
        file_name,
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file_ == INVALID_HANDLE_VALUE) {
        failure_reported_ = true;
        OutputDebugStringW(
            L"GCLoader: failed to open the process session log.\n");
    }
}

BoundedSessionFile::~BoundedSessionFile() {
    plog::util::MutexLock lock(mutex_);
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
}

void BoundedSessionFile::DisableLocked(
    const wchar_t* message) noexcept {
    if (file_ != INVALID_HANDLE_VALUE) {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
    if (!failure_reported_) {
        failure_reported_ = true;
        OutputDebugStringW(message);
    }
}

bool BoundedSessionFile::WriteLocked(
    std::string_view bytes) noexcept {
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto remaining = bytes.size() - offset;
        const DWORD request = static_cast<DWORD>((std::min)(
            remaining,
            static_cast<std::size_t>(
                std::numeric_limits<DWORD>::max())));
        DWORD written = 0;
        if (WriteFile(
                file_,
                bytes.data() + offset,
                request,
                &written,
                nullptr) == FALSE ||
            written == 0) {
            DisableLocked(
                L"GCLoader: failed to write the process session log.\n");
            return false;
        }
        offset += written;
        bytes_written_ += written;
    }
    return true;
}

bool BoundedSessionFile::Write(std::string_view bytes) noexcept {
    plog::util::MutexLock lock(mutex_);
    if (file_ == INVALID_HANDLE_VALUE || capped_) {
        return false;
    }
    if (bytes.empty()) {
        return true;
    }

    const std::uint64_t remaining = bytes_written_ < max_bytes_
        ? max_bytes_ - bytes_written_
        : 0;
    if (bytes.size() <= remaining) {
        return WriteLocked(bytes);
    }

    if (kSessionLogLimitMarker.size() <= remaining) {
        WriteLocked(kSessionLogLimitMarker);
    }
    capped_ = true;
    return false;
}

SessionLogAppender::SessionLogAppender(
    const wchar_t* file_name,
    std::uint64_t max_bytes) noexcept
    : file_(file_name, max_bytes) {
}

void SessionLogAppender::write(const plog::Record& record) {
    if (formatting_failed_.load(std::memory_order_relaxed)) {
        return;
    }
    try {
        const auto message =
            plog::NativeEOLConverter<plog::UTF8Converter>::convert(
                plog::TxtFormatter::format(record));
        file_.Write({message.data(), message.size()});
    } catch (...) {
        if (!formatting_failed_.exchange(
                true,
                std::memory_order_relaxed)) {
            OutputDebugStringW(
                L"GCLoader: failed to format the process session log.\n");
        }
    }
}

} // namespace gc::session_log

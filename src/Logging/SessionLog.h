#pragma once
#include "Platform/Win32/UniqueHandle.h"

#include <Windows.h>

#include "Nesys/NesysServiceProcess.h"

#include "plog/Appenders/IAppender.h"
#include "plog/Util.h"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace gc::session_log {

inline constexpr std::uint64_t kMaxSessionLogBytes =
    100ULL * 1024ULL * 1024ULL;
inline constexpr std::string_view kSessionLogLimitMarker =
    "[GCLoader] session log limit reached; later records dropped.\r\n";

const wchar_t* ProcessLogFileName(
    nesys_service::ProcessRole role) noexcept;

class BoundedSessionFile final {
public:
    BoundedSessionFile(
        const wchar_t* file_name,
        std::uint64_t max_bytes) noexcept;
    ~BoundedSessionFile();

    BoundedSessionFile(const BoundedSessionFile&) = delete;
    BoundedSessionFile& operator=(const BoundedSessionFile&) = delete;

    bool Write(std::string_view bytes) noexcept;
    void Flush() noexcept;

private:
    bool WriteLocked(std::string_view bytes) noexcept;
    void DisableLocked(const wchar_t* message) noexcept;

    gc::platform::win32::UniqueHandle file_;
    const std::uint64_t max_bytes_;
    std::uint64_t bytes_written_{0};
    bool capped_{false};
    bool failure_reported_{false};
    plog::util::Mutex mutex_;
};

class SessionLogAppender final : public plog::IAppender {
public:
    explicit SessionLogAppender(
        const wchar_t* file_name,
        std::uint64_t max_bytes = kMaxSessionLogBytes) noexcept;

    void write(const plog::Record& record) override;
    void Flush() noexcept;

private:
    BoundedSessionFile file_;
    std::atomic_bool formatting_failed_{false};
};

void RegisterActiveProcessLogAppender(
    SessionLogAppender* appender) noexcept;
void FlushActiveProcessLog() noexcept;

} // namespace gc::session_log

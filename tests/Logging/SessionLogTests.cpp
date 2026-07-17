#include "Logging/SessionLog.h"

#include <Windows.h>

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

int expect(bool value, const char* name) {
    if (value) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

std::wstring create_temp_file_path() {
    wchar_t directory[MAX_PATH]{};
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, directory);
    if (length == 0 || length >= MAX_PATH ||
        GetTempFileNameW(directory, L"gcl", 0, path) == 0) {
        return {};
    }
    return path;
}

bool write_raw_file(const std::wstring& path, std::string_view bytes) {
    const HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL result = WriteFile(
        file,
        bytes.data(),
        static_cast<DWORD>(bytes.size()),
        &written,
        nullptr);
    CloseHandle(file);
    return result != FALSE && written == bytes.size();
}

std::string read_file(const std::wstring& path) {
    std::ifstream input(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
}

} // namespace

int main() {
    using gc::nesys_service::ProcessRole;
    using namespace gc::session_log;

    int failures = 0;
    failures += expect(
        std::wstring_view{ProcessLogFileName(ProcessRole::Game)} ==
            L"loader-log.txt",
        "game log filename");
    failures += expect(
        std::wstring_view{ProcessLogFileName(ProcessRole::Service)} ==
            L"loader-service-log.txt",
        "service log filename");
    failures += expect(
        kMaxSessionLogBytes == 100ULL * 1024ULL * 1024ULL,
        "production 100 MiB limit");

    const auto path = create_temp_file_path();
    failures += expect(!path.empty(), "temporary log path");
    if (path.empty()) {
        return 1;
    }

    failures += expect(
        write_raw_file(path, "stale-session"),
        "seed stale session");
    {
        BoundedSessionFile file{path.c_str(), 1024};
        failures += expect(file.Write("first-"), "first ordered write");
        failures += expect(file.Write("second"), "second ordered write");
    }
    failures += expect(
        read_file(path) == "first-second",
        "startup truncates and writes in order");

    const std::string prefix = "prefix:";
    const std::uint64_t test_limit =
        prefix.size() + kSessionLogLimitMarker.size();
    {
        BoundedSessionFile file{path.c_str(), test_limit};
        failures += expect(file.Write(prefix), "prefix fits limit");
        failures += expect(
            !file.Write(std::string(kSessionLogLimitMarker.size() + 1, 'x')),
            "oversized record rejected");
        failures += expect(!file.Write("later"), "later record dropped");
    }
    const std::string capped = read_file(path);
    failures += expect(
        capped == prefix + std::string(kSessionLogLimitMarker),
        "one cap marker and no later record");
    failures += expect(
        capped.size() == test_limit,
        "strict byte limit not exceeded");
    failures += expect(
        capped.find(
            kSessionLogLimitMarker,
            prefix.size() + kSessionLogLimitMarker.size()) ==
            std::string::npos,
        "cap marker emitted once");

    {
        BoundedSessionFile invalid{L"", 64};
        failures += expect(
            !invalid.Write("ignored"),
            "invalid path disables writes without throwing");
    }

    DeleteFileW(path.c_str());
    return failures == 0 ? 0 : 1;
}

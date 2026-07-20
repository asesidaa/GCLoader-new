#include "Patches/TestModeTiming/SystemConfigTimingStore.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gc::test_mode_timing::ConfigEditError;
using gc::test_mode_timing::ConfigEditStage;
using gc::test_mode_timing::ConfigKey;
using gc::test_mode_timing::RewriteTimingAssignments;
using gc::test_mode_timing::SaveOutcome;
using gc::test_mode_timing::SystemConfigStage;
using gc::test_mode_timing::SystemConfigTimingStore;
using gc::test_mode_timing::TimingOffsets;
using gc::test_mode_timing::Win32FileApi;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

std::vector<std::uint8_t> Bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

void Append(std::vector<std::uint8_t>& bytes, std::string_view text) {
    bytes.insert(bytes.end(), text.begin(), text.end());
}

bool Contains(
    std::span<const std::uint8_t> bytes,
    std::string_view text) {
    return std::search(
               bytes.begin(), bytes.end(),
               text.begin(), text.end()) != bytes.end();
}

bool ContainsBytes(
    std::span<const std::uint8_t> bytes,
    std::initializer_list<std::uint8_t> expected) {
    return std::search(
               bytes.begin(), bytes.end(),
               expected.begin(), expected.end()) != bytes.end();
}

bool HasError(
    std::string_view text,
    TimingOffsets offsets,
    ConfigEditError expected) {
    const auto result = RewriteTimingAssignments(Bytes(text), offsets);
    return !result && result.error() == expected;
}

enum class FailPoint {
    None,
    TargetOpen,
    TargetSize,
    TargetRead,
    TargetShortRead,
    TargetClose,
    TempCreate,
    TempWrite,
    TempZeroWrite,
    TempFlush,
    TempClose,
    Replace,
};

struct FakeFileState {
    std::vector<std::uint8_t> target_bytes;
    std::vector<std::uint8_t> temp_bytes;
    FailPoint fail_point{};
    DWORD last_error{ERROR_SUCCESS};
    DWORD process_id{4321};
    unsigned file_exists_failures{};
    bool partial_writes{};
    bool fail_cleanup{};
    bool target_open{};
    bool temp_open{};
    bool temp_exists{};
    unsigned temp_create_calls{};
    unsigned write_calls{};
    unsigned flush_calls{};
    unsigned replace_calls{};
    unsigned delete_calls{};
    DWORD replace_flags{};
    std::vector<std::wstring> temp_paths;
};

FakeFileState* g_fake{};

HANDLE TargetHandle() {
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(1));
}

HANDLE TempHandle() {
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(2));
}

HANDLE WINAPI FakeCreateFileW(
    LPCWSTR path,
    DWORD desired_access,
    DWORD,
    LPSECURITY_ATTRIBUTES,
    DWORD creation_disposition,
    DWORD,
    HANDLE) {
    if (desired_access == GENERIC_READ &&
        creation_disposition == OPEN_EXISTING) {
        if (g_fake->fail_point == FailPoint::TargetOpen) {
            g_fake->last_error = ERROR_ACCESS_DENIED;
            return INVALID_HANDLE_VALUE;
        }
        g_fake->target_open = true;
        return TargetHandle();
    }

    ++g_fake->temp_create_calls;
    g_fake->temp_paths.emplace_back(path);
    if (g_fake->file_exists_failures != 0) {
        --g_fake->file_exists_failures;
        g_fake->last_error = ERROR_FILE_EXISTS;
        return INVALID_HANDLE_VALUE;
    }
    if (g_fake->fail_point == FailPoint::TempCreate) {
        g_fake->last_error = ERROR_ACCESS_DENIED;
        return INVALID_HANDLE_VALUE;
    }
    g_fake->temp_bytes.clear();
    g_fake->temp_open = true;
    g_fake->temp_exists = true;
    return TempHandle();
}

BOOL WINAPI FakeGetFileSizeEx(HANDLE handle, PLARGE_INTEGER size) {
    if (handle != TargetHandle()) {
        g_fake->last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    if (g_fake->fail_point == FailPoint::TargetSize) {
        g_fake->last_error = ERROR_READ_FAULT;
        return FALSE;
    }
    size->QuadPart = static_cast<LONGLONG>(g_fake->target_bytes.size());
    return TRUE;
}

BOOL WINAPI FakeReadFile(
    HANDLE handle,
    LPVOID buffer,
    DWORD bytes_to_read,
    LPDWORD bytes_read,
    LPOVERLAPPED) {
    if (handle != TargetHandle()) {
        g_fake->last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    if (g_fake->fail_point == FailPoint::TargetRead) {
        g_fake->last_error = ERROR_READ_FAULT;
        return FALSE;
    }
    DWORD count = bytes_to_read;
    if (g_fake->fail_point == FailPoint::TargetShortRead && count != 0) {
        --count;
    }
    if (count != 0) {
        std::memcpy(buffer, g_fake->target_bytes.data(), count);
    }
    *bytes_read = count;
    return TRUE;
}

BOOL WINAPI FakeWriteFile(
    HANDLE handle,
    LPCVOID buffer,
    DWORD bytes_to_write,
    LPDWORD bytes_written,
    LPOVERLAPPED) {
    ++g_fake->write_calls;
    if (handle != TempHandle()) {
        g_fake->last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    if (g_fake->fail_point == FailPoint::TempWrite) {
        g_fake->last_error = ERROR_WRITE_FAULT;
        return FALSE;
    }
    if (g_fake->fail_point == FailPoint::TempZeroWrite) {
        *bytes_written = 0;
        return TRUE;
    }
    const DWORD count = g_fake->partial_writes
        ? std::min<DWORD>(3, bytes_to_write)
        : bytes_to_write;
    const auto* first = static_cast<const std::uint8_t*>(buffer);
    g_fake->temp_bytes.insert(
        g_fake->temp_bytes.end(), first, first + count);
    *bytes_written = count;
    return TRUE;
}

BOOL WINAPI FakeFlushFileBuffers(HANDLE handle) {
    ++g_fake->flush_calls;
    if (handle != TempHandle()) {
        g_fake->last_error = ERROR_INVALID_HANDLE;
        return FALSE;
    }
    if (g_fake->fail_point == FailPoint::TempFlush) {
        g_fake->last_error = ERROR_WRITE_FAULT;
        return FALSE;
    }
    return TRUE;
}

BOOL WINAPI FakeCloseHandle(HANDLE handle) {
    if (handle == TargetHandle()) {
        if (g_fake->fail_point == FailPoint::TargetClose) {
            g_fake->last_error = ERROR_INVALID_HANDLE;
            return FALSE;
        }
        g_fake->target_open = false;
        return TRUE;
    }
    if (handle == TempHandle()) {
        if (g_fake->fail_point == FailPoint::TempClose) {
            g_fake->last_error = ERROR_INVALID_HANDLE;
            return FALSE;
        }
        g_fake->temp_open = false;
        return TRUE;
    }
    g_fake->last_error = ERROR_INVALID_HANDLE;
    return FALSE;
}

BOOL WINAPI FakeReplaceFileW(
    LPCWSTR,
    LPCWSTR,
    LPCWSTR,
    DWORD flags,
    LPVOID,
    LPVOID) {
    ++g_fake->replace_calls;
    g_fake->replace_flags = flags;
    if (g_fake->fail_point == FailPoint::Replace) {
        g_fake->last_error = ERROR_ACCESS_DENIED;
        return FALSE;
    }
    g_fake->target_bytes = g_fake->temp_bytes;
    g_fake->temp_exists = false;
    return TRUE;
}

BOOL WINAPI FakeDeleteFileW(LPCWSTR) {
    ++g_fake->delete_calls;
    if (g_fake->fail_cleanup) {
        g_fake->last_error = ERROR_SHARING_VIOLATION;
        return FALSE;
    }
    g_fake->temp_exists = false;
    return TRUE;
}

DWORD WINAPI FakeGetLastError() {
    return g_fake->last_error;
}

DWORD WINAPI FakeGetCurrentProcessId() {
    return g_fake->process_id;
}

Win32FileApi FakeApi() {
    return {
        .create_file = &FakeCreateFileW,
        .get_file_size = &FakeGetFileSizeEx,
        .read_file = &FakeReadFile,
        .write_file = &FakeWriteFile,
        .flush_file = &FakeFlushFileBuffers,
        .close_handle = &FakeCloseHandle,
        .replace_file = &FakeReplaceFileW,
        .delete_file = &FakeDeleteFileW,
        .get_last_error = &FakeGetLastError,
        .get_process_id = &FakeGetCurrentProcessId,
    };
}

FakeFileState BaseFakeState() {
    return {
        .target_bytes = Bytes(
            "GameTimeOffset=0\r\nJudgTimeOffset=-16\r\n"),
    };
}

} // namespace

int main() {
    int failures = 0;

    std::vector<std::uint8_t> input;
    Append(input, "/*\r\nGameTimeOffset = 99\r\n*/\r\n");
    Append(input, "//JudgTimeOffset=99\r\n");
    input.insert(input.end(), {0x83, 0x51, 0x83, 0x5B});
    Append(input, "\r\nJudgTimeOffset\t= -16\r\n");
    Append(input, "GameTimeOffset\t= 0\r\n");

    std::vector<std::uint8_t> expected;
    Append(expected, "/*\r\nGameTimeOffset = 99\r\n*/\r\n");
    Append(expected, "//JudgTimeOffset=99\r\n");
    expected.insert(expected.end(), {0x83, 0x51, 0x83, 0x5B});
    Append(expected, "\r\nJudgTimeOffset\t= 12\r\n");
    Append(expected, "GameTimeOffset\t= -5\r\n");

    const auto rewritten = RewriteTimingAssignments(
        input, {.game_ms = -5, .judge_ms = 12});
    failures += Expect(rewritten.has_value(), "valid assignments rewrite");
    if (rewritten) {
        failures += Expect(rewritten->changed, "different values report changed");
        failures += Expect(
            rewritten->bytes == expected,
            "only active numeric tokens change");
        failures += Expect(
            Contains(rewritten->bytes, "GameTimeOffset\t= -5\r\n"),
            "game token changes sign and width");
        failures += Expect(
            Contains(rewritten->bytes, "JudgTimeOffset\t= 12\r\n"),
            "judge token changes sign and width");
        failures += Expect(
            ContainsBytes(rewritten->bytes, {0x83, 0x51, 0x83, 0x5B}),
            "Shift-JIS bytes are retained");
        failures += Expect(
            Contains(rewritten->bytes, "GameTimeOffset = 99\r\n"),
            "block-comment assignment is ignored");
        failures += Expect(
            Contains(rewritten->bytes, "//JudgTimeOffset=99\r\n"),
            "line-comment assignment is ignored");
    }

    const auto unchanged = RewriteTimingAssignments(
        input, {.game_ms = 0, .judge_ms = -16});
    failures += Expect(
        unchanged && !unchanged->changed && unchanged->bytes == input,
        "unchanged rewrite is byte identical");

    const auto semantic_input = Bytes(
        "GameTimeOffset = +0\r\nJudgTimeOffset = -016\r\n");
    const auto semantic_unchanged = RewriteTimingAssignments(
        semantic_input, {.game_ms = 0, .judge_ms = -16});
    failures += Expect(
        semantic_unchanged && !semantic_unchanged->changed &&
            semantic_unchanged->bytes == semantic_input,
        "equivalent signed tokens remain byte identical");

    std::vector<std::uint8_t> bom_input{0xEF, 0xBB, 0xBF};
    Append(bom_input, "GameTimeOffset=0\r\nJudgTimeOffset=-16\r\n");
    std::vector<std::uint8_t> bom_expected{0xEF, 0xBB, 0xBF};
    Append(bom_expected, "GameTimeOffset=1\r\nJudgTimeOffset=-15\r\n");
    const auto bom_rewritten = RewriteTimingAssignments(
        bom_input, {.game_ms = 1, .judge_ms = -15});
    failures += Expect(
        bom_rewritten && bom_rewritten->changed &&
            bom_rewritten->bytes == bom_expected,
        "UTF-8 BOM is preserved before the first assignment");

    const auto comments_input = Bytes(
        "GameTimeOffsetBackup = 77\n"
        "  /* prefix */ GameTimeOffset = +4 /* keep */\n"
        "\tJudgTimeOffset\t=\t-3 // keep too\n"
        "JudgTimeOffsetSuffix = 88\n");
    const auto comments_unchanged = RewriteTimingAssignments(
        comments_input, {.game_ms = 4, .judge_ms = -3});
    failures += Expect(
        comments_unchanged && !comments_unchanged->changed &&
            comments_unchanged->bytes == comments_input,
        "LF comments whitespace and similar keys are preserved");

    failures += Expect(
        HasError(
            "JudgTimeOffset=0\n",
            {},
            {ConfigEditStage::Missing, ConfigKey::GameTimeOffset}),
        "missing game assignment is typed");
    failures += Expect(
        HasError(
            "GameTimeOffset=0\n",
            {},
            {ConfigEditStage::Missing, ConfigKey::JudgTimeOffset}),
        "missing judge assignment is typed");
    failures += Expect(
        HasError(
            "GameTimeOffset=0\nGameTimeOffset=1\nJudgTimeOffset=0\n",
            {},
            {ConfigEditStage::Duplicate, ConfigKey::GameTimeOffset}),
        "duplicate game assignment is typed");
    failures += Expect(
        HasError(
            "GameTimeOffset=0\nJudgTimeOffset=0\nJudgTimeOffset=1\n",
            {},
            {ConfigEditStage::Duplicate, ConfigKey::JudgTimeOffset}),
        "duplicate judge assignment is typed");
    failures += Expect(
        HasError(
            "GameTimeOffset=+\nJudgTimeOffset=0\n",
            {},
            {ConfigEditStage::Malformed, ConfigKey::GameTimeOffset}),
        "sign without digits is malformed");
    failures += Expect(
        HasError(
            "GameTimeOffset=12x\nJudgTimeOffset=0\n",
            {},
            {ConfigEditStage::Malformed, ConfigKey::GameTimeOffset}),
        "numeric suffix is malformed");
    failures += Expect(
        HasError(
            "GameTimeOffset 12\nJudgTimeOffset=0\n",
            {},
            {ConfigEditStage::Malformed, ConfigKey::GameTimeOffset}),
        "missing equals is malformed");
    failures += Expect(
        HasError(
            "GameTimeOffset=2147483648\nJudgTimeOffset=0\n",
            {},
            {ConfigEditStage::Malformed, ConfigKey::GameTimeOffset}),
        "positive integer overflow is malformed");
    failures += Expect(
        HasError(
            "GameTimeOffset=0\nJudgTimeOffset=-2147483649\n",
            {},
            {ConfigEditStage::Malformed, ConfigKey::JudgTimeOffset}),
        "negative integer overflow is malformed");

    struct FailureCase {
        FailPoint point;
        SystemConfigStage stage;
    };
    constexpr std::array failure_cases{
        FailureCase{FailPoint::TargetOpen, SystemConfigStage::TargetOpen},
        FailureCase{FailPoint::TargetSize, SystemConfigStage::TargetSize},
        FailureCase{FailPoint::TargetRead, SystemConfigStage::TargetRead},
        FailureCase{FailPoint::TargetShortRead, SystemConfigStage::TargetRead},
        FailureCase{FailPoint::TargetClose, SystemConfigStage::TargetClose},
        FailureCase{FailPoint::TempCreate, SystemConfigStage::TempCreate},
        FailureCase{FailPoint::TempWrite, SystemConfigStage::TempWrite},
        FailureCase{FailPoint::TempZeroWrite, SystemConfigStage::TempWrite},
        FailureCase{FailPoint::TempFlush, SystemConfigStage::TempFlush},
        FailureCase{FailPoint::TempClose, SystemConfigStage::TempClose},
        FailureCase{FailPoint::Replace, SystemConfigStage::Replace},
    };
    for (const auto& test : failure_cases) {
        auto fake = BaseFakeState();
        const auto original = fake.target_bytes;
        fake.fail_point = test.point;
        g_fake = &fake;
        SystemConfigTimingStore store{L"data\\system.cfg", FakeApi()};
        const auto result = store.Save({.game_ms = 4, .judge_ms = -3});
        failures += Expect(!result, "injected file stage fails save");
        if (!result) {
            failures += Expect(
                result.error().stage == test.stage,
                "failure reports exact file stage");
        }
        failures += Expect(
            fake.target_bytes == original,
            "file failure preserves target bytes");
        failures += Expect(
            fake.replace_calls ==
                (test.point == FailPoint::Replace ? 1U : 0U),
            "replace call count is bounded");
    }

    {
        auto fake = BaseFakeState();
        fake.partial_writes = true;
        g_fake = &fake;
        SystemConfigTimingStore store{L"data\\system.cfg", FakeApi()};
        const auto result = store.Save({.game_ms = 4, .judge_ms = -3});
        failures += Expect(
            result && *result == SaveOutcome::Changed,
            "partial writes complete a changed save");
        failures += Expect(fake.write_calls > 1, "partial writes are retried");
        failures += Expect(fake.flush_calls == 1, "temp file is flushed once");
        failures += Expect(fake.replace_calls == 1, "target is replaced once");
        failures += Expect(
            fake.replace_flags == REPLACEFILE_WRITE_THROUGH,
            "replacement uses write-through");
        failures += Expect(
            Contains(fake.target_bytes, "GameTimeOffset=4\r\n") &&
                Contains(fake.target_bytes, "JudgTimeOffset=-3\r\n"),
            "successful replace publishes all bytes");
        failures += Expect(
            fake.temp_paths.size() == 1 &&
                fake.temp_paths[0].ends_with(
                    L"system.cfg.gcloader.4321.0.tmp"),
            "temporary file name is same-target derived");
    }

    {
        auto fake = BaseFakeState();
        fake.file_exists_failures = 2;
        g_fake = &fake;
        SystemConfigTimingStore store{L"data\\system.cfg", FakeApi()};
        const auto result = store.Save({.game_ms = 4, .judge_ms = -3});
        failures += Expect(
            result && *result == SaveOutcome::Changed,
            "file-exists collisions are retried");
        failures += Expect(
            fake.temp_create_calls == 3 && fake.temp_paths.size() == 3 &&
                fake.temp_paths[2].ends_with(
                    L"system.cfg.gcloader.4321.2.tmp"),
            "collision advances temporary attempt number");
    }

    {
        auto fake = BaseFakeState();
        g_fake = &fake;
        SystemConfigTimingStore store{L"data\\system.cfg", FakeApi()};
        const auto result = store.Save({.game_ms = 0, .judge_ms = -16});
        failures += Expect(
            result && *result == SaveOutcome::Unchanged,
            "matching values report unchanged");
        failures += Expect(
            fake.temp_create_calls == 0 && fake.write_calls == 0 &&
                fake.flush_calls == 0 && fake.replace_calls == 0 &&
                fake.delete_calls == 0,
            "unchanged save performs no temporary operations");
    }

    {
        auto fake = BaseFakeState();
        fake.fail_point = FailPoint::Replace;
        fake.fail_cleanup = true;
        g_fake = &fake;
        SystemConfigTimingStore store{L"data\\system.cfg", FakeApi()};
        const auto result = store.Save({.game_ms = 4, .judge_ms = -3});
        failures += Expect(!result, "replace plus cleanup failure is reported");
        if (!result) {
            failures += Expect(
                result.error().stage == SystemConfigStage::Replace &&
                    result.error().win32_error == ERROR_ACCESS_DENIED &&
                    result.error().cleanup_error == ERROR_SHARING_VIOLATION,
                "primary and cleanup errors are preserved");
        }
    }

    {
        const auto directory = std::filesystem::temp_directory_path() /
            (L"gcloader-timing-store-" +
             std::to_wstring(::GetCurrentProcessId()));
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::filesystem::create_directories(directory);
        const auto path = directory / L"system.cfg";
        {
            std::ofstream output(path, std::ios::binary);
            output << "GameTimeOffset=0\r\nJudgTimeOffset=-16\r\n";
        }
        SystemConfigTimingStore store{path};
        const auto result = store.Save({.game_ms = 7, .judge_ms = -8});
        std::ifstream input_file(path, std::ios::binary);
        const std::string saved{
            std::istreambuf_iterator<char>{input_file},
            std::istreambuf_iterator<char>{}};
        failures += Expect(
            result && *result == SaveOutcome::Changed,
            "production Win32 store replaces a real temporary target");
        failures += Expect(
            saved == "GameTimeOffset=7\r\nJudgTimeOffset=-8\r\n",
            "real replacement contains exact rewritten bytes");
        std::filesystem::remove_all(directory, ignored);
    }

    return failures == 0 ? 0 : 1;
}

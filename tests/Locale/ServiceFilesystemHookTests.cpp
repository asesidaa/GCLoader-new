#include "Locale/ServiceFilesystemHooks.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <expected>
#include <iostream>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gc::locale_compatibility::AnsiFilesystemApi;
using gc::locale_compatibility::FilesystemDiagnosticActions;
using gc::locale_compatibility::FilesystemDiagnosticRole;
using gc::locale_compatibility::FilesystemDiagnostics;
using gc::locale_compatibility::OriginalServiceFilesystemApi;
using gc::locale_compatibility::WideProbeOutcome;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

enum class Operation {
    original,
    probe,
    emit,
};

struct Capture {
    std::vector<Operation> operations;
    std::vector<std::string> lines;
    LPCSTR first_path{};
    LPCSTR second_path{};
    HANDLE handle{};
    DWORD first_dword{};
    DWORD second_dword{};
    DWORD third_dword{};
    DWORD fourth_dword{};
    LPSECURITY_ATTRIBUTES security{};
    HANDLE template_file{};
    LPWIN32_FIND_DATAA find_data{};
    BOOL flag{};
    HANDLE handle_result{INVALID_HANDLE_VALUE};
    DWORD dword_result{INVALID_FILE_ATTRIBUTES};
    BOOL bool_result{FALSE};
    DWORD original_error{ERROR_ACCESS_DENIED};
    std::string returned_name;
    std::wstring first_probe;
    std::wstring second_probe;

    void Record(Operation operation) {
        operations.push_back(operation);
    }

    void ResetObservation() {
        operations.clear();
        first_probe.clear();
        second_probe.clear();
    }

    static WideProbeOutcome Probe(
        void* context,
        AnsiFilesystemApi,
        std::wstring_view first,
        std::wstring_view second) noexcept {
        auto& capture = *static_cast<Capture*>(context);
        try {
            capture.Record(Operation::probe);
            capture.first_probe.assign(first);
            capture.second_probe.assign(second);
        } catch (...) {
        }
        SetLastError(ERROR_BAD_PATHNAME);
        return WideProbeOutcome::exists;
    }

    static void Emit(void* context, std::string_view line) noexcept {
        auto& capture = *static_cast<Capture*>(context);
        try {
            capture.Record(Operation::emit);
            capture.lines.emplace_back(line);
        } catch (...) {
        }
        SetLastError(ERROR_WRITE_FAULT);
    }

    FilesystemDiagnosticActions Actions() noexcept {
        return {
            .context = this,
            .probe = &Probe,
            .emit = &Emit,
        };
    }
};

Capture* g_capture{};

void RecordOriginal() {
    g_capture->Record(Operation::original);
    SetLastError(g_capture->original_error);
}

HANDLE WINAPI FakeCreateFileA(
    LPCSTR path,
    DWORD desired_access,
    DWORD share_mode,
    LPSECURITY_ATTRIBUTES security,
    DWORD disposition,
    DWORD flags,
    HANDLE template_file) {
    g_capture->first_path = path;
    g_capture->first_dword = desired_access;
    g_capture->second_dword = share_mode;
    g_capture->security = security;
    g_capture->third_dword = disposition;
    g_capture->fourth_dword = flags;
    g_capture->template_file = template_file;
    RecordOriginal();
    return g_capture->handle_result;
}

DWORD WINAPI FakeGetFileAttributesA(LPCSTR path) {
    g_capture->first_path = path;
    RecordOriginal();
    return g_capture->dword_result;
}

HANDLE WINAPI FakeFindFirstFileA(
    LPCSTR path,
    LPWIN32_FIND_DATAA data) {
    g_capture->first_path = path;
    g_capture->find_data = data;
    RecordOriginal();
    return g_capture->handle_result;
}

BOOL WINAPI FakeFindNextFileA(
    HANDLE find,
    LPWIN32_FIND_DATAA data) {
    g_capture->handle = find;
    g_capture->find_data = data;
    if (g_capture->bool_result != FALSE && data != nullptr) {
        strcpy_s(data->cFileName, g_capture->returned_name.c_str());
    }
    RecordOriginal();
    return g_capture->bool_result;
}

BOOL WINAPI FakeCreateDirectoryA(
    LPCSTR path,
    LPSECURITY_ATTRIBUTES security) {
    g_capture->first_path = path;
    g_capture->security = security;
    RecordOriginal();
    return g_capture->bool_result;
}

BOOL WINAPI FakeDeleteFileA(LPCSTR path) {
    g_capture->first_path = path;
    RecordOriginal();
    return g_capture->bool_result;
}

BOOL WINAPI FakeMoveFileA(LPCSTR first, LPCSTR second) {
    g_capture->first_path = first;
    g_capture->second_path = second;
    RecordOriginal();
    return g_capture->bool_result;
}

BOOL WINAPI FakeCopyFileA(
    LPCSTR first,
    LPCSTR second,
    BOOL fail_if_exists) {
    g_capture->first_path = first;
    g_capture->second_path = second;
    g_capture->flag = fail_if_exists;
    RecordOriginal();
    return g_capture->bool_result;
}

bool OperationsAre(
    const Capture& capture,
    std::initializer_list<Operation> expected) {
    return std::ranges::equal(capture.operations, expected);
}

int TestRequestSurface() {
    using namespace gc::locale_compatibility;

    constexpr std::array<std::string_view, 8> expected_exports{
        "CreateFileA",
        "GetFileAttributesA",
        "FindFirstFileA",
        "FindNextFileA",
        "CreateDirectoryA",
        "DeleteFileA",
        "MoveFileA",
        "CopyFileA",
    };
    static_assert(kServiceFilesystemHookCount == expected_exports.size());
    static_assert(std::same_as<
        decltype(InstallServiceFilesystemDiagnostics()),
        std::expected<void, gc::win32_hooks::HookInstallError>>);

    OriginalServiceFilesystemApi originals{};
    const auto requests = BuildServiceFilesystemHookRequests(&originals);

    int failures = 0;
    failures += Expect(requests.size() == expected_exports.size(),
        "service filesystem request count is exact");
    for (std::size_t index = 0; index < requests.size(); ++index) {
        failures += Expect(
            requests[index].module_name != nullptr &&
                std::wstring_view{requests[index].module_name} ==
                    L"kernel32.dll" &&
                requests[index].export_name != nullptr &&
                std::string_view{requests[index].export_name} ==
                    expected_exports[index] &&
                requests[index].detour != nullptr &&
                requests[index].original != nullptr,
            "service filesystem request identity is exact");
    }
    failures += Expect(
        std::ranges::all_of(
            requests,
            [&requests](const auto& candidate) {
                return std::ranges::count_if(
                    requests,
                    [&candidate](const auto& other) {
                        return std::string_view{candidate.export_name} ==
                            std::string_view{other.export_name};
                    }) == 1 &&
                    std::ranges::count_if(
                        requests,
                        [&candidate](const auto& other) {
                            return candidate.original == other.original;
                        }) == 1;
            }),
        "service exports and original slots are unique");
    return failures;
}

int TestCreateFileForwarding() {
    Capture capture;
    g_capture = &capture;
    FilesystemDiagnostics diagnostics{
        FilesystemDiagnosticRole::service,
        capture.Actions()};
    constexpr char path[] = "data\\missing.bin";
    auto* security = reinterpret_cast<LPSECURITY_ATTRIBUTES>(0x3100);
    const auto template_file = reinterpret_cast<HANDLE>(0x3200);
    capture.handle_result = INVALID_HANDLE_VALUE;
    capture.original_error = ERROR_ACCESS_DENIED;

    SetLastError(ERROR_RETRY);
    const auto result =
        gc::locale_compatibility::detail::InvokeCreateFileA(
            path,
            GENERIC_READ,
            FILE_SHARE_READ,
            security,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            template_file,
            &FakeCreateFileA,
            &diagnostics);

    int failures = 0;
    failures += Expect(
        result == INVALID_HANDLE_VALUE &&
            capture.first_path == path &&
            capture.first_dword == GENERIC_READ &&
            capture.second_dword == FILE_SHARE_READ &&
            capture.security == security &&
            capture.third_dword == OPEN_EXISTING &&
            capture.fourth_dword == FILE_ATTRIBUTE_NORMAL &&
            capture.template_file == template_file,
        "CreateFileA forwards all seven arguments and result");
    failures += Expect(
        GetLastError() == ERROR_ACCESS_DENIED &&
            OperationsAre(capture, {
                Operation::original,
                Operation::probe,
                Operation::emit}),
        "CreateFileA observes after original and restores error");
    failures += Expect(
        capture.lines.size() == 1 &&
            capture.lines.front().find("api=create_file") !=
                std::string::npos &&
            capture.lines.front().find("error=5") !=
                std::string::npos,
        "CreateFileA failure reaches diagnostics");
    return failures;
}

int TestFindForwarding() {
    Capture capture;
    g_capture = &capture;
    FilesystemDiagnostics diagnostics{
        FilesystemDiagnosticRole::service,
        capture.Actions()};
    constexpr char pattern[] =
        "data\\" "\x89\xBC" "_*.dds";
    WIN32_FIND_DATAA data{};
    const auto find_first_result = reinterpret_cast<HANDLE>(0x4100);
    capture.handle_result = find_first_result;
    capture.original_error = ERROR_CRC;

    SetLastError(ERROR_RETRY);
    const auto first =
        gc::locale_compatibility::detail::InvokeFindFirstFileA(
            pattern,
            &data,
            &FakeFindFirstFileA,
            &diagnostics);

    int failures = 0;
    failures += Expect(
        first == find_first_result && capture.first_path == pattern &&
            capture.find_data == &data && GetLastError() == ERROR_CRC &&
            OperationsAre(capture, {
                Operation::original,
                Operation::probe,
                Operation::emit}),
        "FindFirstFileA forwards pattern output and result before observing");
    failures += Expect(
        capture.first_probe == L"data\\\u4eee_*.dds" &&
            capture.lines.back().find("api=find_first_file") !=
                std::string::npos,
        "FindFirstFileA diagnostic receives decoded pattern");

    capture.ResetObservation();
    capture.bool_result = TRUE;
    capture.original_error = ERROR_SEEK;
    capture.returned_name = "\x89\xBC" "_start.dds";
    const auto find = reinterpret_cast<HANDLE>(0x4200);
    SetLastError(ERROR_RETRY);
    const auto next =
        gc::locale_compatibility::detail::InvokeFindNextFileA(
            find,
            &data,
            &FakeFindNextFileA,
            &diagnostics);
    failures += Expect(
        next == TRUE && capture.handle == find &&
            capture.find_data == &data &&
            std::string_view{data.cFileName} == capture.returned_name &&
            GetLastError() == ERROR_SEEK &&
            OperationsAre(capture, {
                Operation::original,
                Operation::emit}) &&
            capture.lines.back().find("api=find_next_file") !=
                std::string::npos,
        "FindNextFileA observes successful returned name after original");

    const auto lines_before_completion = capture.lines.size();
    capture.ResetObservation();
    capture.bool_result = FALSE;
    capture.original_error = ERROR_NO_MORE_FILES;
    SetLastError(ERROR_RETRY);
    const auto complete =
        gc::locale_compatibility::detail::InvokeFindNextFileA(
            find,
            &data,
            &FakeFindNextFileA,
            &diagnostics);
    failures += Expect(
        complete == FALSE && GetLastError() == ERROR_NO_MORE_FILES &&
            OperationsAre(capture, {Operation::original}) &&
            capture.lines.size() == lines_before_completion,
        "FindNextFileA completion is suppressed");
    return failures;
}

int TestMoveAndCopyForwarding() {
    Capture capture;
    g_capture = &capture;
    FilesystemDiagnostics diagnostics{
        FilesystemDiagnosticRole::service,
        capture.Actions()};
    constexpr char source[] = "data\\source.bin";
    constexpr char destination[] = "data\\destination.bin";
    capture.bool_result = FALSE;
    capture.original_error = ERROR_PATH_NOT_FOUND;

    SetLastError(ERROR_RETRY);
    const auto moved =
        gc::locale_compatibility::detail::InvokeMoveFileA(
            source,
            destination,
            &FakeMoveFileA,
            &diagnostics);

    int failures = 0;
    failures += Expect(
        moved == FALSE && capture.first_path == source &&
            capture.second_path == destination &&
            GetLastError() == ERROR_PATH_NOT_FOUND &&
            OperationsAre(capture, {
                Operation::original,
                Operation::probe,
                Operation::emit}),
        "MoveFileA forwards both paths then observes failure");

    capture.ResetObservation();
    constexpr char japanese_source[] =
        "data\\" "\x89\xBC" "_source.bin";
    capture.bool_result = TRUE;
    capture.original_error = ERROR_ALREADY_EXISTS;
    SetLastError(ERROR_RETRY);
    const auto copied =
        gc::locale_compatibility::detail::InvokeCopyFileA(
            japanese_source,
            destination,
            TRUE,
            &FakeCopyFileA,
            &diagnostics);
    failures += Expect(
        copied == TRUE && capture.first_path == japanese_source &&
            capture.second_path == destination && capture.flag == TRUE &&
            GetLastError() == ERROR_ALREADY_EXISTS &&
            OperationsAre(capture, {
                Operation::original,
                Operation::probe,
                Operation::emit}) &&
            capture.lines.back().find("api=copy_file") !=
                std::string::npos,
        "CopyFileA forwards both paths flag result and error before observing");
    return failures;
}

int TestRemainingSinglePathHelpers() {
    Capture capture;
    g_capture = &capture;
    FilesystemDiagnostics diagnostics{
        FilesystemDiagnosticRole::service,
        capture.Actions()};
    constexpr char attributes_path[] = "data\\attributes-missing.bin";
    capture.dword_result = INVALID_FILE_ATTRIBUTES;
    capture.original_error = ERROR_FILE_NOT_FOUND;
    const auto attributes =
        gc::locale_compatibility::detail::InvokeGetFileAttributesA(
            attributes_path,
            &FakeGetFileAttributesA,
            &diagnostics);

    int failures = 0;
    failures += Expect(
        attributes == INVALID_FILE_ATTRIBUTES &&
            capture.first_path == attributes_path &&
            GetLastError() == ERROR_FILE_NOT_FOUND &&
            OperationsAre(capture, {
                Operation::original,
                Operation::probe,
                Operation::emit}),
        "GetFileAttributesA helper is transparent");

    capture.ResetObservation();
    constexpr char directory[] = "data\\missing-directory";
    auto* security = reinterpret_cast<LPSECURITY_ATTRIBUTES>(0x5100);
    capture.bool_result = FALSE;
    capture.original_error = ERROR_PATH_NOT_FOUND;
    const auto created =
        gc::locale_compatibility::detail::InvokeCreateDirectoryA(
            directory,
            security,
            &FakeCreateDirectoryA,
            &diagnostics);
    failures += Expect(
        created == FALSE && capture.first_path == directory &&
            capture.security == security &&
            GetLastError() == ERROR_PATH_NOT_FOUND &&
            OperationsAre(capture, {
                Operation::original,
                Operation::probe,
                Operation::emit}),
        "CreateDirectoryA helper is transparent");

    capture.ResetObservation();
    constexpr char deleted_path[] = "data\\missing-delete.bin";
    capture.original_error = ERROR_FILE_NOT_FOUND;
    const auto deleted =
        gc::locale_compatibility::detail::InvokeDeleteFileA(
            deleted_path,
            &FakeDeleteFileA,
            &diagnostics);
    failures += Expect(
        deleted == FALSE && capture.first_path == deleted_path &&
            GetLastError() == ERROR_FILE_NOT_FOUND &&
            OperationsAre(capture, {
                Operation::original,
                Operation::probe,
                Operation::emit}),
        "DeleteFileA helper is transparent");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += TestRequestSurface();
    failures += TestCreateFileForwarding();
    failures += TestFindForwarding();
    failures += TestMoveAndCopyForwarding();
    failures += TestRemainingSinglePathHelpers();
    return failures == 0 ? 0 : 1;
}

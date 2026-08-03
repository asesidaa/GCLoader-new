#include "Locale/FilesystemDiagnostics.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gc::locale_compatibility::AnsiFilesystemApi;
using gc::locale_compatibility::AnsiFilesystemObservation;
using gc::locale_compatibility::FilesystemDiagnosticActions;
using gc::locale_compatibility::FilesystemDiagnosticRole;
using gc::locale_compatibility::FilesystemDiagnostics;
using gc::locale_compatibility::WideProbeOutcome;
using gc::locale_compatibility::kFilesystemCategoryCapacity;
using gc::locale_compatibility::kFilesystemInspectionLimit;
using gc::locale_compatibility::kFilesystemRenderedPathLimit;
using gc::locale_compatibility::kObservedAnsiFilesystemApis;

int Expect(bool condition, const char* name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

bool Contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

struct Capture {
    std::vector<std::string> lines;
    std::wstring first_probe;
    std::wstring second_probe;
    WideProbeOutcome probe_outcome{WideProbeOutcome::exists};
    int probe_calls{};
    int event_lines{};
    int failure_lines{};
    int non_ascii_lines{};
    int cap_lines{};
    FilesystemDiagnostics* recursive_target{};
    bool recurse_once{};
    AnsiFilesystemObservation recursive_observation{};

    static WideProbeOutcome Probe(
        void* context,
        AnsiFilesystemApi,
        std::wstring_view first,
        std::wstring_view second) noexcept {
        auto& capture = *static_cast<Capture*>(context);
        try {
            ++capture.probe_calls;
            capture.first_probe.assign(first);
            capture.second_probe.assign(second);
        } catch (...) {
        }
        SetLastError(ERROR_BAD_PATHNAME);
        return capture.probe_outcome;
    }

    static void Emit(void* context, std::string_view line) noexcept {
        auto& capture = *static_cast<Capture*>(context);
        try {
            capture.lines.emplace_back(line);
            if (Contains(line, "class=")) {
                ++capture.event_lines;
            }
            if (Contains(line, "class=failure")) {
                ++capture.failure_lines;
            }
            if (Contains(line, "class=non_ascii")) {
                ++capture.non_ascii_lines;
            }
            if (Contains(line, "category cap reached")) {
                ++capture.cap_lines;
            }
            if (capture.recurse_once &&
                capture.recursive_target != nullptr) {
                capture.recurse_once = false;
                capture.recursive_target->Observe(
                    capture.recursive_observation);
            }
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

AnsiFilesystemObservation Failure(
    AnsiFilesystemApi api,
    LPCSTR first_path,
    DWORD error = ERROR_FILE_NOT_FOUND,
    LPCSTR second_path = nullptr) {
    return {
        .api = api,
        .first_path = first_path,
        .second_path = second_path,
        .succeeded = false,
        .last_error = error,
    };
}

AnsiFilesystemObservation Success(
    AnsiFilesystemApi api,
    LPCSTR first_path,
    LPCSTR second_path = nullptr) {
    return {
        .api = api,
        .first_path = first_path,
        .second_path = second_path,
        .succeeded = true,
        .last_error = ERROR_SUCCESS,
    };
}

int TestStartupClassificationAndExclusions() {
    Capture capture;
    FilesystemDiagnostics diagnostics{
        FilesystemDiagnosticRole::service,
        capture.Actions()};

    diagnostics.Start(kObservedAnsiFilesystemApis);
    diagnostics.Start(kObservedAnsiFilesystemApis);

    int failures = 0;
    failures += Expect(capture.lines.size() == 1,
        "startup emits exactly once");
    if (!capture.lines.empty()) {
        const auto& startup = capture.lines.front();
        failures += Expect(Contains(startup, "role=service"),
            "startup identifies service role");
        for (const auto name : std::array<std::string_view, 8>{
                 "create_file",
                 "get_file_attributes",
                 "find_first_file",
                 "find_next_file",
                 "create_directory",
                 "delete_file",
                 "move_file",
                 "copy_file"}) {
            failures += Expect(Contains(startup, name),
                "startup lists every observed API");
        }
        failures += Expect(
            Contains(startup, "non_ascii_capacity=32") &&
                Contains(startup, "failure_capacity=32"),
            "startup reports both fixed capacities");
    }

    diagnostics.Observe(Success(
        AnsiFilesystemApi::create_file,
        "data\\image.dds"));
    failures += Expect(capture.event_lines == 0,
        "successful ASCII path is quiet");

    diagnostics.Observe(Failure(
        AnsiFilesystemApi::get_file_attributes,
        "data\\missing.dat"));
    failures += Expect(
        capture.event_lines == 1 && capture.failure_lines == 1,
        "ordinary failed path emits one failure");

    constexpr char japanese_path[] =
        "data\\" "\x89\xBC" "_start.dds";
    diagnostics.Observe(Success(
        AnsiFilesystemApi::create_file,
        japanese_path));
    failures += Expect(
        capture.event_lines == 2 && capture.non_ascii_lines == 1,
        "successful CP932 path emits one non-ASCII event");

    diagnostics.Observe(Failure(
        AnsiFilesystemApi::find_next_file,
        nullptr,
        ERROR_NO_MORE_FILES));
    failures += Expect(capture.event_lines == 2,
        "FindNext completion is quiet");

    for (const auto excluded : std::array<LPCSTR, 7>{
             "COM2",
             "\\\\.\\pipe\\nesys_games",
             "\\\\?\\pipe\\nesys_games",
             "\\Device\\NamedPipe\\nesys_games",
             "loader-log.txt",
             "data\\loader-service-log.txt",
             "DATA\\LOADER-LOG.TXT"}) {
        diagnostics.Observe(Failure(
            AnsiFilesystemApi::create_file,
            excluded,
            ERROR_ACCESS_DENIED));
    }
    failures += Expect(capture.event_lines == 2,
        "devices pipes COM ports and loader logs are excluded");

    diagnostics.Observe(Failure(
        AnsiFilesystemApi::get_file_attributes,
        "\\\\?\\C:\\data\\missing.dat"));
    failures += Expect(capture.event_lines == 3,
        "ordinary extended path is observed");

    failures += Expect(
        kFilesystemCategoryCapacity == 32 &&
            kFilesystemRenderedPathLimit == 192 &&
            kFilesystemInspectionLimit == 4096,
        "public diagnostic bounds are exact");
    return failures;
}

int TestFormattingProbeAndLastError() {
    Capture capture;
    capture.probe_outcome = WideProbeOutcome::exists;
    FilesystemDiagnostics diagnostics{
        FilesystemDiagnosticRole::game,
        capture.Actions()};
    constexpr char japanese_path[] =
        "data\\" "\x89\xBC" "_start.dds";

    SetLastError(ERROR_ACCESS_DENIED);
    diagnostics.Observe(Failure(
        AnsiFilesystemApi::create_file,
        japanese_path,
        ERROR_ACCESS_DENIED));

    int failures = 0;
    failures += Expect(GetLastError() == ERROR_ACCESS_DENIED,
        "Observe restores LastError after probe and sink");
    failures += Expect(
        capture.probe_calls == 1 &&
            capture.first_probe == L"data\\\u4eee_start.dds" &&
            capture.second_probe.empty(),
        "probe receives explicit CP932 decoded path");
    failures += Expect(capture.lines.size() == 1,
        "formatted event emits once");
    if (!capture.lines.empty()) {
        const auto& line = capture.lines.front();
        failures += Expect(
            Contains(line, "role=game") &&
                Contains(line, "api=create_file") &&
                Contains(line, "class=non_ascii") &&
                Contains(line, "succeeded=false") &&
                Contains(line, "error=5") &&
                Contains(line, "probe=exists"),
            "formatted event preserves call facts");
        failures += Expect(
            Contains(line, "\\x89\\xBC") &&
                Contains(line, "\xE4\xBB\xAE") &&
                Contains(line, "_start.dds"),
            "formatted event has escaped bytes and UTF-8 decoded path");
    }
    return failures;
}

int TestLongPathBoundsAndInvalidCp932() {
    Capture bounded_capture;
    FilesystemDiagnostics bounded{
        FilesystemDiagnosticRole::game,
        bounded_capture.Actions()};

    std::string first(kFilesystemInspectionLimit + 1, 'a');
    first[0] = static_cast<char>(0x89);
    first[1] = static_cast<char>(0xBC);
    first[kFilesystemInspectionLimit] = 'X';
    auto second = first;
    second[kFilesystemInspectionLimit] = 'Y';

    bounded.Observe(Success(
        AnsiFilesystemApi::get_file_attributes,
        first.c_str()));
    bounded.Observe(Success(
        AnsiFilesystemApi::get_file_attributes,
        second.c_str()));

    int failures = 0;
    failures += Expect(
        bounded_capture.event_lines == 1,
        "inspection-window identity deduplicates suffix-only differences");
    if (!bounded_capture.lines.empty()) {
        const auto& line = bounded_capture.lines.front();
        failures += Expect(
            Contains(line, "truncated=true") &&
                Contains(line, "rendered_bytes=192"),
            "long path reports bounded rendering");
        failures += Expect(
            !Contains(line, "X") && !Contains(line, "Y"),
            "bytes outside rendering and inspection windows are absent");
    }

    Capture invalid_capture;
    FilesystemDiagnostics invalid{
        FilesystemDiagnosticRole::game,
        invalid_capture.Actions()};
    constexpr char invalid_cp932[] =
        "data\\bad_" "\x81";
    invalid.Observe(Success(
        AnsiFilesystemApi::create_file,
        invalid_cp932));
    failures += Expect(
        invalid_capture.probe_calls == 0 &&
            invalid_capture.event_lines == 1 &&
            Contains(invalid_capture.lines.front(),
                "probe=invalid_cp932"),
        "invalid CP932 is reported without probing");
    return failures;
}

int TestDeduplicationCapsAndReentrancy() {
    Capture duplicate_capture;
    FilesystemDiagnostics duplicate{
        FilesystemDiagnosticRole::game,
        duplicate_capture.Actions()};
    const auto same_failure = Failure(
        AnsiFilesystemApi::delete_file,
        "data\\same.dat",
        ERROR_ACCESS_DENIED);
    duplicate.Observe(same_failure);
    duplicate.Observe(same_failure);

    int failures = 0;
    failures += Expect(duplicate_capture.event_lines == 1,
        "duplicate event logs once");

    Capture cap_capture;
    FilesystemDiagnostics capped{
        FilesystemDiagnosticRole::service,
        cap_capture.Actions()};
    std::array<std::string, 40> ascii_paths;
    std::array<std::string, 40> non_ascii_paths;
    for (int index = 0; index < 40; ++index) {
        char suffix[24]{};
        std::snprintf(suffix, sizeof(suffix), "%02d.dat", index);
        ascii_paths[index] = "data\\missing_" + std::string{suffix};
        non_ascii_paths[index] =
            "data\\" "\x89\xBC" "_" + std::string{suffix};
        capped.Observe(Failure(
            AnsiFilesystemApi::get_file_attributes,
            ascii_paths[index].c_str()));
    }
    for (int index = 0; index < 40; ++index) {
        capped.Observe(Success(
            AnsiFilesystemApi::get_file_attributes,
            non_ascii_paths[index].c_str()));
    }
    failures += Expect(
        cap_capture.failure_lines == 32 &&
            cap_capture.non_ascii_lines == 32 &&
            cap_capture.cap_lines == 1,
        "independent fixed budgets emit one total cap line");

    Capture recursive_capture;
    FilesystemDiagnostics recursive{
        FilesystemDiagnosticRole::game,
        recursive_capture.Actions()};
    recursive_capture.recursive_target = &recursive;
    recursive_capture.recurse_once = true;
    recursive_capture.recursive_observation = Failure(
        AnsiFilesystemApi::copy_file,
        "data\\nested.dat",
        ERROR_FILE_NOT_FOUND,
        "data\\nested-copy.dat");
    SetLastError(ERROR_LOCK_VIOLATION);
    recursive.Observe(Failure(
        AnsiFilesystemApi::move_file,
        "data\\outer.dat",
        ERROR_ACCESS_DENIED,
        "data\\outer-moved.dat"));
    failures += Expect(
        recursive_capture.event_lines == 1 &&
            GetLastError() == ERROR_LOCK_VIOLATION,
        "reentrant sink is suppressed and LastError is restored");
    return failures;
}

} // namespace

int main() {
    int failures = 0;
    failures += TestStartupClassificationAndExclusions();
    failures += TestFormattingProbeAndLastError();
    failures += TestLongPathBoundsAndInvalidCp932();
    failures += TestDeduplicationCapsAndReentrancy();
    return failures == 0 ? 0 : 1;
}

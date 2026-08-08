#include "Nesys/ThreadPriorityOverride.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

int expect_true(bool actual, const char* name) {
    if (actual) {
        return 0;
    }
    std::cerr << "Expected true for " << name << "\n";
    return 1;
}

int expect_priority(int actual, int expected, const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " to be " << expected
              << ", got " << actual << "\n";
    return 1;
}

struct FakeSetPriorityState {
    HANDLE thread{};
    int priority{};
    int calls{};
    BOOL result{TRUE};
    DWORD last_error{ERROR_SUCCESS};
    int diagnostic_calls{};
    std::uintptr_t diagnostic_caller{};
    int diagnostic_requested{};
    int diagnostic_effective{};
};

FakeSetPriorityState* g_fake{};

BOOL WINAPI fake_set_thread_priority(HANDLE thread, int priority) {
    ++g_fake->calls;
    g_fake->thread = thread;
    g_fake->priority = priority;
    SetLastError(g_fake->last_error);
    return g_fake->result;
}

void fake_diagnostic(
    std::uintptr_t caller,
    int requested,
    int effective) noexcept {
    ++g_fake->diagnostic_calls;
    g_fake->diagnostic_caller = caller;
    g_fake->diagnostic_requested = requested;
    g_fake->diagnostic_effective = effective;
    SetLastError(ERROR_BUSY);
}

struct SyntheticPeImage {
    alignas(IMAGE_NT_HEADERS) std::array<std::byte, 0x400> bytes{};
};

SyntheticPeImage make_valid_pe(DWORD size_of_image = 0x3000) {
    SyntheticPeImage image{};
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.bytes.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;

    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        image.bytes.data() + dos->e_lfanew);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.SizeOfImage = size_of_image;
    return image;
}

IMAGE_DOS_HEADER* dos_header(SyntheticPeImage& image) {
    return reinterpret_cast<IMAGE_DOS_HEADER*>(image.bytes.data());
}

IMAGE_NT_HEADERS* nt_headers(SyntheticPeImage& image) {
    return reinterpret_cast<IMAGE_NT_HEADERS*>(
        image.bytes.data() + dos_header(image)->e_lfanew);
}

} // namespace

int main() {
    using namespace gc::nesys_service;

    int failures = 0;
    const ExecutableImageRange image{0x1000, 0x2000};

    failures += expect_priority(
        NormalizeExecutableThreadPriority(
            image, 0x1000, THREAD_PRIORITY_IDLE),
        THREAD_PRIORITY_NORMAL,
        "image-base idle priority");
    failures += expect_priority(
        NormalizeExecutableThreadPriority(
            image, 0x1800, THREAD_PRIORITY_LOWEST),
        THREAD_PRIORITY_NORMAL,
        "image lowest priority");
    failures += expect_priority(
        NormalizeExecutableThreadPriority(
            image, 0x1FFF, THREAD_PRIORITY_BELOW_NORMAL),
        THREAD_PRIORITY_NORMAL,
        "last image byte below-normal priority");
    failures += expect_priority(
        NormalizeExecutableThreadPriority(
            image, 0x1800, THREAD_PRIORITY_NORMAL),
        THREAD_PRIORITY_NORMAL,
        "normal priority passes");
    failures += expect_priority(
        NormalizeExecutableThreadPriority(
            image, 0x1800, THREAD_PRIORITY_ABOVE_NORMAL),
        THREAD_PRIORITY_ABOVE_NORMAL,
        "above-normal priority passes");
    failures += expect_priority(
        NormalizeExecutableThreadPriority(
            image, 0x1800, THREAD_PRIORITY_TIME_CRITICAL),
        THREAD_PRIORITY_TIME_CRITICAL,
        "time-critical priority passes");
    failures += expect_priority(
        NormalizeExecutableThreadPriority(
            image, 0x1800, THREAD_MODE_BACKGROUND_BEGIN),
        THREAD_MODE_BACKGROUND_BEGIN,
        "background begin passes");
    failures += expect_priority(
        NormalizeExecutableThreadPriority(
            image, 0x1800, THREAD_MODE_BACKGROUND_END),
        THREAD_MODE_BACKGROUND_END,
        "background end passes");
    failures += expect_priority(
        NormalizeExecutableThreadPriority(
            image, 0x0FFF, THREAD_PRIORITY_BELOW_NORMAL),
        THREAD_PRIORITY_BELOW_NORMAL,
        "caller below image passes");
    failures += expect_priority(
        NormalizeExecutableThreadPriority(
            image, 0x2000, THREAD_PRIORITY_LOWEST),
        THREAD_PRIORITY_LOWEST,
        "exclusive image end passes");
    failures += expect_priority(
        NormalizeExecutableThreadPriority(
            ExecutableImageRange{0x2000, 0x1000},
            0x1800,
            THREAD_PRIORITY_LOWEST),
        THREAD_PRIORITY_LOWEST,
        "invalid image range passes");

    FakeSetPriorityState clamped{};
    clamped.result = FALSE;
    clamped.last_error = ERROR_ACCESS_DENIED;
    g_fake = &clamped;
    const auto sentinel = reinterpret_cast<HANDLE>(0x1234);
    const BOOL clamped_result = ForwardExecutableThreadPriority(
        image,
        0x1800,
        sentinel,
        THREAD_PRIORITY_LOWEST,
        &fake_set_thread_priority,
        &fake_diagnostic);
    failures += expect_true(
        clamped_result == FALSE &&
            clamped.calls == 1 &&
            clamped.thread == sentinel &&
            clamped.priority == THREAD_PRIORITY_NORMAL &&
            clamped.diagnostic_calls == 1 &&
            clamped.diagnostic_caller == 0x1800 &&
            clamped.diagnostic_requested == THREAD_PRIORITY_LOWEST &&
            clamped.diagnostic_effective == THREAD_PRIORITY_NORMAL &&
            GetLastError() == ERROR_ACCESS_DENIED,
        "clamped request preserves API contract");

    FakeSetPriorityState passthrough{};
    g_fake = &passthrough;
    const BOOL passthrough_result = ForwardExecutableThreadPriority(
        image,
        0x3000,
        sentinel,
        THREAD_PRIORITY_LOWEST,
        &fake_set_thread_priority,
        &fake_diagnostic);
    failures += expect_true(
        passthrough_result == TRUE &&
            passthrough.calls == 1 &&
            passthrough.thread == sentinel &&
            passthrough.priority == THREAD_PRIORITY_LOWEST &&
            passthrough.diagnostic_calls == 0,
        "non-executable caller passes through");

    SetLastError(ERROR_SUCCESS);
    const BOOL missing_original = ForwardExecutableThreadPriority(
        image,
        0x1800,
        sentinel,
        THREAD_PRIORITY_LOWEST,
        nullptr,
        &fake_diagnostic);
    failures += expect_true(
        missing_original == FALSE &&
            GetLastError() == ERROR_INVALID_FUNCTION,
        "missing original fails with Win32 error");

    auto valid_pe = make_valid_pe();
    const auto valid_range = ReadExecutableImageRange(
        reinterpret_cast<HMODULE>(valid_pe.bytes.data()));
    const auto valid_begin = reinterpret_cast<std::uintptr_t>(
        valid_pe.bytes.data());
    failures += expect_true(
        valid_range.has_value() &&
            valid_range->begin == valid_begin &&
            valid_range->end == valid_begin + 0x3000,
        "valid x86 PE image range");
    failures += expect_true(
        !ReadExecutableImageRange(nullptr).has_value(),
        "null PE module rejected");

    auto invalid_dos = make_valid_pe();
    dos_header(invalid_dos)->e_magic = 0;
    failures += expect_true(
        !ReadExecutableImageRange(
             reinterpret_cast<HMODULE>(invalid_dos.bytes.data()))
             .has_value(),
        "invalid DOS signature rejected");

    auto invalid_nt = make_valid_pe();
    nt_headers(invalid_nt)->Signature = 0;
    failures += expect_true(
        !ReadExecutableImageRange(
             reinterpret_cast<HMODULE>(invalid_nt.bytes.data()))
             .has_value(),
        "invalid NT signature rejected");

    auto invalid_optional = make_valid_pe();
    nt_headers(invalid_optional)->OptionalHeader.Magic = 0;
    failures += expect_true(
        !ReadExecutableImageRange(
             reinterpret_cast<HMODULE>(invalid_optional.bytes.data()))
             .has_value(),
        "invalid optional-header magic rejected");

    auto zero_size = make_valid_pe(0);
    failures += expect_true(
        !ReadExecutableImageRange(
             reinterpret_cast<HMODULE>(zero_size.bytes.data()))
             .has_value(),
        "zero image size rejected");

    if constexpr (sizeof(std::uintptr_t) == sizeof(std::uint32_t)) {
        auto overflowing = make_valid_pe(
            std::numeric_limits<DWORD>::max());
        failures += expect_true(
            !ReadExecutableImageRange(
                 reinterpret_cast<HMODULE>(overflowing.bytes.data()))
                 .has_value(),
            "overflowing x86 image range rejected");
    }

    std::vector<ApiHookRequest> requests;
    AppendThreadPriorityOverrideHookRequest(requests);
    failures += expect_true(
        requests.size() == 1 &&
            std::wstring_view{requests.front().module_name} ==
                L"kernel32.dll" &&
            std::string_view{requests.front().export_name} ==
                "SetThreadPriority" &&
            requests.front().detour != nullptr &&
            requests.front().original != nullptr,
        "priority hook request contract");

    return failures == 0 ? 0 : 1;
}

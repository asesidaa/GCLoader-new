#include "Nesys/Registry/RegistryConfigOverride.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

std::atomic_bool g_fail_next_allocation{false};

void* operator new(std::size_t size) {
    if (g_fail_next_allocation.exchange(
            false,
            std::memory_order_relaxed)) {
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

namespace {

using namespace gc::nesys_service;

struct FakeRegistryState {
    LSTATUS open_status{ERROR_SUCCESS};
    HKEY next_handle{reinterpret_cast<HKEY>(0x1000)};
    int open_calls{0};
    HKEY last_root{nullptr};
    std::string last_subkey;
    DWORD last_options{0};
    REGSAM last_access{0};

    LSTATUS query_status{ERROR_FILE_NOT_FOUND};
    int query_calls{0};
    HKEY last_query_handle{nullptr};
    std::string last_value_name;
    LPDWORD last_reserved{nullptr};
    LPDWORD last_type{nullptr};
    LPBYTE last_data{nullptr};
    LPDWORD last_data_size{nullptr};

    LSTATUS close_status{ERROR_SUCCESS};
    int close_calls{0};
    HKEY last_close_handle{nullptr};
};

FakeRegistryState* g_fake = nullptr;

struct AllocationFailureState {
    HKEY opened_handle{reinterpret_cast<HKEY>(0x5001)};
    int open_calls{0};
    int close_calls{0};
    HKEY closed_handle{nullptr};
};

AllocationFailureState* g_allocation_failure = nullptr;

LSTATUS WINAPI fake_open_for_allocation_failure(
    HKEY,
    LPCSTR,
    DWORD,
    REGSAM,
    PHKEY result) {
    ++g_allocation_failure->open_calls;
    if (result != nullptr) {
        *result = g_allocation_failure->opened_handle;
    }
    return ERROR_SUCCESS;
}

LSTATUS WINAPI fake_close_for_allocation_failure(HKEY key) {
    ++g_allocation_failure->close_calls;
    g_allocation_failure->closed_handle = key;
    return ERROR_SUCCESS;
}

struct HandleReuseControl {
    HANDLE close_entered;
    HANDLE allow_close_return;
    HANDLE reopen_original_returning;
};

HandleReuseControl* g_handle_reuse = nullptr;

LSTATUS WINAPI fake_open(
    HKEY root,
    LPCSTR subkey,
    DWORD options,
    REGSAM access,
    PHKEY result) {
    ++g_fake->open_calls;
    g_fake->last_root = root;
    g_fake->last_subkey = subkey != nullptr ? subkey : "<null>";
    g_fake->last_options = options;
    g_fake->last_access = access;
    if (g_fake->open_status == ERROR_SUCCESS && result != nullptr) {
        *result = g_fake->next_handle;
    }
    return g_fake->open_status;
}

LSTATUS WINAPI fake_query(
    HKEY key,
    LPCSTR value_name,
    LPDWORD reserved,
    LPDWORD type,
    LPBYTE data,
    LPDWORD data_size) {
    ++g_fake->query_calls;
    g_fake->last_query_handle = key;
    g_fake->last_value_name =
        value_name != nullptr ? value_name : "<null>";
    g_fake->last_reserved = reserved;
    g_fake->last_type = type;
    g_fake->last_data = data;
    g_fake->last_data_size = data_size;
    return g_fake->query_status;
}

LSTATUS WINAPI fake_close(HKEY key) {
    ++g_fake->close_calls;
    g_fake->last_close_handle = key;
    return g_fake->close_status;
}

LSTATUS WINAPI fake_open_reused_handle(
    HKEY root,
    LPCSTR subkey,
    DWORD options,
    REGSAM access,
    PHKEY result) {
    const LSTATUS status = fake_open(
        root,
        subkey,
        options,
        access,
        result);
    SetEvent(g_handle_reuse->reopen_original_returning);
    return status;
}

LSTATUS WINAPI fake_close_with_reuse_barrier(HKEY key) {
    ++g_fake->close_calls;
    g_fake->last_close_handle = key;
    const LSTATUS status = g_fake->close_status;
    SetEvent(g_handle_reuse->close_entered);
    WaitForSingleObject(g_handle_reuse->allow_close_return, INFINITE);
    return status;
}

int expect(bool value, const char* name) {
    if (value) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

int expect_status(LSTATUS actual, LSTATUS expected, const char* name) {
    if (actual == expected) {
        return 0;
    }
    std::cerr << "Expected " << name << " status " << expected
              << ", got " << actual << "\n";
    return 1;
}

HKEY track(
    RegistryConfigOverride& overlay,
    FakeRegistryState& state,
    HKEY root,
    const char* subkey,
    DWORD options,
    REGSAM access,
    HKEY handle,
    int* failures) {
    state.open_status = ERROR_SUCCESS;
    state.next_handle = handle;
    g_fake = &state;
    HKEY opened = nullptr;
    *failures += expect_status(
        overlay.Open(
            fake_open,
            root,
            subkey,
            options,
            access,
            &opened),
        ERROR_SUCCESS,
        "tracked open");
    *failures += expect(opened == handle, "original open handle returned");
    return opened;
}

int expect_dword_override(
    RegistryConfigOverride& overlay,
    FakeRegistryState& state,
    HKEY handle,
    const char* name,
    DWORD expected) {
    int failures = 0;
    const int original_calls = state.query_calls;

    DWORD type = 0;
    DWORD size = 0;
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            nullptr,
            &size),
        ERROR_SUCCESS,
        "DWORD size probe");
    failures += expect(type == REG_DWORD, "DWORD probe type");
    failures += expect(size == sizeof(DWORD), "DWORD probe size");

    DWORD value = 0xCCCCCCCC;
    size = sizeof(value);
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(&value),
            &size),
        ERROR_SUCCESS,
        "DWORD exact buffer");
    failures += expect(type == REG_DWORD, "DWORD exact type");
    failures += expect(size == sizeof(DWORD), "DWORD exact size");
    failures += expect(value == expected, "DWORD exact value");

    std::array<BYTE, 8> oversized{};
    oversized.fill(0xA5);
    size = static_cast<DWORD>(oversized.size());
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            oversized.data(),
            &size),
        ERROR_SUCCESS,
        "DWORD oversized buffer");
    DWORD oversized_value = 0;
    std::memcpy(&oversized_value, oversized.data(), sizeof(oversized_value));
    failures += expect(oversized_value == expected, "DWORD oversized value");
    failures += expect(
        oversized[4] == 0xA5 && oversized[7] == 0xA5,
        "DWORD oversized tail untouched");

    std::array<BYTE, 3> short_buffer{0x5A, 0x5A, 0x5A};
    size = static_cast<DWORD>(short_buffer.size());
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            short_buffer.data(),
            &size),
        ERROR_MORE_DATA,
        "DWORD short buffer");
    failures += expect(size == sizeof(DWORD), "DWORD short required size");
    failures += expect(
        short_buffer == std::array<BYTE, 3>{0x5A, 0x5A, 0x5A},
        "DWORD short buffer not overwritten");
    failures += expect(
        state.query_calls == original_calls,
        "owned DWORD never calls original query");
    return failures;
}

int expect_string_override(
    RegistryConfigOverride& overlay,
    FakeRegistryState& state,
    HKEY handle,
    const char* name,
    std::string_view expected) {
    int failures = 0;
    const int original_calls = state.query_calls;
    const DWORD required = static_cast<DWORD>(expected.size() + 1);

    DWORD type = 0;
    DWORD size = 0;
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            nullptr,
            &size),
        ERROR_SUCCESS,
        "string size probe");
    failures += expect(type == REG_SZ, "string probe type");
    failures += expect(size == required, "string probe includes NUL");

    std::vector<BYTE> exact(required, 0xCC);
    size = required;
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            exact.data(),
            &size),
        ERROR_SUCCESS,
        "string exact buffer");
    failures += expect(type == REG_SZ, "string exact type");
    failures += expect(size == required, "string exact size");
    failures += expect(
        std::string_view{
            reinterpret_cast<const char*>(exact.data()),
            expected.size()} == expected,
        "string exact bytes");
    failures += expect(exact.back() == 0, "string exact terminator");

    std::vector<BYTE> oversized(required + 4, 0xA5);
    size = static_cast<DWORD>(oversized.size());
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            oversized.data(),
            &size),
        ERROR_SUCCESS,
        "string oversized buffer");
    failures += expect(
        oversized[required] == 0xA5 && oversized.back() == 0xA5,
        "string oversized tail untouched");

    std::vector<BYTE> short_buffer(required - 1, 0x5A);
    size = static_cast<DWORD>(short_buffer.size());
    failures += expect_status(
        overlay.Query(
            fake_query,
            handle,
            name,
            nullptr,
            &type,
            short_buffer.data(),
            &size),
        ERROR_MORE_DATA,
        "string short buffer");
    failures += expect(size == required, "string short required size");
    failures += expect(
        short_buffer.front() == 0x5A && short_buffer.back() == 0x5A,
        "string short buffer not overwritten");
    failures += expect(
        state.query_calls == original_calls,
        "owned string never calls original query");
    return failures;
}

} // namespace

int main() {
    using namespace gc::nesys_service;
    int failures = 0;

    RegistryConfig config{};
    config.game().country = GameCountry::GrooveCoasterEng;
    config.nesys().game_kind = 303802;
    config.nesys().event_next_time = 0;
    config.nesys().condition_time = 1;
    config.nesys().log_level = 2;
    config.nesys().news_path = "N:\\news";
    config.nesys().event_path = "E:\\event";
    config.nesys().log_path = "L:\\log";

    const auto values = CreateRegistryOverrideValues(config);
    failures += expect(values.has_value(), "valid immutable override values");
    if (!values.has_value()) {
        return 1;
    }

    FakeRegistryState state{};
    g_fake = &state;
    RegistryConfigOverride game{ProcessRole::Game, *values};
    RegistryConfigOverride service{ProcessRole::Service, *values};

    const auto game_handle = reinterpret_cast<HKEY>(0x1001);
    track(
        game,
        state,
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\taito\\typex",
        17,
        KEY_ALL_ACCESS,
        game_handle,
        &failures);
    failures += expect(state.last_root == HKEY_LOCAL_MACHINE, "game root unchanged");
    failures += expect(
        state.last_subkey == "SOFTWARE\\taito\\typex",
        "game subkey unchanged");
    failures += expect(state.last_options == 17, "game options unchanged");
    failures += expect(state.last_access == KEY_ALL_ACCESS, "game access unchanged");
    failures += expect_dword_override(
        game,
        state,
        game_handle,
        "Country",
        2);
    failures += expect_dword_override(
        game,
        state,
        game_handle,
        "cOuNtRy",
        2);

    DWORD type = 0;
    DWORD size = 0;
    failures += expect_status(
        game.Query(
            fake_query,
            game_handle,
            "Country",
            reinterpret_cast<LPDWORD>(1),
            &type,
            nullptr,
            &size),
        ERROR_INVALID_PARAMETER,
        "non-null reserved pointer");
    DWORD country = 0;
    failures += expect_status(
        game.Query(
            fake_query,
            game_handle,
            "Country",
            nullptr,
            &type,
            reinterpret_cast<LPBYTE>(&country),
            nullptr),
        ERROR_INVALID_PARAMETER,
        "data without size pointer");
    type = 0;
    failures += expect_status(
        game.Query(
            fake_query,
            game_handle,
            "Country",
            nullptr,
            &type,
            nullptr,
            nullptr),
        ERROR_SUCCESS,
        "type-only query with null data and size");
    failures += expect(type == REG_DWORD, "type-only query type");

    const auto service_handle = reinterpret_cast<HKEY>(0x2001);
    track(
        service,
        state,
        HKEY_LOCAL_MACHINE,
        "software\\TAITO\\TYPEX",
        0,
        KEY_READ,
        service_handle,
        &failures);
    failures += expect(state.last_access == KEY_READ, "service access unchanged");

    struct DwordCase {
        const char* name;
        DWORD value;
    };
    constexpr std::array<DwordCase, 5> dword_cases{{
        {"GameKind", 303802},
        {"EventNextTime", 0},
        {"ConditionTime", 1},
        {"TrafficCount", 0},
        {"LogLevel", 2},
    }};
    for (const auto& test : dword_cases) {
        failures += expect_dword_override(
            service,
            state,
            service_handle,
            test.name,
            test.value);
    }

    struct StringCase {
        const char* name;
        std::string_view value;
    };
    constexpr std::array<StringCase, 3> string_cases{{
        {"NewsPath", "N:\\news"},
        {"EventPath", "E:\\event"},
        {"LogPath", "L:\\log"},
    }};
    for (const auto& test : string_cases) {
        failures += expect_string_override(
            service,
            state,
            service_handle,
            test.name,
            test.value);
    }

    const int before_pass_through = state.query_calls;
    constexpr std::array<const char*, 3> service_pass_through{
        "CoinCredit",
        "NetworkAddress",
        "Country",
    };
    for (const auto* name : service_pass_through) {
        failures += expect_status(
            service.Query(
                fake_query,
                service_handle,
                name,
                nullptr,
                nullptr,
                nullptr,
                nullptr),
            ERROR_FILE_NOT_FOUND,
            name);
    }
    failures += expect(
        state.query_calls ==
            before_pass_through +
                static_cast<int>(service_pass_through.size()),
        "service unowned values call original");
    const int before_null_name = state.query_calls;
    failures += expect_status(
        service.Query(
            fake_query,
            service_handle,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "null value name pass-through");
    failures += expect(
        state.query_calls == before_null_name + 1 &&
            state.last_value_name == "<null>",
        "null value name reaches original query");

    const int before_game_unowned = state.query_calls;
    failures += expect_status(
        game.Query(
            fake_query,
            game_handle,
            "GameKind",
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "game does not own GameKind");
    failures += expect(
        state.query_calls == before_game_unowned + 1,
        "game unowned query calls original");

    const auto unrelated_handle = reinterpret_cast<HKEY>(0x3001);
    track(
        game,
        state,
        HKEY_LOCAL_MACHINE,
        "SYSTEM\\ControlSet001\\Control\\Biosinfo",
        0,
        KEY_READ,
        unrelated_handle,
        &failures);
    const int before_bios = state.query_calls;
    failures += expect_status(
        game.Query(
            fake_query,
            unrelated_handle,
            "SystemBiosDate",
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "SystemBiosDate pass-through");
    failures += expect(
        state.query_calls == before_bios + 1,
        "unrelated key calls original query");

    const auto wrong_root_handle = reinterpret_cast<HKEY>(0x3002);
    track(
        game,
        state,
        HKEY_CURRENT_USER,
        "SOFTWARE\\taito\\typex",
        0,
        KEY_READ,
        wrong_root_handle,
        &failures);
    const int before_wrong_root = state.query_calls;
    failures += expect_status(
        game.Query(
            fake_query,
            wrong_root_handle,
            "Country",
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "Type X path below wrong root pass-through");
    failures += expect(
        state.query_calls == before_wrong_root + 1,
        "wrong-root handle calls original query");

    state.open_status = ERROR_ACCESS_DENIED;
    state.next_handle = reinterpret_cast<HKEY>(0x4001);
    HKEY failed_open = nullptr;
    failures += expect_status(
        game.Open(
            fake_open,
            HKEY_LOCAL_MACHINE,
            "SOFTWARE\\taito\\typex",
            0,
            KEY_ALL_ACCESS,
            &failed_open),
        ERROR_ACCESS_DENIED,
        "physical Type X open failure preserved");
    const int before_failed_handle = state.query_calls;
    failures += expect_status(
        game.Query(
            fake_query,
            reinterpret_cast<HKEY>(0x4001),
            "Country",
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "failed open never tracked");
    failures += expect(
        state.query_calls == before_failed_handle + 1,
        "failed-open handle passes through");

    state.open_status = ERROR_FILE_NOT_FOUND;
    HKEY synthetic_service_handle = nullptr;
    const int before_synthetic_open = state.open_calls;
    failures += expect_status(
        service.Open(
            fake_open,
            HKEY_LOCAL_MACHINE,
            "SOFTWARE\\taito\\typex",
            0,
            KEY_READ,
            &synthetic_service_handle),
        ERROR_SUCCESS,
        "missing physical service key uses synthetic handle");
    failures += expect(
        state.open_calls == before_synthetic_open + 1 &&
            synthetic_service_handle != nullptr,
        "synthetic service open preserves original-first probe");
    failures += expect_dword_override(
        service,
        state,
        synthetic_service_handle,
        "TrafficCount",
        0);
    const int before_synthetic_close = state.close_calls;
    failures += expect_status(
        service.Close(fake_close, synthetic_service_handle),
        ERROR_SUCCESS,
        "synthetic service handle close");
    failures += expect(
        state.close_calls == before_synthetic_close,
        "synthetic service close does not reach native registry");

    const auto second_handle = reinterpret_cast<HKEY>(0x1002);
    track(
        game,
        state,
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\TAITO\\TYPEX",
        0,
        KEY_READ,
        second_handle,
        &failures);
    failures += expect_dword_override(
        game,
        state,
        second_handle,
        "Country",
        2);

    state.close_status = ERROR_SUCCESS;
    failures += expect_status(
        game.Close(fake_close, game_handle),
        ERROR_SUCCESS,
        "first handle close");
    const int before_closed_query = state.query_calls;
    failures += expect_status(
        game.Query(
            fake_query,
            game_handle,
            "Country",
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "closed handle no stale ownership");
    failures += expect(
        state.query_calls == before_closed_query + 1,
        "closed handle calls original query");
    failures += expect_dword_override(
        game,
        state,
        second_handle,
        "Country",
        2);

    state.close_status = ERROR_BUSY;
    failures += expect_status(
        game.Close(fake_close, second_handle),
        ERROR_BUSY,
        "failed close status");
    failures += expect_dword_override(
        game,
        state,
        second_handle,
        "Country",
        2);

    state.close_status = ERROR_SUCCESS;
    failures += expect_status(
        game.Close(fake_close, second_handle),
        ERROR_SUCCESS,
        "successful close after failure");
    const int before_reuse = state.query_calls;
    failures += expect_status(
        game.Query(
            fake_query,
            second_handle,
            "Country",
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        ERROR_FILE_NOT_FOUND,
        "reused numeric handle not implicitly tracked");
    failures += expect(
        state.query_calls == before_reuse + 1,
        "stale reused handle calls original");
    track(
        game,
        state,
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\taito\\typex",
        0,
        KEY_READ,
        second_handle,
        &failures);
    failures += expect_dword_override(
        game,
        state,
        second_handle,
        "Country",
        2);

    const auto raced_handle = reinterpret_cast<HKEY>(0x1003);
    track(
        game,
        state,
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\taito\\typex",
        0,
        KEY_READ,
        raced_handle,
        &failures);
    HANDLE close_entered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE allow_close_return = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE reopen_original_returning =
        CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE reopen_completed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    failures += expect(close_entered != nullptr, "close-entered event");
    failures += expect(
        allow_close_return != nullptr,
        "allow-close-return event");
    failures += expect(
        reopen_original_returning != nullptr,
        "reopen-original-returning event");
    failures += expect(
        reopen_completed != nullptr,
        "reopen-completed event");
    if (close_entered != nullptr &&
        allow_close_return != nullptr &&
        reopen_original_returning != nullptr &&
        reopen_completed != nullptr) {
        HandleReuseControl control{
            close_entered,
            allow_close_return,
            reopen_original_returning,
        };
        g_handle_reuse = &control;
        state.close_status = ERROR_SUCCESS;
        state.open_status = ERROR_SUCCESS;
        state.next_handle = raced_handle;

        LSTATUS concurrent_close_status = ERROR_INVALID_FUNCTION;
        HKEY reopened = nullptr;
        int reopen_failures = 0;
        std::thread closing([&] {
            concurrent_close_status = game.Close(
                fake_close_with_reuse_barrier,
                raced_handle);
        });
        constexpr DWORD event_wait_milliseconds = 5000;
        const DWORD close_wait = WaitForSingleObject(
            close_entered,
            event_wait_milliseconds);
        failures += expect(
            close_wait == WAIT_OBJECT_0,
            "native close reached reuse barrier");
        if (close_wait == WAIT_OBJECT_0) {
            std::thread reopening([&] {
                reopen_failures += expect_status(
                    game.Open(
                        fake_open_reused_handle,
                        HKEY_LOCAL_MACHINE,
                        "SOFTWARE\\taito\\typex",
                        0,
                        KEY_READ,
                        &reopened),
                    ERROR_SUCCESS,
                    "concurrent reused handle open");
                SetEvent(reopen_completed);
            });
            const DWORD reopen_original_wait = WaitForSingleObject(
                reopen_original_returning,
                event_wait_milliseconds);
            failures += expect(
                reopen_original_wait == WAIT_OBJECT_0,
                "reused native open reached dispatcher");
            constexpr DWORD blocked_open_probe_milliseconds = 1000;
            const DWORD reopen_completion_wait = WaitForSingleObject(
                reopen_completed,
                blocked_open_probe_milliseconds);
            failures += expect(
                reopen_completion_wait == WAIT_OBJECT_0 ||
                    reopen_completion_wait == WAIT_TIMEOUT,
                "reused overlay open wait result");
            SetEvent(allow_close_return);
            closing.join();
            reopening.join();
            failures += reopen_failures;
            failures += expect_status(
                concurrent_close_status,
                ERROR_SUCCESS,
                "concurrent old handle close");
            failures += expect(
                reopened == raced_handle,
                "native open reused numeric handle");
            failures += expect_dword_override(
                game,
                state,
                raced_handle,
                "Country",
                2);
        } else {
            SetEvent(allow_close_return);
            closing.join();
        }
        g_handle_reuse = nullptr;
    }
    if (reopen_completed != nullptr) {
        CloseHandle(reopen_completed);
    }
    if (reopen_original_returning != nullptr) {
        CloseHandle(reopen_original_returning);
    }
    if (allow_close_return != nullptr) {
        CloseHandle(allow_close_return);
    }
    if (close_entered != nullptr) {
        CloseHandle(close_entered);
    }

    const auto exports = RegistryOverrideHookExports();
    failures += expect(exports.size() == 3, "exact registry hook count");
    constexpr std::array<std::string_view, 3> expected_exports{
        "RegOpenKeyExA",
        "RegQueryValueExA",
        "RegCloseKey",
    };
    for (std::size_t index = 0; index < expected_exports.size(); ++index) {
        failures += expect(
            exports[index] == expected_exports[index],
            "registry hook export order");
    }
    constexpr std::array<std::string_view, 8> forbidden_exports{
        "RegOpenKeyExW",
        "RegQueryValueExW",
        "RegEnumKeyExA",
        "RegEnumValueA",
        "RegCreateKeyExA",
        "RegSetValueExA",
        "RegDeleteKeyA",
        "RegDeleteValueA",
    };
    for (const auto forbidden : forbidden_exports) {
        for (const auto* exported : exports) {
            failures += expect(
                forbidden != exported,
                "forbidden registry export absent");
        }
    }

    std::vector<ApiHookRequest> requests;
    AppendRegistryOverrideHookRequests(requests);
    failures += expect(requests.size() == 3, "three registry hook requests");
    for (std::size_t index = 0; index < requests.size(); ++index) {
        failures += expect(
            std::wstring_view{requests[index].module_name} == L"Advapi32.dll",
            "registry hook module");
        failures += expect(
            std::string_view{requests[index].export_name} ==
                expected_exports[index],
            "registry request export order");
        failures += expect(
            requests[index].detour != nullptr &&
                requests[index].original != nullptr,
            "registry request has detour and trampoline slot");
    }

    failures += expect(
        InitializeRegistryConfigOverride(ProcessRole::Game, config),
        "initialize allocation-failure detour state");
    std::vector<ApiHookRequest> allocation_requests;
    AppendRegistryOverrideHookRequests(allocation_requests);
    *allocation_requests[0].original = reinterpret_cast<LPVOID>(
        &fake_open_for_allocation_failure);
    *allocation_requests[2].original = reinterpret_cast<LPVOID>(
        &fake_close_for_allocation_failure);

    AllocationFailureState allocation_failure{};
    g_allocation_failure = &allocation_failure;
    HKEY allocation_result = nullptr;
    g_fail_next_allocation.store(true, std::memory_order_relaxed);
    const auto open_detour = reinterpret_cast<RegOpenKeyExAFn>(
        allocation_requests[0].detour);
    failures += expect_status(
        open_detour(
            HKEY_LOCAL_MACHINE,
            "SOFTWARE\\taito\\typex",
            0,
            KEY_READ,
            &allocation_result),
        ERROR_NOT_ENOUGH_MEMORY,
        "tracking allocation failure");
    failures += expect(
        allocation_failure.open_calls == 1,
        "allocation failure opened physical key once");
    failures += expect(
        allocation_failure.close_calls == 1 &&
            allocation_failure.closed_handle ==
                allocation_failure.opened_handle,
        "allocation failure closes exact physical handle");
    failures += expect(
        allocation_result == nullptr,
        "allocation failure clears caller handle");
    g_allocation_failure = nullptr;

    RegistryConfig invalid = config;
    invalid.nesys().log_path = std::string(260, 'x');
    failures += expect(
        !CreateRegistryOverrideValues(invalid).has_value(),
        "invalid config cannot become immutable override state");

    return failures == 0 ? 0 : 1;
}

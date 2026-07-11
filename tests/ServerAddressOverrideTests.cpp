#include "ServerAddressOverride.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <thread>
#include <vector>

namespace {

struct ModernCapture {
    int calls{0};
    PCWSTR node{nullptr};
    PCWSTR service{nullptr};
    const ADDRINFOW* hints_pointer{nullptr};
    ADDRINFOW hints_value{};
    bool had_hints{false};
    PADDRINFOW result_value{
        reinterpret_cast<PADDRINFOW>(0x12345678)};
    INT return_value{WSAHOST_NOT_FOUND};
};

ModernCapture g_modern{};

INT WSAAPI fake_get_addr_info_w(
    PCWSTR node,
    PCWSTR service,
    const ADDRINFOW* hints,
    PADDRINFOW* result) {
    ++g_modern.calls;
    g_modern.node = node;
    g_modern.service = service;
    g_modern.hints_pointer = hints;
    g_modern.had_hints = hints != nullptr;
    if (hints != nullptr) {
        g_modern.hints_value = *hints;
    }
    *result = g_modern.result_value;
    return g_modern.return_value;
}

struct ExCapture {
    int calls{0};
    PCWSTR node{nullptr};
    PCWSTR service{nullptr};
    DWORD name_space{0};
    LPGUID provider{nullptr};
    const ADDRINFOEXW* hints_pointer{nullptr};
    ADDRINFOEXW hints_value{};
    bool had_hints{false};
    PADDRINFOEXW result_value{
        reinterpret_cast<PADDRINFOEXW>(0x23456789)};
    timeval* timeout{nullptr};
    LPOVERLAPPED overlapped{nullptr};
    LPLOOKUPSERVICE_COMPLETION_ROUTINE completion{nullptr};
    LPHANDLE cancel_handle{nullptr};
    INT return_value{WSA_IO_PENDING};
};

ExCapture g_ex{};

INT WSAAPI fake_get_addr_info_ex_w(
    PCWSTR node,
    PCWSTR service,
    DWORD name_space,
    LPGUID provider,
    const ADDRINFOEXW* hints,
    PADDRINFOEXW* result,
    timeval* timeout,
    LPOVERLAPPED overlapped,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE completion,
    LPHANDLE cancel_handle) {
    ++g_ex.calls;
    g_ex.node = node;
    g_ex.service = service;
    g_ex.name_space = name_space;
    g_ex.provider = provider;
    g_ex.hints_pointer = hints;
    g_ex.had_hints = hints != nullptr;
    if (hints != nullptr) {
        g_ex.hints_value = *hints;
    }
    *result = g_ex.result_value;
    g_ex.timeout = timeout;
    g_ex.overlapped = overlapped;
    g_ex.completion = completion;
    g_ex.cancel_handle = cancel_handle;
    return g_ex.return_value;
}

hostent g_passthrough_host{};
int g_legacy_calls = 0;

hostent* WSAAPI fake_get_host_by_name(const char*) {
    ++g_legacy_calls;
    return &g_passthrough_host;
}

std::unique_ptr<ADDRINFOEXW> reject_hint_allocation() {
    return nullptr;
}

void CALLBACK fake_completion(DWORD, DWORD, LPWSAOVERLAPPED) {
}

int expect(bool value, const char* name) {
    if (value) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

bool has_export(
    std::span<const char* const> exports,
    const char* name) {
    for (const char* value : exports) {
        if (std::strcmp(value, name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

int main() {
    using namespace gc::nesys_service;
    int failures = 0;

    const auto state_result =
        CreateServerAddressState("10.23.45.67");
    failures += expect(state_result.has_value(), "valid server state");
    const auto& state = *state_result;
    failures += expect(
        state.octets == Ipv4Octets{10, 23, 45, 67} &&
            state.ansi == "10.23.45.67" &&
            state.wide == L"10.23.45.67",
        "canonical server state");

    ADDRINFOW hints{};
    hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED | AI_ALL | AI_CANONNAME;
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    PADDRINFOW result = nullptr;
    g_modern = {};
    const INT modern_status = RedirectGetAddrInfoW(
        state,
        fake_get_addr_info_w,
        L"original.example",
        L"443",
        &hints,
        &result);
    failures += expect(
        modern_status == WSAHOST_NOT_FOUND &&
            g_modern.calls == 1,
        "modern error returned without retry");
    failures += expect(
        std::wcscmp(g_modern.node, L"10.23.45.67") == 0 &&
            std::wcscmp(g_modern.service, L"443") == 0,
        "modern node replaced and service preserved");
    failures += expect(
        g_modern.had_hints &&
            g_modern.hints_value.ai_family == AF_INET &&
            (g_modern.hints_value.ai_flags & AI_NUMERICHOST) != 0 &&
            (g_modern.hints_value.ai_flags &
             (AI_ADDRCONFIG | AI_V4MAPPED | AI_ALL)) == 0 &&
            (g_modern.hints_value.ai_flags & AI_CANONNAME) != 0 &&
            g_modern.hints_value.ai_socktype == SOCK_STREAM &&
            g_modern.hints_value.ai_protocol == IPPROTO_TCP,
        "modern hints normalized");
    failures += expect(
        result == g_modern.result_value,
        "Winsock result ownership preserved");

    g_modern = {};
    result = nullptr;
    RedirectGetAddrInfoW(
        state,
        fake_get_addr_info_w,
        nullptr,
        L"0",
        &hints,
        &result);
    failures += expect(
        g_modern.node == nullptr &&
            g_modern.hints_pointer == &hints,
        "null modern node passes through unchanged");

    AsyncResolverHintCache async_cache;
    ADDRINFOEXW ex_hints{};
    ex_hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED;
    ex_hints.ai_family = AF_INET6;
    ex_hints.ai_socktype = SOCK_DGRAM;
    ex_hints.ai_protocol = IPPROTO_UDP;
    GUID provider{};
    timeval timeout{2, 0};
    OVERLAPPED overlapped{};
    HANDLE cancel_handle = nullptr;
    PADDRINFOEXW ex_result = nullptr;
    g_ex = {};
    const INT ex_status = RedirectGetAddrInfoExW(
        state,
        async_cache,
        fake_get_addr_info_ex_w,
        L"original.example",
        L"12345",
        NS_DNS,
        &provider,
        &ex_hints,
        &ex_result,
        &timeout,
        &overlapped,
        fake_completion,
        &cancel_handle);
    const ADDRINFOEXW* persistent_hints = g_ex.hints_pointer;
    failures += expect(
        ex_status == WSA_IO_PENDING &&
            g_ex.calls == 1 &&
            std::wcscmp(g_ex.node, L"10.23.45.67") == 0,
        "async resolver redirected once");
    failures += expect(
        g_ex.service != nullptr &&
            std::wcscmp(g_ex.service, L"12345") == 0 &&
            g_ex.name_space == NS_DNS &&
            g_ex.provider == &provider &&
            g_ex.timeout == &timeout &&
            g_ex.overlapped == &overlapped &&
            g_ex.completion == fake_completion &&
            g_ex.cancel_handle == &cancel_handle,
        "async arguments preserved");
    failures += expect(
        persistent_hints != &ex_hints &&
            persistent_hints->ai_family == AF_INET &&
            (persistent_hints->ai_flags & AI_NUMERICHOST) != 0 &&
            persistent_hints->ai_socktype == SOCK_DGRAM &&
            persistent_hints->ai_protocol == IPPROTO_UDP,
        "async hints are normalized process storage");
    failures += expect(
        ex_result == g_ex.result_value,
        "async Winsock result ownership preserved");

    g_ex = {};
    RedirectGetAddrInfoExW(
        state,
        async_cache,
        fake_get_addr_info_ex_w,
        L"second.example",
        L"9999",
        NS_DNS,
        &provider,
        &ex_hints,
        &ex_result,
        &timeout,
        &overlapped,
        fake_completion,
        &cancel_handle);
    failures += expect(
        g_ex.hints_pointer == persistent_hints,
        "equal async hint key deduplicated");

    g_ex = {};
    g_ex.return_value = 0;
    ex_result = nullptr;
    failures += expect(
        RedirectGetAddrInfoExW(
            state,
            async_cache,
            fake_get_addr_info_ex_w,
            L"sync.example",
            L"8443",
            NS_DNS,
            &provider,
            &ex_hints,
            &ex_result,
            &timeout,
            nullptr,
            nullptr,
            nullptr) == 0 &&
            g_ex.hints_pointer != &ex_hints &&
            g_ex.hints_value.ai_family == AF_INET &&
            g_ex.overlapped == nullptr,
        "synchronous GetAddrInfoExW uses local normalized hints");

    g_ex = {};
    ex_result = nullptr;
    RedirectGetAddrInfoExW(
        state,
        async_cache,
        fake_get_addr_info_ex_w,
        nullptr,
        L"0",
        NS_DNS,
        &provider,
        &ex_hints,
        &ex_result,
        &timeout,
        &overlapped,
        fake_completion,
        &cancel_handle);
    failures += expect(
        g_ex.node == nullptr &&
            g_ex.hints_pointer == &ex_hints,
        "null GetAddrInfoExW node passes through unchanged");

    std::array<const ADDRINFOEXW*, 8> concurrent{};
    std::array<std::thread, 8> threads;
    for (std::size_t index = 0; index < threads.size(); ++index) {
        threads[index] = std::thread([&, index] {
            concurrent[index] = async_cache.Get(&ex_hints);
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    failures += expect(
        std::all_of(
            concurrent.begin(),
            concurrent.end(),
            [persistent_hints](const ADDRINFOEXW* value) {
                return value == persistent_hints;
            }),
        "concurrent async cache uses stable object");

    AsyncResolverHintCache failing_cache(reject_hint_allocation);
    g_ex = {};
    failures += expect(
        RedirectGetAddrInfoExW(
            state,
            failing_cache,
            fake_get_addr_info_ex_w,
            L"original.example",
            L"80",
            NS_DNS,
            nullptr,
            &ex_hints,
            &ex_result,
            nullptr,
            &overlapped,
            nullptr,
            nullptr) == WSA_NOT_ENOUGH_MEMORY &&
            g_ex.calls == 0,
        "async allocation failure does not call resolver");

    g_legacy_calls = 0;
    hostent* legacy = RedirectGetHostByName(
        state,
        fake_get_host_by_name,
        "original.example");
    failures += expect(
        legacy != nullptr &&
            std::strcmp(legacy->h_name, "original.example") == 0 &&
            legacy->h_aliases[0] == nullptr &&
            legacy->h_addrtype == AF_INET &&
            legacy->h_length == 4 &&
            legacy->h_addr_list[0] != nullptr &&
            legacy->h_addr_list[1] == nullptr &&
            std::memcmp(
                legacy->h_addr_list[0],
                state.octets.data(),
                state.octets.size()) == 0 &&
            g_legacy_calls == 0,
        "legacy synthetic hostent");
    failures += expect(
        RedirectGetHostByName(
            state,
            fake_get_host_by_name,
            nullptr) == &g_passthrough_host &&
            g_legacy_calls == 1,
        "legacy null name pass-through");

    std::uintptr_t other_thread_host = 0;
    std::thread legacy_thread([&] {
        other_thread_host = reinterpret_cast<std::uintptr_t>(
            RedirectGetHostByName(
                state,
                fake_get_host_by_name,
                "thread.example"));
    });
    legacy_thread.join();
    failures += expect(
        other_thread_host != reinterpret_cast<std::uintptr_t>(legacy),
        "legacy hostent is thread-local");

    const auto game_exports =
        ServerAddressHookExports(ProcessRole::Game);
    const auto service_exports =
        ServerAddressHookExports(ProcessRole::Service);
    failures += expect(
        game_exports.size() == 2 &&
            has_export(game_exports, "GetAddrInfoW") &&
            has_export(game_exports, "GetAddrInfoExW"),
        "game resolver inventory");
    failures += expect(
        service_exports.size() == 3 &&
            has_export(service_exports, "gethostbyname"),
        "service resolver inventory");
    failures += expect(
        !has_export(service_exports, "connect") &&
            !has_export(service_exports, "WSAConnect") &&
            !has_export(service_exports, "FreeAddrInfoW") &&
            !has_export(service_exports, "FreeAddrInfoExW"),
        "socket and free APIs remain unhooked");

    return failures == 0 ? 0 : 1;
}

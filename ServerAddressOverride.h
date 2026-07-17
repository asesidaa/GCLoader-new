#pragma once

#include <WinSock2.h>
#include <WS2tcpip.h>

#include "NesysHookTransaction.h"
#include "Nesys/Network/NesysNetworkConfig.h"
#include "NesysServiceProcess.h"

#include <compare>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gc::nesys_service {

using GetAddrInfoWFn = INT(WSAAPI*)(
    PCWSTR,
    PCWSTR,
    const ADDRINFOW*,
    PADDRINFOW*);
using GetAddrInfoExWFn = INT(WSAAPI*)(
    PCWSTR,
    PCWSTR,
    DWORD,
    LPGUID,
    const ADDRINFOEXW*,
    PADDRINFOEXW*,
    timeval*,
    LPOVERLAPPED,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE,
    LPHANDLE);
using GetHostByNameFn = hostent*(WSAAPI*)(const char*);

struct ServerAddressState {
    Ipv4Octets octets{};
    std::string ansi;
    std::wstring wide;
};

std::optional<ServerAddressState> CreateServerAddressState(
    std::string_view configured);

struct ResolverHintKey {
    int flags{0};
    int socket_type{0};
    int protocol{0};

    auto operator<=>(const ResolverHintKey&) const = default;
};

using ResolverHintAllocator =
    std::unique_ptr<ADDRINFOEXW>(*)();

class AsyncResolverHintCache {
public:
    explicit AsyncResolverHintCache(
        ResolverHintAllocator allocator = nullptr) noexcept;

    const ADDRINFOEXW* Get(const ADDRINFOEXW* source) noexcept;

private:
    ResolverHintAllocator allocator_;
    std::mutex mutex_;
    std::map<
        ResolverHintKey,
        std::unique_ptr<ADDRINFOEXW>> entries_;
};

ADDRINFOW NormalizeAddrInfoW(const ADDRINFOW* source) noexcept;
ADDRINFOEXW NormalizeAddrInfoExW(const ADDRINFOEXW* source) noexcept;

INT RedirectGetAddrInfoW(
    const ServerAddressState& state,
    GetAddrInfoWFn original,
    PCWSTR node,
    PCWSTR service,
    const ADDRINFOW* hints,
    PADDRINFOW* result) noexcept;

INT RedirectGetAddrInfoExW(
    const ServerAddressState& state,
    AsyncResolverHintCache& cache,
    GetAddrInfoExWFn original,
    PCWSTR node,
    PCWSTR service,
    DWORD name_space,
    LPGUID provider,
    const ADDRINFOEXW* hints,
    PADDRINFOEXW* result,
    timeval* timeout,
    LPOVERLAPPED overlapped,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE completion,
    LPHANDLE cancel_handle) noexcept;

hostent* RedirectGetHostByName(
    const ServerAddressState& state,
    GetHostByNameFn original,
    const char* requested_name) noexcept;

bool InitializeServerAddressOverride(
    std::string_view configured) noexcept;
std::span<const char* const> ServerAddressHookExports(
    ProcessRole role) noexcept;
void AppendServerAddressHookRequests(
    ProcessRole role,
    std::vector<ApiHookRequest>& requests);

} // namespace gc::nesys_service

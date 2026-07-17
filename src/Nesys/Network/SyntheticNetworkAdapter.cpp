#include "Nesys/Network/SyntheticNetworkAdapter.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cwchar>
#include <iomanip>
#include <string_view>
#include <utility>

#include <safetyhook.hpp>

#include "plog/Log.h"

namespace gc::nesys_service {
namespace {

std::atomic_flag g_adapter_query_logged = ATOMIC_FLAG_INIT;
std::atomic_flag g_notification_logged = ATOMIC_FLAG_INIT;
std::atomic_flag g_mutation_logged = ATOMIC_FLAG_INIT;
safetyhook::MidHook g_service_ping_hook{};
std::atomic_flag g_ping_logged = ATOMIC_FLAG_INIT;

bool read_bytes_safe(
    std::uintptr_t address,
    void* output,
    std::size_t size) noexcept {
    if (address == 0 || output == nullptr || size == 0) {
        return false;
    }

    __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void service_ping_callback(safetyhook::Context& context) noexcept {
    ApplyServicePingTarget(&context.eax);
    if (!g_ping_logged.test_and_set(std::memory_order_relaxed)) {
        try {
            PLOG_INFO
                << "SyntheticNetworkAdapter: first ping-redirection family"
                << " target=" << kServicePingLoopback;
        } catch (...) {
        }
    }
}

void log_first(std::atomic_flag& flag, const char* family) noexcept {
    if (flag.test_and_set(std::memory_order_relaxed)) {
        return;
    }
    try {
        PLOG_INFO << "SyntheticNetworkAdapter: first " << family;
    } catch (...) {
    }
}

template <std::size_t Size>
void copy_ascii(char (&destination)[Size], std::string_view source) noexcept {
    const auto count = std::min(source.size(), Size - 1);
    std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
}

template <std::size_t Size>
void copy_wide(
    wchar_t (&destination)[Size],
    std::wstring_view source) noexcept {
    const auto count = std::min(source.size(), Size - 1);
    std::wmemcpy(destination, source.data(), count);
    destination[count] = L'\0';
}

DWORD prepare_buffer(
    void* output,
    PULONG size_pointer,
    ULONG required,
    DWORD too_small) noexcept {
    if (size_pointer == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }

    const ULONG supplied = *size_pointer;
    *size_pointer = required;
    if (output == nullptr || supplied < required) {
        return too_small;
    }

    std::memset(output, 0, required);
    return NO_ERROR;
}

void fill_address(
    IP_ADDR_STRING& destination,
    std::string_view address,
    std::string_view mask = {}) noexcept {
    destination.Next = nullptr;
    copy_ascii(destination.IpAddress.String, address);
    if (!mask.empty()) {
        copy_ascii(destination.IpMask.String, mask);
    }
    destination.Context = 0;
}

constexpr std::array<const char*, 3> kGameExports{
    "GetAdaptersInfo",
    "NotifyAddrChange",
    "CancelIPChangeNotify",
};

constexpr std::array<const char*, 7> kServiceExports{
    "GetAdaptersInfo",
    "GetIfTable",
    "GetInterfaceInfo",
    "GetNetworkParams",
    "IpReleaseAddress",
    "IpRenewAddress",
    "FlushIpNetTable",
};

} // namespace

ULONG WINAPI SyntheticGetAdaptersInfo(
    PIP_ADAPTER_INFO adapter_info,
    PULONG size_pointer) noexcept {
    log_first(g_adapter_query_logged, "adapter-query family");
    constexpr ULONG required = sizeof(IP_ADAPTER_INFO);
    const DWORD status = prepare_buffer(
        adapter_info,
        size_pointer,
        required,
        ERROR_BUFFER_OVERFLOW);
    if (status != NO_ERROR) {
        return status;
    }

    adapter_info->Next = nullptr;
    adapter_info->ComboIndex = 0;
    copy_ascii(adapter_info->AdapterName, kSyntheticAdapterName);
    copy_ascii(
        adapter_info->Description,
        kSyntheticAdapterDescription);
    adapter_info->AddressLength =
        static_cast<UINT>(kSyntheticMac.size());
    std::memcpy(
        adapter_info->Address,
        kSyntheticMac.data(),
        kSyntheticMac.size());
    adapter_info->Index = kSyntheticInterfaceIndex;
    adapter_info->Type = MIB_IF_TYPE_ETHERNET;
    adapter_info->DhcpEnabled = TRUE;
    adapter_info->CurrentIpAddress = &adapter_info->IpAddressList;
    fill_address(
        adapter_info->IpAddressList,
        kSyntheticIpv4,
        kSyntheticMask);
    fill_address(adapter_info->GatewayList, kSyntheticGateway);
    fill_address(adapter_info->DhcpServer, kSyntheticDhcpServer);
    adapter_info->HaveWins = FALSE;
    adapter_info->LeaseObtained = 0;
    adapter_info->LeaseExpires = static_cast<time_t>(0x7FFFFFFF);
    return NO_ERROR;
}

DWORD WINAPI SyntheticGetIfTable(
    PMIB_IFTABLE table,
    PULONG size_pointer,
    BOOL) noexcept {
    log_first(g_adapter_query_logged, "adapter-query family");
    const ULONG required = SIZEOF_IFTABLE(1);
    const DWORD status = prepare_buffer(
        table,
        size_pointer,
        required,
        ERROR_INSUFFICIENT_BUFFER);
    if (status != NO_ERROR) {
        return status;
    }

    table->dwNumEntries = 1;
    auto& row = table->table[0];
    copy_wide(row.wszName, kSyntheticAdapterNameWide);
    row.dwIndex = kSyntheticInterfaceIndex;
    row.dwType = MIB_IF_TYPE_ETHERNET;
    row.dwMtu = kSyntheticMtu;
    row.dwSpeed = kSyntheticLinkSpeed;
    row.dwPhysAddrLen = static_cast<DWORD>(kSyntheticMac.size());
    std::memcpy(row.bPhysAddr, kSyntheticMac.data(), kSyntheticMac.size());
    row.dwAdminStatus = MIB_IF_ADMIN_STATUS_UP;
    row.dwOperStatus = IF_OPER_STATUS_OPERATIONAL;
    row.dwDescrLen =
        static_cast<DWORD>(std::size(kSyntheticAdapterDescription) - 1);
    std::memcpy(
        row.bDescr,
        kSyntheticAdapterDescription,
        row.dwDescrLen);
    return NO_ERROR;
}

DWORD WINAPI SyntheticGetInterfaceInfo(
    PIP_INTERFACE_INFO interface_info,
    PULONG size_pointer) noexcept {
    log_first(g_adapter_query_logged, "adapter-query family");
    constexpr ULONG required = sizeof(IP_INTERFACE_INFO);
    const DWORD status = prepare_buffer(
        interface_info,
        size_pointer,
        required,
        ERROR_INSUFFICIENT_BUFFER);
    if (status != NO_ERROR) {
        return status;
    }

    interface_info->NumAdapters = 1;
    interface_info->Adapter[0].Index = kSyntheticInterfaceIndex;
    copy_wide(
        interface_info->Adapter[0].Name,
        kSyntheticAdapterNameWide);
    return NO_ERROR;
}

DWORD WINAPI SyntheticGetNetworkParams(
    PFIXED_INFO fixed_info,
    PULONG size_pointer) noexcept {
    log_first(g_adapter_query_logged, "adapter-query family");
    constexpr ULONG required = sizeof(FIXED_INFO);
    const DWORD status = prepare_buffer(
        fixed_info,
        size_pointer,
        required,
        ERROR_BUFFER_OVERFLOW);
    if (status != NO_ERROR) {
        return status;
    }

    copy_ascii(fixed_info->HostName, "GCLoader");
    fixed_info->CurrentDnsServer = &fixed_info->DnsServerList;
    fill_address(fixed_info->DnsServerList, kSyntheticDnsServer);
    fixed_info->NodeType = BROADCAST_NODETYPE;
    fixed_info->EnableRouting = 0;
    fixed_info->EnableProxy = 0;
    fixed_info->EnableDns = 1;
    return NO_ERROR;
}

DWORD WINAPI SyntheticNotifyAddrChange(
    PHANDLE handle,
    LPOVERLAPPED) noexcept {
    log_first(g_notification_logged, "adapter-notification family");
    if (handle != nullptr) {
        *handle = nullptr;
    }
    SetLastError(ERROR_IO_PENDING);
    return ERROR_IO_PENDING;
}

BOOL WINAPI SyntheticCancelIPChangeNotify(LPOVERLAPPED) noexcept {
    log_first(g_notification_logged, "adapter-notification family");
    return TRUE;
}

DWORD WINAPI SuppressIpReleaseAddress(PIP_ADAPTER_INDEX_MAP) noexcept {
    log_first(g_mutation_logged, "mutation-suppression family");
    return NO_ERROR;
}

DWORD WINAPI SuppressIpRenewAddress(PIP_ADAPTER_INDEX_MAP) noexcept {
    log_first(g_mutation_logged, "mutation-suppression family");
    return NO_ERROR;
}

DWORD WINAPI SuppressFlushIpNetTable(DWORD) noexcept {
    log_first(g_mutation_logged, "mutation-suppression family");
    return NO_ERROR;
}

bool ValidateServicePingSignature(
    std::span<const std::uint8_t> bytes) noexcept {
    return bytes.size() >= kServicePingSignature.size() &&
        std::equal(
            kServicePingSignature.begin(),
            kServicePingSignature.end(),
            bytes.begin());
}

void ApplyServicePingTarget(std::uintptr_t* saved_eax) noexcept {
    static_assert(sizeof(void*) == sizeof(std::uint32_t));
    if (saved_eax != nullptr) {
        *saved_eax =
            reinterpret_cast<std::uintptr_t>(kServicePingLoopback);
    }
}

bool PreflightServicePingRedirect(
    std::uintptr_t module_base) noexcept {
    std::array<std::uint8_t, kServicePingSignature.size()> actual{};
    const std::uintptr_t target = module_base + kServicePingRva;
    if (!read_bytes_safe(target, actual.data(), actual.size())) {
        try {
            PLOG_ERROR
                << "SyntheticNetworkAdapter: ping signature read failed"
                << " rva=0x" << std::hex << kServicePingRva << std::dec;
        } catch (...) {
        }
        return false;
    }

    if (ValidateServicePingSignature(actual)) {
        return true;
    }

    std::size_t mismatch = 0;
    while (mismatch < actual.size() &&
           actual[mismatch] == kServicePingSignature[mismatch]) {
        ++mismatch;
    }
    try {
        PLOG_ERROR
            << "SyntheticNetworkAdapter: ping signature mismatch"
            << " rva=0x" << std::hex << kServicePingRva
            << " offset=0x" << mismatch
            << " expected=0x"
            << static_cast<unsigned>(kServicePingSignature[mismatch])
            << " actual=0x" << static_cast<unsigned>(actual[mismatch])
            << std::dec;
    } catch (...) {
    }
    return false;
}

bool PrepareServicePingRedirect(
    std::uintptr_t module_base) noexcept {
    try {
        auto created = safetyhook::MidHook::create(
            reinterpret_cast<void*>(module_base + kServicePingRva),
            service_ping_callback,
            safetyhook::MidHook::StartDisabled);
        if (!created.has_value()) {
            PLOG_ERROR
                << "SyntheticNetworkAdapter: ping hook creation failed"
                << " rva=0x" << std::hex << kServicePingRva << std::dec;
            return false;
        }
        g_service_ping_hook = std::move(*created);
        return static_cast<bool>(g_service_ping_hook) &&
            !g_service_ping_hook.enabled();
    } catch (...) {
        try {
            PLOG_ERROR
                << "SyntheticNetworkAdapter: ping hook threw during creation";
        } catch (...) {
        }
        return false;
    }
}

bool ActivateServicePingRedirect() noexcept {
    try {
        if (!g_service_ping_hook) {
            return false;
        }
        const auto enabled = g_service_ping_hook.enable();
        if (!enabled.has_value()) {
            g_service_ping_hook.reset();
            return false;
        }
        PLOG_INFO
            << "SyntheticNetworkAdapter: service ping hook active"
            << " rva=0x" << std::hex << kServicePingRva << std::dec;
        return true;
    } catch (...) {
        try {
            PLOG_ERROR
                << "SyntheticNetworkAdapter: ping hook threw during enable";
        } catch (...) {
        }
        return false;
    }
}

void RollbackServicePingRedirect() noexcept {
    try {
        g_service_ping_hook.reset();
    } catch (...) {
    }
}

std::span<const char* const> SyntheticAdapterHookExports(
    ProcessRole role) noexcept {
    return role == ProcessRole::Game
        ? std::span<const char* const>{
              kGameExports.data(),
              kGameExports.size()}
        : std::span<const char* const>{
              kServiceExports.data(),
              kServiceExports.size()};
}

void AppendSyntheticAdapterHookRequests(
    ProcessRole role,
    std::vector<ApiHookRequest>& requests) {
    requests.push_back({
        L"iphlpapi.dll",
        "GetAdaptersInfo",
        reinterpret_cast<LPVOID>(&SyntheticGetAdaptersInfo),
        nullptr,
    });

    if (role == ProcessRole::Game) {
        requests.push_back({
            L"iphlpapi.dll",
            "NotifyAddrChange",
            reinterpret_cast<LPVOID>(&SyntheticNotifyAddrChange),
            nullptr,
        });
        requests.push_back({
            L"iphlpapi.dll",
            "CancelIPChangeNotify",
            reinterpret_cast<LPVOID>(&SyntheticCancelIPChangeNotify),
            nullptr,
        });
        return;
    }

    requests.push_back({L"iphlpapi.dll", "GetIfTable",
        reinterpret_cast<LPVOID>(&SyntheticGetIfTable), nullptr});
    requests.push_back({L"iphlpapi.dll", "GetInterfaceInfo",
        reinterpret_cast<LPVOID>(&SyntheticGetInterfaceInfo), nullptr});
    requests.push_back({L"iphlpapi.dll", "GetNetworkParams",
        reinterpret_cast<LPVOID>(&SyntheticGetNetworkParams), nullptr});
    requests.push_back({L"iphlpapi.dll", "IpReleaseAddress",
        reinterpret_cast<LPVOID>(&SuppressIpReleaseAddress), nullptr});
    requests.push_back({L"iphlpapi.dll", "IpRenewAddress",
        reinterpret_cast<LPVOID>(&SuppressIpRenewAddress), nullptr});
    requests.push_back({L"iphlpapi.dll", "FlushIpNetTable",
        reinterpret_cast<LPVOID>(&SuppressFlushIpNetTable), nullptr});
}

} // namespace gc::nesys_service

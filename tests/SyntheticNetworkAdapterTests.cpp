#include "SyntheticNetworkAdapter.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <string>
#include <vector>

namespace {

int expect(bool value, const char* name) {
    if (value) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << "\n";
    return 1;
}

bool tail_is_unchanged(
    const std::vector<std::uint8_t>& buffer,
    std::size_t used) {
    return std::all_of(
        buffer.begin() + static_cast<std::ptrdiff_t>(used),
        buffer.end(),
        [](std::uint8_t value) { return value == 0xCD; });
}

bool has_export(
    std::span<const char* const> exports,
    const char* name) {
    return std::any_of(
        exports.begin(),
        exports.end(),
        [name](const char* value) {
            return std::strcmp(value, name) == 0;
        });
}

} // namespace

int main() {
    using namespace gc::nesys_service;
    int failures = 0;

    ULONG adapter_size = 0;
    failures += expect(
        SyntheticGetAdaptersInfo(nullptr, &adapter_size) ==
            ERROR_BUFFER_OVERFLOW,
        "GetAdaptersInfo size probe");
    failures += expect(
        adapter_size == sizeof(IP_ADAPTER_INFO),
        "GetAdaptersInfo required size");

    std::vector<std::uint8_t> adapter_bytes(adapter_size + 16, 0xCD);
    ULONG short_adapter_size = adapter_size - 1;
    failures += expect(
        SyntheticGetAdaptersInfo(
            reinterpret_cast<PIP_ADAPTER_INFO>(adapter_bytes.data()),
            &short_adapter_size) == ERROR_BUFFER_OVERFLOW,
        "GetAdaptersInfo short buffer");
    failures += expect(
        std::all_of(
            adapter_bytes.begin(),
            adapter_bytes.end(),
            [](std::uint8_t value) { return value == 0xCD; }),
        "GetAdaptersInfo short buffer untouched");

    ULONG exact_adapter_size = adapter_size;
    failures += expect(
        SyntheticGetAdaptersInfo(
            reinterpret_cast<PIP_ADAPTER_INFO>(adapter_bytes.data()),
            &exact_adapter_size) == NO_ERROR,
        "GetAdaptersInfo success");
    const auto* adapter =
        reinterpret_cast<const IP_ADAPTER_INFO*>(adapter_bytes.data());
    failures += expect(adapter->Next == nullptr, "single adapter");
    failures += expect(
        std::strcmp(adapter->AdapterName, kSyntheticAdapterName) == 0,
        "adapter name");
    failures += expect(
        std::strcmp(adapter->Description, kSyntheticAdapterDescription) == 0,
        "adapter description");
    failures += expect(
        adapter->AddressLength == kSyntheticMac.size() &&
            std::memcmp(
                adapter->Address,
                kSyntheticMac.data(),
                kSyntheticMac.size()) == 0,
        "adapter MAC");
    failures += expect(
        adapter->Index == kSyntheticInterfaceIndex,
        "adapter index");
    failures += expect(
        adapter->Type == MIB_IF_TYPE_ETHERNET,
        "adapter Ethernet type");
    failures += expect(adapter->DhcpEnabled == TRUE, "adapter DHCP enabled");
    failures += expect(
        adapter->CurrentIpAddress == &adapter->IpAddressList,
        "current IPv4 pointer");
    failures += expect(
        std::strcmp(
            adapter->IpAddressList.IpAddress.String,
            kSyntheticIpv4) == 0 &&
            std::strcmp(
                adapter->IpAddressList.IpMask.String,
                kSyntheticMask) == 0,
        "adapter IPv4 and mask");
    failures += expect(
        std::strcmp(
            adapter->GatewayList.IpAddress.String,
            kSyntheticGateway) == 0,
        "adapter gateway");
    failures += expect(
        std::strcmp(
            adapter->DhcpServer.IpAddress.String,
            kSyntheticDhcpServer) == 0,
        "adapter DHCP server");
    failures += expect(
        adapter->IpAddressList.Next == nullptr &&
            adapter->GatewayList.Next == nullptr &&
            adapter->DhcpServer.Next == nullptr,
        "adapter lists terminate");
    failures += expect(
        adapter->HaveWins == FALSE &&
            adapter->PrimaryWinsServer.Next == nullptr &&
            adapter->SecondaryWinsServer.Next == nullptr,
        "WINS disabled");
    failures += expect(
        adapter->LeaseObtained == 0 &&
            adapter->LeaseExpires == static_cast<time_t>(0x7FFFFFFF),
        "lease range");
    failures += expect(
        tail_is_unchanged(adapter_bytes, adapter_size),
        "GetAdaptersInfo no overrun");

    ULONG if_table_size = 0;
    failures += expect(
        SyntheticGetIfTable(nullptr, &if_table_size, TRUE) ==
            ERROR_INSUFFICIENT_BUFFER,
        "GetIfTable size probe");
    failures += expect(
        if_table_size == SIZEOF_IFTABLE(1),
        "GetIfTable required size");
    std::vector<std::uint8_t> if_table_bytes(if_table_size + 16, 0xCD);
    ULONG short_if_table_size = if_table_size - 1;
    failures += expect(
        SyntheticGetIfTable(
            reinterpret_cast<PMIB_IFTABLE>(if_table_bytes.data()),
            &short_if_table_size,
            TRUE) == ERROR_INSUFFICIENT_BUFFER &&
            short_if_table_size == if_table_size,
        "GetIfTable short buffer");
    failures += expect(
        std::all_of(
            if_table_bytes.begin(),
            if_table_bytes.end(),
            [](std::uint8_t value) { return value == 0xCD; }),
        "GetIfTable short buffer untouched");
    ULONG exact_if_table_size = if_table_size;
    failures += expect(
        SyntheticGetIfTable(
            reinterpret_cast<PMIB_IFTABLE>(if_table_bytes.data()),
            &exact_if_table_size,
            TRUE) == NO_ERROR,
        "GetIfTable success");
    const auto* if_table =
        reinterpret_cast<const MIB_IFTABLE*>(if_table_bytes.data());
    const auto& row = if_table->table[0];
    failures += expect(if_table->dwNumEntries == 1, "one interface row");
    failures += expect(
        row.dwIndex == kSyntheticInterfaceIndex,
        "row index");
    failures += expect(
        row.dwType == MIB_IF_TYPE_ETHERNET &&
            row.dwAdminStatus == MIB_IF_ADMIN_STATUS_UP &&
            row.dwOperStatus == IF_OPER_STATUS_OPERATIONAL,
        "row Ethernet and up");
    failures += expect(
        row.dwMtu == kSyntheticMtu &&
            row.dwSpeed == kSyntheticLinkSpeed,
        "row MTU and speed");
    failures += expect(
        row.dwPhysAddrLen == kSyntheticMac.size() &&
            std::memcmp(
                row.bPhysAddr,
                kSyntheticMac.data(),
                kSyntheticMac.size()) == 0,
        "row MAC");
    failures += expect(
        std::wcscmp(row.wszName, kSyntheticAdapterNameWide) == 0,
        "row adapter name");
    failures += expect(
        tail_is_unchanged(if_table_bytes, if_table_size),
        "GetIfTable no overrun");

    ULONG interface_size = 0;
    failures += expect(
        SyntheticGetInterfaceInfo(nullptr, &interface_size) ==
            ERROR_INSUFFICIENT_BUFFER,
        "GetInterfaceInfo size probe");
    std::vector<std::uint8_t> interface_bytes(interface_size + 16, 0xCD);
    ULONG short_interface_size = interface_size - 1;
    failures += expect(
        SyntheticGetInterfaceInfo(
            reinterpret_cast<PIP_INTERFACE_INFO>(interface_bytes.data()),
            &short_interface_size) == ERROR_INSUFFICIENT_BUFFER &&
            short_interface_size == interface_size,
        "GetInterfaceInfo short buffer");
    failures += expect(
        std::all_of(
            interface_bytes.begin(),
            interface_bytes.end(),
            [](std::uint8_t value) { return value == 0xCD; }),
        "GetInterfaceInfo short buffer untouched");
    ULONG exact_interface_size = interface_size;
    failures += expect(
        SyntheticGetInterfaceInfo(
            reinterpret_cast<PIP_INTERFACE_INFO>(interface_bytes.data()),
            &exact_interface_size) == NO_ERROR,
        "GetInterfaceInfo success");
    const auto* interface_info =
        reinterpret_cast<const IP_INTERFACE_INFO*>(interface_bytes.data());
    failures += expect(interface_info->NumAdapters == 1, "one interface map");
    failures += expect(
        interface_info->Adapter[0].Index == kSyntheticInterfaceIndex &&
            std::wcscmp(
                interface_info->Adapter[0].Name,
                kSyntheticAdapterNameWide) == 0,
        "interface map identity");
    failures += expect(
        tail_is_unchanged(interface_bytes, interface_size),
        "GetInterfaceInfo no overrun");

    ULONG network_size = 0;
    failures += expect(
        SyntheticGetNetworkParams(nullptr, &network_size) ==
            ERROR_BUFFER_OVERFLOW,
        "GetNetworkParams size probe");
    std::vector<std::uint8_t> network_bytes(network_size + 16, 0xCD);
    ULONG short_network_size = network_size - 1;
    failures += expect(
        SyntheticGetNetworkParams(
            reinterpret_cast<PFIXED_INFO>(network_bytes.data()),
            &short_network_size) == ERROR_BUFFER_OVERFLOW &&
            short_network_size == network_size,
        "GetNetworkParams short buffer");
    failures += expect(
        std::all_of(
            network_bytes.begin(),
            network_bytes.end(),
            [](std::uint8_t value) { return value == 0xCD; }),
        "GetNetworkParams short buffer untouched");
    ULONG exact_network_size = network_size;
    failures += expect(
        SyntheticGetNetworkParams(
            reinterpret_cast<PFIXED_INFO>(network_bytes.data()),
            &exact_network_size) == NO_ERROR,
        "GetNetworkParams success");
    const auto* network =
        reinterpret_cast<const FIXED_INFO*>(network_bytes.data());
    failures += expect(
        std::strcmp(network->HostName, "GCLoader") == 0 &&
            network->DomainName[0] == '\0' &&
            network->ScopeId[0] == '\0',
        "network names");
    failures += expect(
        network->NodeType == BROADCAST_NODETYPE &&
            network->EnableRouting == 0 &&
            network->EnableProxy == 0 &&
            network->EnableDns == 1,
        "network flags");
    failures += expect(
        network->CurrentDnsServer == &network->DnsServerList &&
            network->DnsServerList.Next == nullptr &&
            std::strcmp(
                network->DnsServerList.IpAddress.String,
                kSyntheticDnsServer) == 0,
        "network DNS");
    failures += expect(
        tail_is_unchanged(network_bytes, network_size),
        "GetNetworkParams no overrun");

    HANDLE notification_handle = reinterpret_cast<HANDLE>(0x1234);
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetLastError(ERROR_SUCCESS);
    failures += expect(
        SyntheticNotifyAddrChange(
            &notification_handle,
            &overlapped) == ERROR_IO_PENDING,
        "NotifyAddrChange pending");
    failures += expect(
        notification_handle == nullptr &&
            GetLastError() == ERROR_IO_PENDING &&
            WaitForSingleObject(overlapped.hEvent, 0) == WAIT_TIMEOUT,
        "NotifyAddrChange null handle and unsignaled event");
    failures += expect(
        SyntheticCancelIPChangeNotify(&overlapped) == TRUE,
        "CancelIPChangeNotify success");
    CloseHandle(overlapped.hEvent);

    IP_ADAPTER_INDEX_MAP map{};
    map.Index = kSyntheticInterfaceIndex;
    failures += expect(
        SuppressIpReleaseAddress(&map) == NO_ERROR &&
            SuppressIpRenewAddress(&map) == NO_ERROR &&
            SuppressFlushIpNetTable(map.Index) == NO_ERROR,
        "mutation suppression success");

    auto exact_ping_signature = kServicePingSignature;
    failures += expect(
        ValidateServicePingSignature(exact_ping_signature),
        "exact ping signature");

    auto changed_ping_signature = exact_ping_signature;
    changed_ping_signature[17] ^= 0x01;
    failures += expect(
        !ValidateServicePingSignature(changed_ping_signature),
        "changed target byte rejects ping hook");

    std::vector<std::uint8_t> fake_image(
        kServicePingRva + kServicePingSignature.size() + 32,
        0x5A);
    std::copy(
        kServicePingSignature.begin(),
        kServicePingSignature.end(),
        fake_image.begin() + kServicePingRva);
    auto target_bytes = std::span<const std::uint8_t>{
        fake_image.data() + kServicePingRva,
        kServicePingSignature.size(),
    };
    failures += expect(
        ValidateServicePingSignature(target_bytes),
        "matching target accepts arbitrary surrounding image");
    fake_image[0x20] ^= 0xFF;
    failures += expect(
        ValidateServicePingSignature(target_bytes),
        "unrelated executable change ignored");

    std::uintptr_t saved_eax = 0x12345678;
    ApplyServicePingTarget(&saved_eax);
    failures += expect(
        saved_eax == reinterpret_cast<std::uintptr_t>(kServicePingLoopback),
        "ping target becomes process-lifetime loopback");
    failures += expect(
        std::strcmp(kServicePingLoopback, "127.0.0.1") == 0,
        "ping loopback text");

    const auto game_exports =
        SyntheticAdapterHookExports(ProcessRole::Game);
    const auto service_exports =
        SyntheticAdapterHookExports(ProcessRole::Service);
    failures += expect(
        game_exports.size() == 3 &&
            has_export(game_exports, "GetAdaptersInfo") &&
            has_export(game_exports, "NotifyAddrChange") &&
            has_export(game_exports, "CancelIPChangeNotify"),
        "game adapter hook inventory");
    failures += expect(
        service_exports.size() == 7 &&
            has_export(service_exports, "GetAdaptersInfo") &&
            has_export(service_exports, "GetIfTable") &&
            has_export(service_exports, "GetInterfaceInfo") &&
            has_export(service_exports, "GetNetworkParams") &&
            has_export(service_exports, "IpReleaseAddress") &&
            has_export(service_exports, "IpRenewAddress") &&
            has_export(service_exports, "FlushIpNetTable"),
        "service adapter hook inventory");
    failures += expect(
        !has_export(service_exports, "GetIpNetTable"),
        "GetIpNetTable remains real");

    return failures == 0 ? 0 : 1;
}

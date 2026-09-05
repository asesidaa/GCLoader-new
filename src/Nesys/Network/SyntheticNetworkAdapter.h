#pragma once

#include <WinSock2.h>
#include <RTInfo.h>
#include <Iphlpapi.h>

#include "Platform/Win32/Hooking/HookPlan.h"
#include "Nesys/NesysServiceProcess.h"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace gc::nesys_service {

inline constexpr char kSyntheticAdapterName[] = "GCLoaderNesys0";
inline constexpr wchar_t kSyntheticAdapterNameWide[] = L"GCLoaderNesys0";
inline constexpr char kSyntheticAdapterDescription[] =
    "GCLoader NESYS IPv4 Adapter";
inline constexpr std::array<std::uint8_t, 6> kSyntheticMac{
    0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01,
};
inline constexpr DWORD kSyntheticInterfaceIndex = 0x0BADC0DE;
inline constexpr char kSyntheticIpv4[] = "192.0.2.2";
inline constexpr char kSyntheticMask[] = "255.255.255.0";
inline constexpr char kSyntheticGateway[] = "192.0.2.1";
inline constexpr char kSyntheticDhcpServer[] = "192.0.2.1";
inline constexpr char kSyntheticDnsServer[] = "192.0.2.1";
inline constexpr DWORD kSyntheticMtu = 1500;
inline constexpr DWORD kSyntheticLinkSpeed = 1'000'000'000;
inline constexpr std::uintptr_t kServicePingRva = 0x00008E40;
inline constexpr std::array<std::uint8_t, 32> kServicePingSignature{
    0x51, 0x53, 0x55, 0x56, 0x57, 0x50, 0x8B, 0xD9,
    0x8D, 0x6B, 0x04, 0x6A, 0x10, 0x55, 0xC7, 0x44,
    0x24, 0x1C, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x02,
    0x73, 0x02, 0x00, 0x83, 0xC4, 0x0C, 0x8D, 0x73,
};
inline constexpr char kServicePingLoopback[] = "127.0.0.1";

bool ValidateServicePingSignature(
    std::span<const std::uint8_t> bytes) noexcept;
bool PreflightServicePingRedirect(
    std::uintptr_t module_base) noexcept;
bool PrepareServicePingRedirect(
    std::uintptr_t module_base) noexcept;
bool ActivateServicePingRedirect() noexcept;
void RollbackServicePingRedirect() noexcept;
void ApplyServicePingTarget(std::uintptr_t* saved_eax) noexcept;

ULONG WINAPI SyntheticGetAdaptersInfo(
    PIP_ADAPTER_INFO adapter_info,
    PULONG size_pointer) noexcept;
DWORD WINAPI SyntheticGetIfTable(
    PMIB_IFTABLE table,
    PULONG size_pointer,
    BOOL order) noexcept;
DWORD WINAPI SyntheticGetInterfaceInfo(
    PIP_INTERFACE_INFO interface_info,
    PULONG size_pointer) noexcept;
DWORD WINAPI SyntheticGetNetworkParams(
    PFIXED_INFO fixed_info,
    PULONG size_pointer) noexcept;
DWORD WINAPI SyntheticNotifyAddrChange(
    PHANDLE handle,
    LPOVERLAPPED overlapped) noexcept;
BOOL WINAPI SyntheticCancelIPChangeNotify(
    LPOVERLAPPED overlapped) noexcept;
DWORD WINAPI SuppressIpReleaseAddress(
    PIP_ADAPTER_INDEX_MAP adapter_info) noexcept;
DWORD WINAPI SuppressIpRenewAddress(
    PIP_ADAPTER_INDEX_MAP adapter_info) noexcept;
DWORD WINAPI SuppressFlushIpNetTable(DWORD index) noexcept;

std::span<const char* const> SyntheticAdapterHookExports(
    ProcessRole role) noexcept;
[[nodiscard]] std::expected<void, hooking::HookError> AddSyntheticAdapterHooks(
    ProcessRole role,
    hooking::HookPlan& hooks) noexcept;

} // namespace gc::nesys_service

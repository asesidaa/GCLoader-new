#include "Nesys/Network/ServerAddressOverride.h"

#include <array>
#include <atomic>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstring>
#include <new>
#include <string>
#include <utility>

#include "plog/Log.h"

namespace gc::nesys_service
{
    namespace
    {
        GetAddrInfoWFn g_original_get_addr_info_w = nullptr;
        GetAddrInfoExWFn g_original_get_addr_info_ex_w = nullptr;
        GetHostByNameFn g_original_get_host_by_name = nullptr;
        std::unique_ptr<const ServerAddressState> g_server_address;
        AsyncResolverHintCache g_async_hints;
        std::atomic_flag g_modern_logged = ATOMIC_FLAG_INIT;
        std::atomic_flag g_legacy_logged = ATOMIC_FLAG_INIT;

        constexpr std::array<const char*, 2> kGameExports{
            "GetAddrInfoW",
            "GetAddrInfoExW",
        };
        constexpr std::array<const char*, 3> kServiceExports{
            "GetAddrInfoW",
            "GetAddrInfoExW",
            "gethostbyname",
        };

        int normalized_flags(int flags) noexcept
        {
            constexpr int removed =
                AI_ADDRCONFIG | AI_V4MAPPED | AI_ALL;
            return (flags | AI_NUMERICHOST) & ~removed;
        }

        std::unique_ptr<ADDRINFOEXW> allocate_hint()
        {
            return std::make_unique<ADDRINFOEXW>();
        }

        void log_first(
            std::atomic_flag& flag,
            const char* family,
            const wchar_t* original_node) noexcept
        {
            if (flag.test_and_set(std::memory_order_relaxed))
            {
                return;
            }
            try
            {
                PLOG_INFO << "ServerAddressOverride: first " << family
                    << " node_present=" << (original_node != nullptr);
            }
            catch (...)
            {
            }
        }

        void log_first_legacy(const char* original_name) noexcept
        {
            if (g_legacy_logged.test_and_set(std::memory_order_relaxed))
            {
                return;
            }
            try
            {
                PLOG_INFO << "ServerAddressOverride: first legacy-resolver family"
                    << " original_name="
                    << (original_name != nullptr
                            ? original_name
                            : "<null>");
            }
            catch (...)
            {
            }
        }

INT WSAAPI get_addr_info_w_detour(
            PCWSTR node,
            PCWSTR service,
            const ADDRINFOW* hints,
            PADDRINFOW* result)
        {
            log_first(g_modern_logged, "modern-resolver family", node);
            if (g_server_address == nullptr)
            {
                return WSAEINVAL;
            }
            return RedirectGetAddrInfoW(
                *g_server_address,
                g_original_get_addr_info_w,
                node,
                service,
                hints,
                result);
        }

INT WSAAPI get_addr_info_ex_w_detour(
            PCWSTR node,
            PCWSTR service,
            DWORD name_space,
            LPGUID provider,
            const ADDRINFOEXW* hints,
            PADDRINFOEXW* result,
            timeval* timeout,
            LPOVERLAPPED overlapped,
            LPLOOKUPSERVICE_COMPLETION_ROUTINE completion,
            LPHANDLE cancel_handle)
        {
            log_first(g_modern_logged, "modern-resolver family", node);
            if (g_server_address == nullptr)
            {
                return WSAEINVAL;
            }
            return RedirectGetAddrInfoExW(
                *g_server_address,
                g_async_hints,
                g_original_get_addr_info_ex_w,
                node,
                service,
                name_space,
                provider,
                hints,
                result,
                timeout,
                overlapped,
                completion,
                cancel_handle);
        }

        hostent* WSAAPI get_host_by_name_detour(const char* requested_name)
        {
            log_first_legacy(requested_name);
            if (g_server_address == nullptr)
            {
                return nullptr;
            }
            return RedirectGetHostByName(
                *g_server_address,
                g_original_get_host_by_name,
                requested_name);
        }
    } // namespace

    ADDRINFOW NormalizeAddrInfoW(const ADDRINFOW* source) noexcept
    {
        ADDRINFOW normalized{};
        if (source != nullptr)
        {
            normalized.ai_flags = source->ai_flags;
            normalized.ai_socktype = source->ai_socktype;
            normalized.ai_protocol = source->ai_protocol;
        }
        normalized.ai_flags = normalized_flags(normalized.ai_flags);
        normalized.ai_family = AF_INET;
        return normalized;
    }

    ADDRINFOEXW NormalizeAddrInfoExW(
        const ADDRINFOEXW* source) noexcept
    {
        ADDRINFOEXW normalized{};
        if (source != nullptr)
        {
            normalized.ai_flags = source->ai_flags;
            normalized.ai_socktype = source->ai_socktype;
            normalized.ai_protocol = source->ai_protocol;
        }
        normalized.ai_flags = normalized_flags(normalized.ai_flags);
        normalized.ai_family = AF_INET;
        return normalized;
    }

    AsyncResolverHintCache::AsyncResolverHintCache(
        ResolverHintAllocator allocator) noexcept
        : allocator_(allocator != nullptr ? allocator : allocate_hint)
    {
    }

    const ADDRINFOEXW* AsyncResolverHintCache::Get(
        const ADDRINFOEXW* source) noexcept
    {
        const auto normalized = NormalizeAddrInfoExW(source);
        const ResolverHintKey key{
            normalized.ai_flags,
            normalized.ai_socktype,
            normalized.ai_protocol,
        };

        try
        {
            std::lock_guard lock(mutex_);
            if (const auto existing = entries_.find(key);
                existing != entries_.end())
            {
                return existing->second.get();
            }

            auto entry = allocator_();
            if (entry == nullptr)
            {
                return nullptr;
            }
            *entry = normalized;
            const auto [inserted, was_inserted] =
                entries_.emplace(key, std::move(entry));
            return inserted->second.get();
        }
        catch (const std::bad_alloc&)
        {
            return nullptr;
        }
    }

    INT RedirectGetAddrInfoW(
        const ServerAddressState& state,
        GetAddrInfoWFn original,
        PCWSTR node,
        PCWSTR service,
        const ADDRINFOW* hints,
        PADDRINFOW* result) noexcept
    {
        if (original == nullptr)
        {
            return WSAEINVAL;
        }
        if (node == nullptr)
        {
            return original(node, service, hints, result);
        }

        const ADDRINFOW normalized = NormalizeAddrInfoW(hints);
        return original(
            state.wide().c_str(),
            service,
            &normalized,
            result);
    }

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
        LPHANDLE cancel_handle) noexcept
    {
        if (original == nullptr)
        {
            return WSAEINVAL;
        }
        if (node == nullptr)
        {
            return original(
                node,
                service,
                name_space,
                provider,
                hints,
                result,
                timeout,
                overlapped,
                completion,
                cancel_handle);
        }

        ADDRINFOEXW synchronous_hints{};
        const ADDRINFOEXW* normalized = nullptr;
        if (overlapped == nullptr)
        {
            synchronous_hints = NormalizeAddrInfoExW(hints);
            normalized = &synchronous_hints;
        }
        else
        {
            normalized = cache.Get(hints);
            if (normalized == nullptr)
            {
                return WSA_NOT_ENOUGH_MEMORY;
            }
        }

        return original(
            state.wide().c_str(),
            service,
            name_space,
            provider,
            normalized,
            result,
            timeout,
            overlapped,
            completion,
            cancel_handle);
    }

    hostent* RedirectGetHostByName(
        const ServerAddressState& state,
        GetHostByNameFn original,
        const char* requested_name) noexcept
    {
        if (requested_name == nullptr)
        {
            return original != nullptr ? original(requested_name) : nullptr;
        }

        struct LegacyStorage
        {
            std::string requested;
            std::array<char, 4> address{};
            std::array<char*, 1> aliases{};
            std::array<char*, 2> addresses{};
            hostent value{};
        };
        thread_local LegacyStorage storage;

        try
        {
            storage.requested.assign(requested_name);
        }
        catch (const std::bad_alloc&)
        {
            return nullptr;
        }
        std::memcpy(
            storage.address.data(),
            state.octets().data(),
            state.octets().size());
        storage.aliases = {nullptr};
        storage.addresses = {storage.address.data(), nullptr};
        storage.value.h_name = storage.requested.data();
        storage.value.h_aliases = storage.aliases.data();
        storage.value.h_addrtype = AF_INET;
        storage.value.h_length = 4;
        storage.value.h_addr_list = storage.addresses.data();
        return &storage.value;
    }

    bool InitializeServerAddressOverride(ServerAddressState state) noexcept
    {
        try
        {
            if (g_server_address != nullptr)
            {
                return g_server_address->ansi() == state.ansi();
            }
            g_server_address =
                std::make_unique<const ServerAddressState>(std::move(state));
            PLOG_INFO << "ServerAddressOverride: configured server IPv4="
                << g_server_address->ansi();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    std::span<const char* const> ServerAddressHookExports(
        ProcessRole role) noexcept
    {
        return role == ProcessRole::Game
                   ? std::span<const char* const>{
                       kGameExports.data(),
                       kGameExports.size()
                   }
                   : std::span<const char* const>{
                       kServiceExports.data(),
                       kServiceExports.size()
                   };
    }

    void AppendServerAddressHookRequests(
        ProcessRole role,
        std::vector<ApiHookRequest>& requests)
    {
        requests.push_back({
            L"ws2_32.dll",
            "GetAddrInfoW",
            reinterpret_cast<LPVOID>(&get_addr_info_w_detour),
            reinterpret_cast<LPVOID*>(&g_original_get_addr_info_w),
        });
        requests.push_back({
            L"ws2_32.dll",
            "GetAddrInfoExW",
            reinterpret_cast<LPVOID>(&get_addr_info_ex_w_detour),
            reinterpret_cast<LPVOID*>(&g_original_get_addr_info_ex_w),
        });
        if (role == ProcessRole::Service)
        {
            requests.push_back({
                L"ws2_32.dll",
                "gethostbyname",
                reinterpret_cast<LPVOID>(&get_host_by_name_detour),
                reinterpret_cast<LPVOID*>(&g_original_get_host_by_name),
            });
        }
    }
} // namespace gc::nesys_service

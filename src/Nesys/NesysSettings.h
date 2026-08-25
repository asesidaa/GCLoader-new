#pragma once

#include "Nesys/Network/NesysNetworkConfig.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace gc::config
{
    class ConfigCompiler;
}

namespace gc::nesys_service
{
    class ServerAddressState final
    {
    public:
        [[nodiscard]] const Ipv4Octets& octets() const noexcept
        {
            return octets_;
        }

        [[nodiscard]] const std::string& ansi() const noexcept
        {
            return ansi_;
        }

        [[nodiscard]] const std::wstring& wide() const noexcept
        {
            return wide_;
        }

    private:
        ServerAddressState(
            Ipv4Octets octets,
            std::string ansi,
            std::wstring wide)
            : octets_(octets),
              ansi_(std::move(ansi)),
              wide_(std::move(wide))
        {
        }

        friend class gc::config::ConfigCompiler;
        Ipv4Octets octets_{};
        std::string ansi_;
        std::wstring wide_;
    };

    class RegistryOverrideValues final
    {
    public:
        [[nodiscard]] const std::uint32_t& country() const noexcept
        {
            return country_;
        }

        [[nodiscard]] const std::uint32_t& game_kind() const noexcept
        {
            return game_kind_;
        }

        [[nodiscard]] const std::uint32_t& event_next_time() const noexcept
        {
            return event_next_time_;
        }

        [[nodiscard]] const std::uint32_t& condition_time() const noexcept
        {
            return condition_time_;
        }

        [[nodiscard]] const std::uint32_t& traffic_count() const noexcept
        {
            return traffic_count_;
        }

        [[nodiscard]] const std::uint32_t& log_level() const noexcept
        {
            return log_level_;
        }

        [[nodiscard]] const std::string& news_path() const noexcept
        {
            return news_path_;
        }

        [[nodiscard]] const std::string& event_path() const noexcept
        {
            return event_path_;
        }

        [[nodiscard]] const std::string& log_path() const noexcept
        {
            return log_path_;
        }

    private:
        RegistryOverrideValues(
            std::uint32_t country,
            std::uint32_t game_kind,
            std::uint32_t event_next_time,
            std::uint32_t condition_time,
            std::uint32_t traffic_count,
            std::uint32_t log_level,
            std::string news_path,
            std::string event_path,
            std::string log_path)
            : country_(country),
              game_kind_(game_kind),
              event_next_time_(event_next_time),
              condition_time_(condition_time),
              traffic_count_(traffic_count),
              log_level_(log_level),
              news_path_(std::move(news_path)),
              event_path_(std::move(event_path)),
              log_path_(std::move(log_path))
        {
        }

        friend class gc::config::ConfigCompiler;
        std::uint32_t country_{};
        std::uint32_t game_kind_{};
        std::uint32_t event_next_time_{};
        std::uint32_t condition_time_{};
        std::uint32_t traffic_count_{};
        std::uint32_t log_level_{};
        std::string news_path_;
        std::string event_path_;
        std::string log_path_;
    };

    class NesysSettings final
    {
    public:
        [[nodiscard]] bool adapter_patch_enabled() const noexcept
        {
            return adapter_patch_enabled_;
        }

        [[nodiscard]] const ServerAddressState& server_address() const & noexcept
        {
            return server_address_;
        }

        [[nodiscard]] ServerAddressState server_address() && noexcept
        {
            return std::move(server_address_);
        }

        [[nodiscard]] const std::optional<RegistryOverrideValues>&
        registry_override() const & noexcept
        {
            return registry_override_;
        }

        [[nodiscard]] std::optional<RegistryOverrideValues>
        registry_override() && noexcept
        {
            return std::move(registry_override_);
        }

    private:
        NesysSettings(
            bool adapter_patch_enabled,
            ServerAddressState server_address,
            std::optional<RegistryOverrideValues> registry_override)
            : adapter_patch_enabled_(adapter_patch_enabled),
              server_address_(std::move(server_address)),
              registry_override_(std::move(registry_override))
        {
        }

        friend class gc::config::ConfigCompiler;
        bool adapter_patch_enabled_{};
        ServerAddressState server_address_;
        std::optional<RegistryOverrideValues> registry_override_;
    };
} // namespace gc::nesys_service

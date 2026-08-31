#include "Patches/WindowedWidescreen/WindowedWidescreenPatchTransaction.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace gc::windowed_widescreen
{
    namespace
    {
        [[nodiscard]] bool CheckedAddress(
            const std::uintptr_t image_base,
            const std::uint32_t rva,
            std::uintptr_t& address) noexcept
        {
            if (image_base >
                std::numeric_limits<std::uintptr_t>::max() - rva)
            {
                address = 0;
                return false;
            }
            address = image_base + rva;
            return true;
        }

        [[nodiscard]] const WidescreenByteContract* FindByteContract(
            const std::span<const WidescreenByteContract> contracts,
            const WidescreenContractSite site) noexcept
        {
            const auto found = std::find_if(
                contracts.begin(),
                contracts.end(),
                [site](const WidescreenByteContract& contract)
                {
                    return contract.site == site;
                });
            return found == contracts.end() ? nullptr : &*found;
        }

        [[nodiscard]] bool ActionsAreComplete(
            const WidescreenInstallActions& actions) noexcept
        {
            return actions.context != nullptr && actions.read != nullptr &&
                actions.create_disabled != nullptr &&
                actions.enable != nullptr && actions.reset != nullptr &&
                actions.detach_renderer_resource != nullptr &&
                actions.clear_callback_context != nullptr &&
                actions.publish_owner != nullptr;
        }
    } // namespace

    std::expected<void, WidescreenInstallError>
    InstallWindowedWidescreenHooks(
        const std::uintptr_t image_base,
        const WidescreenContractManifest manifest,
        const std::span<const WidescreenHookRequest> requests,
        const WidescreenInstallActions& actions) noexcept
    {
        if (!ActionsAreComplete(actions))
        {
            return std::unexpected(WidescreenInstallError{
                .stage = WidescreenInstallStage::invalid_actions,
            });
        }
        if (image_base != kWidescreenPreferredImageBase)
        {
            return std::unexpected(WidescreenInstallError{
                .stage = WidescreenInstallStage::unexpected_image_base,
            });
        }
        if (requests.empty() ||
            requests.size() > kMaximumWidescreenHooks)
        {
            return std::unexpected(WidescreenInstallError{
                .stage = requests.empty()
                    ? WidescreenInstallStage::invalid_request
                    : WidescreenInstallStage::capacity_overflow,
            });
        }
        if (manifest.byte_contracts.empty())
        {
            return std::unexpected(WidescreenInstallError{
                .stage = WidescreenInstallStage::invalid_manifest,
            });
        }

        for (std::size_t index = 0;
             index < manifest.byte_contracts.size();
             ++index)
        {
            const auto& contract = manifest.byte_contracts[index];
            std::uintptr_t ignored{};
            if (contract.site == WidescreenContractSite::none ||
                !contract.pattern.valid() ||
                !CheckedAddress(image_base, contract.rva, ignored))
            {
                return std::unexpected(WidescreenInstallError{
                    .stage = !CheckedAddress(
                        image_base, contract.rva, ignored)
                        ? WidescreenInstallStage::address_overflow
                        : WidescreenInstallStage::invalid_manifest,
                    .site = contract.site,
                    .index = index,
                });
            }
            for (std::size_t prior = 0; prior < index; ++prior)
            {
                if (manifest.byte_contracts[prior].site == contract.site)
                {
                    return std::unexpected(WidescreenInstallError{
                        .stage = WidescreenInstallStage::invalid_manifest,
                        .site = contract.site,
                        .index = index,
                    });
                }
            }
        }

        for (std::size_t index = 0;
             index < manifest.pointer_contracts.size();
             ++index)
        {
            const auto& contract = manifest.pointer_contracts[index];
            std::uintptr_t pointer_address{};
            std::uintptr_t target_address{};
            if (contract.site == WidescreenContractSite::none ||
                !CheckedAddress(
                    image_base,
                    contract.pointer_rva,
                    pointer_address) ||
                !CheckedAddress(
                    image_base,
                    contract.target_rva,
                    target_address))
            {
                return std::unexpected(WidescreenInstallError{
                    .stage = WidescreenInstallStage::address_overflow,
                    .site = contract.site,
                    .index = index,
                });
            }
        }

        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            const auto& request = requests[index];
            const auto* contract = FindByteContract(
                manifest.byte_contracts,
                request.site);
            if (request.callback == nullptr || contract == nullptr ||
                contract->hook_kind == WidescreenHookKind::read_only)
            {
                return std::unexpected(WidescreenInstallError{
                    .stage = WidescreenInstallStage::invalid_request,
                    .site = request.site,
                    .index = index,
                });
            }
            for (std::size_t prior = 0; prior < index; ++prior)
            {
                if (requests[prior].site == request.site)
                {
                    return std::unexpected(WidescreenInstallError{
                        .stage = WidescreenInstallStage::invalid_request,
                        .site = request.site,
                        .index = index,
                    });
                }
            }
        }

        std::array<std::byte, kWidescreenMaximumPatternSize> actual{};
        for (std::size_t index = 0;
             index < manifest.byte_contracts.size();
             ++index)
        {
            const auto& contract = manifest.byte_contracts[index];
            std::uintptr_t address{};
            (void)CheckedAddress(image_base, contract.rva, address);
            const auto expected = contract.pattern.view();
            const auto destination =
                std::span{actual}.first(expected.size());
            if (!actions.read(actions.context, address, destination))
            {
                return std::unexpected(WidescreenInstallError{
                    .stage = WidescreenInstallStage::preflight_read,
                    .site = contract.site,
                    .index = index,
                });
            }
            if (!std::ranges::equal(destination, expected))
            {
                return std::unexpected(WidescreenInstallError{
                    .stage = WidescreenInstallStage::byte_mismatch,
                    .site = contract.site,
                    .index = index,
                });
            }
        }

        for (std::size_t index = 0;
             index < manifest.pointer_contracts.size();
             ++index)
        {
            const auto& contract = manifest.pointer_contracts[index];
            std::uintptr_t pointer_address{};
            std::uintptr_t expected_target{};
            (void)CheckedAddress(
                image_base,
                contract.pointer_rva,
                pointer_address);
            (void)CheckedAddress(
                image_base,
                contract.target_rva,
                expected_target);
            std::array<std::byte, sizeof(std::uint32_t)> pointer_bytes{};
            if (!actions.read(
                    actions.context,
                    pointer_address,
                    pointer_bytes))
            {
                return std::unexpected(WidescreenInstallError{
                    .stage = WidescreenInstallStage::preflight_read,
                    .site = contract.site,
                    .index = index,
                });
            }
            std::uint32_t actual_target{};
            std::memcpy(
                &actual_target,
                pointer_bytes.data(),
                sizeof(actual_target));
            if (actual_target != expected_target)
            {
                return std::unexpected(WidescreenInstallError{
                    .stage = WidescreenInstallStage::pointer_mismatch,
                    .site = contract.site,
                    .index = index,
                });
            }
        }

        std::array<WidescreenContractSite, kMaximumWidescreenHooks>
            created_sites{};
        std::size_t created_count{};

        const auto rollback = [&actions, image_base, manifest,
                               &created_sites, &created_count](
                                  const WidescreenInstallStage stage,
                                  const WidescreenContractSite site,
                                  const std::size_t index)
            -> std::expected<void, WidescreenInstallError>
        {
            for (std::size_t offset = created_count; offset > 0; --offset)
            {
                actions.reset(actions.context, created_sites[offset - 1]);
            }
            actions.detach_renderer_resource(actions.context);
            actions.clear_callback_context(actions.context);

            bool verified = true;
            std::array<std::byte, kWidescreenMaximumPatternSize>
                rollback_bytes{};
            for (std::size_t candidate = 0;
                 candidate < created_count;
                 ++candidate)
            {
                const auto* contract = FindByteContract(
                    manifest.byte_contracts,
                    created_sites[candidate]);
                if (contract == nullptr)
                {
                    verified = false;
                    continue;
                }
                std::uintptr_t address{};
                if (!CheckedAddress(image_base, contract->rva, address))
                {
                    verified = false;
                    continue;
                }
                const auto expected = contract->pattern.view();
                const auto destination =
                    std::span{rollback_bytes}.first(expected.size());
                if (!actions.read(
                        actions.context,
                        address,
                        destination) ||
                    !std::ranges::equal(destination, expected))
                {
                    verified = false;
                }
            }

            return std::unexpected(WidescreenInstallError{
                .stage = stage,
                .site = site,
                .index = index,
                .rollback_attempted = true,
                .rollback_complete = verified,
            });
        };

        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            const auto& request = requests[index];
            const auto* contract = FindByteContract(
                manifest.byte_contracts,
                request.site);
            std::uintptr_t address{};
            (void)CheckedAddress(image_base, contract->rva, address);
            if (!actions.create_disabled(
                    actions.context,
                    request.site,
                    contract->hook_kind,
                    address,
                    request.callback))
            {
                return rollback(
                    WidescreenInstallStage::hook_create,
                    request.site,
                    index);
            }
            created_sites[created_count++] = request.site;
        }

        for (std::size_t index = 0; index < requests.size(); ++index)
        {
            if (!actions.enable(actions.context, requests[index].site))
            {
                return rollback(
                    WidescreenInstallStage::hook_enable,
                    requests[index].site,
                    index);
            }
        }

        if (!actions.publish_owner(actions.context))
        {
            return rollback(
                WidescreenInstallStage::owner_publish,
                WidescreenContractSite::none,
                requests.size());
        }
        return {};
    }
} // namespace gc::windowed_widescreen

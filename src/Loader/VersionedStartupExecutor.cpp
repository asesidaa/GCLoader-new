#include "Loader/VersionedStartupExecutor.h"
#include "Patches/GameVersion/VersionedPlanDiagnostics.h"
#include "Platform/Win32/Hooking/HookDiagnostics.h"
#include "plog/Log.h"
#include <format>
#include <type_traits>

namespace gc::loader {
std::expected<void, StartupInstallError> InstallApprovedVersionedPlan(
    const game_version::ApprovedVersionedPlan& plan, const runtime_image::RuntimeImage& image,
    hooking::HookRegistry& registry) noexcept {
    using namespace game_version;
    StartupInstallError current{.stage = StartupInstallStage::image_binding, .context = plan.context()};
    if (plan.image_base() != image.base() || plan.image_size() != image.size())
        return std::unexpected(current);
    try {
        // Validate already resolved every operation and ordered dependencies,
        // install_order, then source ordinal. Never reselect profiles here.
        for (const auto& site : plan.sites()) {
            current = {.stage = StartupInstallStage::operation, .context = plan.context(),
                .operation = site.operation, .address = site.address};
            const auto& contract = site.contract();
            if (site.disposition == SiteDisposition::already_installed) {
                PLOG_INFO << "VersionedStartup: feature=" << FeatureName(contract.feature)
                    << " site=" << contract.site << " state=already_installed";
                continue;
            }
            if (site.disposition == SiteDisposition::verify_only) continue;
            const runtime_image::SiteIdentity identity{FeatureName(contract.feature), contract.site, contract.rva};
            const auto installed = std::visit([&](const auto& operation) -> std::expected<void, StartupInstallError> {
                using T = std::decay_t<decltype(operation)>;
                if constexpr (std::is_same_v<T, BytePatchOperation> ||
                              std::is_same_v<T, GlobalVtableSlotOperation>) {
                    const auto result = [&] {
                        if constexpr (std::is_same_v<T, BytePatchOperation>)
                            return image.Write(identity, operation.replacement, operation.memory_kind);
                        else return image.ExchangePointer(identity, operation.expected, operation.replacement);
                    }();
                    if (!result) { current.memory = result.error(); return std::unexpected(current); }
                } else if constexpr (std::is_same_v<T, InlineHookOperation> ||
                                     std::is_same_v<T, MidHookOperation>) {
                    hooking::HookPlan hook;
                    const auto added = [&] {
                        if constexpr (std::is_same_v<T, InlineHookOperation>)
                            return hook.AddInlineAddress({identity.feature, identity.site}, site.address,
                                operation.detour, operation.original);
                        else return hook.AddMidAddress({identity.feature, identity.site}, site.address, operation.callback);
                    }();
                    if (!added) { current.hook = added.error(); return std::unexpected(current); }
                    const auto resolved = hook.ResolveAndValidate();
                    if (!resolved) { current.hook = resolved.error(); return std::unexpected(current); }
                    const auto result = registry.Install(*resolved);
                    if (!result) { current.hook = result.error(); return std::unexpected(current); }
                }
                return {};
            }, site.operation);
            if (!installed) return installed;
        }
        return {};
    } catch (...) {
        current.stage = StartupInstallStage::exception;
        return std::unexpected(current);
    }
}
diagnostics::FatalProcessReport FormatStartupInstallError(const StartupInstallError& error) {
    auto report = diagnostics::FatalProcessReport{
        std::format("VersionedStartup: installation failed stage={}", static_cast<unsigned>(error.stage)),
        L"GCLoader could not install its validated executable patches. Check the process loader log.",
        L"GCLoader executable installation error"};
    report.log += " " + game_version::FormatPlanContext(error.context);
    if (error.operation) {
        const auto& contract = game_version::ContractOf(*error.operation);
        report.log += std::format(" feature={} site={} rva=0x{:08X} address=0x{:08X} original={} replacement={}",
            game_version::FeatureName(contract.feature), contract.site, contract.rva, error.address,
            game_version::FormatContractBytes(contract.original), game_version::FormatContractBytes(contract.installed));
    }
    if (error.memory) {
        const auto& memory = *error.memory;
        report.log += std::format(" memory_stage={} win32_error={} expected={} observed={} memory_changed={} "
            "restore_attempted={} restore_succeeded={}", runtime_image::MemoryStageName(memory.stage),
            memory.win32_error, game_version::FormatContractBytes(memory.expected),
            game_version::FormatContractBytes(memory.observed), memory.memory_changed,
            memory.restore_attempted, memory.restore_succeeded);
    }
    if (error.hook) report.log += " " + hooking::FormatHookError(*error.hook).log;
    return report;
}
}

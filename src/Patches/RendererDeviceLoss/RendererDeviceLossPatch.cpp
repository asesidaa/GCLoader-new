#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"

#include <Windows.h>

#include <plog/Log.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <limits>
#include <memory>

namespace gc::renderer_device_loss {

namespace {

struct RendererDeviceLossRuntime {
    safetyhook::MidHook vertex_buffer_result_hook{};
};

std::unique_ptr<RendererDeviceLossRuntime> g_runtime_owner;

bool ProductionRead(
    void*,
    std::uintptr_t address,
    std::span<std::byte> output) noexcept {
    if (address == 0 || output.empty()) {
        return false;
    }

    __try {
        std::memcpy(
            output.data(),
            reinterpret_cast<const void*>(address),
            output.size());
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ProductionClearInitialized(
    void*,
    std::uintptr_t renderer,
    std::size_t offset) noexcept {
    if (renderer == 0 || offset != kRendererInitializedOffset ||
        renderer > std::numeric_limits<std::uintptr_t>::max() - offset) {
        return false;
    }

    __try {
        *reinterpret_cast<std::uint8_t*>(renderer + offset) = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void OnVertexBufferCreateResult(
    safetyhook::Context& context) noexcept {
    try {
        static_cast<void>(ApplyRendererDeviceLossRetry(
            context,
            kPreferredImageBase,
            {
                .clear_initialized = ProductionClearInitialized,
            }));
    } catch (...) {
    }
}

bool ProductionInstallHook(
    void* opaque,
    std::uintptr_t address) noexcept {
    if (opaque == nullptr || address == 0) {
        return false;
    }

    try {
        auto& runtime =
            *static_cast<RendererDeviceLossRuntime*>(opaque);
        runtime.vertex_buffer_result_hook = safetyhook::create_mid(
            reinterpret_cast<void*>(address),
            OnVertexBufferCreateResult);
        return static_cast<bool>(runtime.vertex_buffer_result_hook);
    } catch (...) {
        return false;
    }
}

void ProductionResetHook(void* opaque) noexcept {
    if (opaque == nullptr) {
        return;
    }

    try {
        static_cast<RendererDeviceLossRuntime*>(opaque)
            ->vertex_buffer_result_hook.reset();
    } catch (...) {
    }
}

const char* InstallStageName(RendererInstallStage stage) noexcept {
    switch (stage) {
    case RendererInstallStage::None: return "none";
    case RendererInstallStage::InvalidActions: return "invalid_actions";
    case RendererInstallStage::UnexpectedImageBase:
        return "unexpected_image_base";
    case RendererInstallStage::PreflightRead: return "preflight_read";
    case RendererInstallStage::PreflightMismatch:
        return "preflight_mismatch";
    case RendererInstallStage::HookInstall: return "hook_install";
    }
    return "unknown";
}

const char* ContractSiteName(RendererContractSite site) noexcept {
    switch (site) {
    case RendererContractSite::None: return "none";
    case RendererContractSite::VertexBufferResult:
        return "vertex_buffer_result";
    case RendererContractSite::InitializerEpilogue:
        return "initializer_epilogue";
    }
    return "unknown";
}

void LogInstallFailure(const RendererInstallError& error) noexcept {
    try {
        PLOG_ERROR << "RendererDeviceLossPatch: install failed stage="
                   << InstallStageName(error.stage)
                   << " site=" << ContractSiteName(error.site);
    } catch (...) {
    }
}

} // namespace

bool ApplyRendererDeviceLossRetry(
    safetyhook::Context& context,
    std::uintptr_t image_base,
    RendererInitializedWriter writer) noexcept {
    if (static_cast<std::int32_t>(context.eax) >= 0) {
        return false;
    }
    if (context.esi == 0 || writer.clear_initialized == nullptr ||
        image_base != kPreferredImageBase ||
        !writer.clear_initialized(
            writer.context,
            context.esi,
            kRendererInitializedOffset)) {
        return false;
    }

    context.eip = static_cast<std::uint32_t>(
        image_base + kRendererInitializerEpilogueRva);
    return true;
}

std::expected<void, RendererInstallError>
InstallRendererDeviceLossPatch(
    std::uintptr_t image_base,
    RendererInstallActions actions) noexcept {
    if (actions.context == nullptr || actions.read == nullptr ||
        actions.install_hook == nullptr || actions.reset_hook == nullptr) {
        return std::unexpected(RendererInstallError{
            .stage = RendererInstallStage::InvalidActions,
        });
    }
    if (image_base != kPreferredImageBase) {
        return std::unexpected(RendererInstallError{
            .stage = RendererInstallStage::UnexpectedImageBase,
        });
    }

    const auto preflight = [&](RendererContractSite site,
                               std::uint32_t rva,
                               std::span<const std::byte> expected)
        -> std::expected<void, RendererInstallError> {
        std::array<std::byte, 7> actual{};
        if (expected.size() != actual.size() ||
            !actions.read(
                actions.context,
                image_base + rva,
                actual)) {
            return std::unexpected(RendererInstallError{
                .stage = RendererInstallStage::PreflightRead,
                .site = site,
            });
        }
        if (!std::ranges::equal(actual, expected)) {
            return std::unexpected(RendererInstallError{
                .stage = RendererInstallStage::PreflightMismatch,
                .site = site,
            });
        }
        return {};
    };

    auto result_site = preflight(
        RendererContractSite::VertexBufferResult,
        kVertexBufferResultRva,
        kVertexBufferResultPattern);
    if (!result_site) {
        return result_site;
    }

    auto epilogue = preflight(
        RendererContractSite::InitializerEpilogue,
        kRendererInitializerEpilogueRva,
        kRendererEpiloguePattern);
    if (!epilogue) {
        return epilogue;
    }

    if (!actions.install_hook(
            actions.context,
            image_base + kVertexBufferResultRva)) {
        actions.reset_hook(actions.context);
        return std::unexpected(RendererInstallError{
            .stage = RendererInstallStage::HookInstall,
            .site = RendererContractSite::VertexBufferResult,
        });
    }
    return {};
}

bool RendererDeviceLossPatchInit() noexcept {
    static std::atomic_bool attempted{false};
    static std::atomic_bool stored_result{false};
    bool expected = false;
    if (!attempted.compare_exchange_strong(expected, true)) {
        return stored_result.load(std::memory_order_acquire);
    }

    try {
        const auto module = GetModuleHandleW(nullptr);
        const auto image_base = reinterpret_cast<std::uintptr_t>(module);
        auto candidate = std::make_unique<RendererDeviceLossRuntime>();
        auto installed = InstallRendererDeviceLossPatch(
            image_base,
            {
                .context = candidate.get(),
                .read = ProductionRead,
                .install_hook = ProductionInstallHook,
                .reset_hook = ProductionResetHook,
            });
        if (!installed) {
            LogInstallFailure(installed.error());
            stored_result.store(false, std::memory_order_release);
            return false;
        }

        g_runtime_owner = std::move(candidate);
        stored_result.store(true, std::memory_order_release);
        try {
            PLOG_INFO << "RendererDeviceLossPatch: hook committed"
                      << " result_rva=0x" << std::hex
                      << kVertexBufferResultRva
                      << " retry_rva=0x"
                      << kRendererInitializerEpilogueRva << std::dec;
        } catch (...) {
        }
        return true;
    } catch (...) {
        try {
            PLOG_ERROR
                << "RendererDeviceLossPatch: initialization exception"
                << " before publish";
        } catch (...) {
        }
        stored_result.store(false, std::memory_order_release);
        return false;
    }
}

} // namespace gc::renderer_device_loss

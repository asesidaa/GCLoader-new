#include "Patches/RendererDeviceLoss/RendererDeviceLossPatch.h"

namespace gc::renderer_device_loss {

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

} // namespace gc::renderer_device_loss

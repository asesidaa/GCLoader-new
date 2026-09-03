#pragma once

#include <cstdint>

namespace gc::auto_play
{
    using NativeDebugTextFunction = int(__cdecl*)(
        float,
        float,
        std::uint32_t,
        const char*,
        ...);

    struct AutoPlayMarkerTextActions
    {
        void* context{};
        bool (*queue)(
            void*,
            float,
            float,
            std::uint32_t,
            const char*) noexcept{};
    };

    enum class AutoPlayMarkerFrameResult : std::uint8_t
    {
        inactive,
        queued,
        invalid_actions,
        native_text_failure,
    };

    [[nodiscard]] AutoPlayMarkerFrameResult ProduceAutoPlayMarkerFrame(
        bool active,
        const AutoPlayMarkerTextActions& actions) noexcept;

    [[nodiscard]] bool CallNativeDebugTextGuarded(
        NativeDebugTextFunction function,
        float x,
        float y,
        std::uint32_t argb,
        const char* text) noexcept;
} // namespace gc::auto_play

#include "AutoPlayMarker.h"

#include <Windows.h>

#include <array>

namespace gc::auto_play
{
    namespace
    {
        struct MarkerTextCall
        {
            float x;
            float y;
            std::uint32_t argb;
            const char* text;
        };

        constexpr std::array kMarkerTextCalls{
            MarkerTextCall{34.0F, 34.0F, 0xFF000000U, "AUTO PLAY"},
            MarkerTextCall{
                34.0F,
                54.0F,
                0xFF000000U,
                "SCORE SAVE DISABLED"},
            MarkerTextCall{32.0F, 32.0F, 0xFFFFFF00U, "AUTO PLAY"},
            MarkerTextCall{
                32.0F,
                52.0F,
                0xFFFFFF00U,
                "SCORE SAVE DISABLED"},
        };

        [[nodiscard]] bool CallNativeDebugText(
            const NativeDebugTextFunction function,
            const MarkerTextCall& call) noexcept
        {
#if defined(_MSC_VER)
            __try
            {
                (void)function(
                    call.x,
                    call.y,
                    call.argb,
                    "%s",
                    call.text);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
#else
#error "Native auto play requires the MSVC x86 SEH boundary"
#endif
        }
    } // namespace

    bool DrawAutoPlayMarker(
        const NativeDebugTextFunction function) noexcept
    {
        if (function == nullptr)
        {
            return false;
        }

        for (const auto& call : kMarkerTextCalls)
        {
            if (!CallNativeDebugText(function, call))
            {
                return false;
            }
        }
        return true;
    }
} // namespace gc::auto_play

#include "Patches/AutoPlay/AutoPlayMarker.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

// Marker oracle: docs/superpowers/specs/
// 2026-09-03-native-auto-play-safety-design.md, Visible Marker Contract.
// Native ABI evidence: game471-debug-text-outer-frame-trace.json,
// SHA-256 FD2FBB6475A0368B8C7DE1356F7F16E74DB7F2612481318FB674AB3998B9BBE7.

namespace
{
    int g_failures{};

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    struct TextCall
    {
        float x{};
        float y{};
        std::uint32_t argb{};
        std::string_view text;
    };

    struct TextRecorder
    {
        std::array<TextCall, 4> calls{};
        std::size_t count{};
        std::size_t fail_at{std::numeric_limits<std::size_t>::max()};
    };

    bool RecordText(
        void* context,
        const float x,
        const float y,
        const std::uint32_t argb,
        const char* text) noexcept
    {
        auto& recorder = *static_cast<TextRecorder*>(context);
        if (recorder.count == recorder.fail_at)
        {
            return false;
        }
        if (recorder.count >= recorder.calls.size() || text == nullptr)
        {
            return false;
        }
        recorder.calls[recorder.count++] = {x, y, argb, text};
        return true;
    }

    void MarkerProducerIsInactiveOrEmitsTheFixedContract()
    {
        using enum gc::auto_play::AutoPlayMarkerFrameResult;

        const auto inactive_result =
            gc::auto_play::ProduceAutoPlayMarkerFrame(false, {});
        Expect(
            inactive_result == inactive,
            "inactive marker returns without requiring actions");

        TextRecorder recorder{};
        const gc::auto_play::AutoPlayMarkerTextActions actions{
            .context = &recorder,
            .queue = RecordText,
        };
        const auto queued_result =
            gc::auto_play::ProduceAutoPlayMarkerFrame(true, actions);
        Expect(queued_result == queued, "active marker queues its complete frame");
        Expect(recorder.count == 4, "active marker queues exactly four calls");

        constexpr std::array<TextCall, 4> expected{
            TextCall{34.0F, 34.0F, 0xFF000000U, "AUTO PLAY"},
            TextCall{34.0F, 54.0F, 0xFF000000U, "SCORE SAVE DISABLED"},
            TextCall{32.0F, 32.0F, 0xFFFFFF00U, "AUTO PLAY"},
            TextCall{32.0F, 52.0F, 0xFFFFFF00U, "SCORE SAVE DISABLED"},
        };
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            Expect(
                recorder.calls[index].x == expected[index].x &&
                    recorder.calls[index].y == expected[index].y &&
                    recorder.calls[index].argb == expected[index].argb &&
                    recorder.calls[index].text == expected[index].text,
                "marker call matches the fixed visible contract");
        }

        const auto invalid_result =
            gc::auto_play::ProduceAutoPlayMarkerFrame(true, {});
        Expect(
            invalid_result == invalid_actions,
            "active marker rejects an incomplete text action");

        TextRecorder failing_recorder{
            .fail_at = 2,
        };
        const auto failing_result = gc::auto_play::ProduceAutoPlayMarkerFrame(
            true,
            {
                .context = &failing_recorder,
                .queue = RecordText,
            });
        Expect(
            failing_result == native_text_failure,
            "marker reports the first native text failure");
        Expect(
            failing_recorder.count == 2,
            "marker stops after the first native text failure");
    }
} // namespace

int main()
{
    MarkerProducerIsInactiveOrEmitsTheFixedContract();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

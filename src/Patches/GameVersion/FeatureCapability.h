#pragma once
#include <cstdint>
namespace gc::game_version {
enum class FeatureId : std::uint8_t {
    game_compatibility, auto_play, song_unlock, switch_input, absolute_judgement,
    framerate, countdown, test_mode_timing, renderer_device_loss,
    windowed_widescreen, asio_close, nesys_ping,
};
enum class Capability : std::uint8_t { unavailable, supported };
struct FeatureRequirement final {
    FeatureId feature;
    bool mandatory{};
    bool enabled{};
};
[[nodiscard]] const char* FeatureName(FeatureId) noexcept;
}

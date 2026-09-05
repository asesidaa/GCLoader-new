#pragma once
#include "Config/ConfigDocument.h"
#include "Config/Validation/ValidationContext.h"

namespace gc::config::validation {
struct ExperimentalValidationResult final {
    std::uint32_t target_fps{};
    std::uint64_t widescreen_width{};
    std::uint64_t widescreen_height{};
    windowed_widescreen::GameplayHudPlacement widescreen_hud_placement{};
    audio::AudioBackend audio_backend{};
};
[[nodiscard]] ExperimentalValidationResult ValidateExperimental(
    const ConfigDocument&, bool input_poll_valid, ValidationContext&);
}

#pragma once
namespace gc::config { class ValidatedConfig; }
namespace gc::switch_input { class SwitchInputSettings; }
namespace gc::framerate { class FramerateSettings; }
namespace gc::audio { enum class AudioBackend : unsigned char; }
namespace gc::absolute_judgement { class JudgementSettings; }
namespace gc::loader {
// Removed at Plan09's single global-startup cutover.
void InstallTransitionalFramerate(const framerate::FramerateSettings&, audio::AudioBackend) noexcept;
void InstallTransitionalTestModeTiming() noexcept;
void InstallTransitionalGameCompatibility() noexcept;
void InstallTransitionalOptionalPatches(const config::ValidatedConfig&) noexcept;
void InstallTransitionalSwitchInput(const switch_input::SwitchInputSettings&) noexcept;
void InstallTransitionalAbsoluteJudgement(const absolute_judgement::JudgementSettings&) noexcept;
}

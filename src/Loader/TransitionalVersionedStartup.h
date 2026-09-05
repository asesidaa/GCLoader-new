#pragma once
namespace gc::config { class ValidatedConfig; }
namespace gc::switch_input { class SwitchInputSettings; }
namespace gc::framerate { class FramerateSettings; }
namespace gc::audio { enum class AudioBackend : unsigned char; }
namespace gc::absolute_judgement { class JudgementSettings; }
namespace gc::windowed_widescreen { class WindowedWidescreenSettings; }
namespace gc::loader {
void InstallTransitionalAsioClose(audio::AudioBackend) noexcept;
void InstallTransitionalWidescreen(const windowed_widescreen::WindowedWidescreenSettings&) noexcept;
// Removed at Plan09's single global-startup cutover.
void InstallTransitionalFramerate(const framerate::FramerateSettings&, audio::AudioBackend) noexcept;
void InstallTransitionalTestModeTiming() noexcept;
void InstallTransitionalRendererDeviceLoss() noexcept;
void InstallTransitionalGameCompatibility() noexcept;
void InstallTransitionalOptionalPatches(const config::ValidatedConfig&) noexcept;
void InstallTransitionalSwitchInput(const switch_input::SwitchInputSettings&) noexcept;
void InstallTransitionalAbsoluteJudgement(const absolute_judgement::JudgementSettings&) noexcept;
}

#pragma once
namespace gc::config { class ValidatedConfig; }
namespace gc::switch_input { class SwitchInputSettings; }
namespace gc::loader {
// Removed at Plan09's single global-startup cutover.
void InstallTransitionalGameCompatibility() noexcept;
void InstallTransitionalOptionalPatches(const config::ValidatedConfig&) noexcept;
void InstallTransitionalSwitchInput(const switch_input::SwitchInputSettings&) noexcept;
}

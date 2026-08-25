#pragma once

#include "Config/NativeInputConfig.h"
#include "Input/Types/InputTypes.h"
#include "Input/Win32/InputCapture.h"

// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <vector>

class InputEditorModel {
public:
    explicit InputEditorModel(gc::config::ControllerConfig config);

    void SetAvailableIdentities(
        std::vector<gc::input::ControllerIdentity> identities);
    void SelectIdentity(gc::input::ControllerIdentity identity);

    [[nodiscard]] gc::input::ControllerIdentity
    SelectedIdentity() const;
    [[nodiscard]] bool selected_identity_available() const noexcept;

    [[nodiscard]] std::expected<void, std::string> AddBinding(
        gc::input::DigitalControlBinding binding);
    [[nodiscard]] std::expected<void, std::string> ReplaceBinding(
        std::size_t index,
        gc::input::DigitalControlBinding binding);
    [[nodiscard]] std::expected<void, std::string> RemoveBinding(
        std::size_t index);
    [[nodiscard]] std::expected<void, std::string> AcceptCapture(
        const gc::input::CaptureResult& capture,
        std::optional<std::size_t> replacement_index);

    [[nodiscard]] gc::config::ControllerConfig& config() noexcept;
    [[nodiscard]] const gc::config::ControllerConfig& config() const noexcept;

private:
    [[nodiscard]] static std::expected<void, std::string> Validate(
        const gc::config::ControllerConfig& config);

    gc::config::ControllerConfig config_;
    std::vector<gc::input::ControllerIdentity> available_identities_;
};

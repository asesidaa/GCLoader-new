#include "InputEditorModel.h"

#include <algorithm>
#include <utility>
#include <variant>

InputEditorModel::InputEditorModel(gc::config::ControllerConfig config)
    : config_(std::move(config))
{
}

void InputEditorModel::SetAvailableIdentities(
    std::vector<gc::input::ControllerIdentity> identities)
{
    available_identities_ = std::move(identities);
}

void InputEditorModel::SelectIdentity(gc::input::ControllerIdentity identity)
{
    if (identity == SelectedIdentity())
    {
        return;
    }

    config_.backend = identity.backend;
    config_.device_id = std::move(identity.device_id);
    config_.bindings().clear();
}

gc::input::ControllerIdentity InputEditorModel::SelectedIdentity() const
{
    return {
        .backend = config_.backend(),
        .device_id = config_.device_id(),
    };
}

bool InputEditorModel::selected_identity_available() const noexcept
{
    const auto selected = SelectedIdentity();
    return std::ranges::find(available_identities_, selected) !=
        available_identities_.end();
}

std::expected<void, std::string> InputEditorModel::AddBinding(
    gc::input::DigitalControlBinding binding)
{
    auto candidate = config_;
    candidate.bindings().push_back(std::move(binding));
    if (const auto validation = Validate(candidate); !validation)
    {
        return std::unexpected(validation.error());
    }
    config_ = std::move(candidate);
    return {};
}

std::expected<void, std::string> InputEditorModel::ReplaceBinding(
    std::size_t index,
    gc::input::DigitalControlBinding binding)
{
    if (index >= config_.bindings().size())
    {
        return std::unexpected("Controller binding index is out of range");
    }

    auto candidate = config_;
    candidate.bindings()[index] = std::move(binding);
    if (const auto validation = Validate(candidate); !validation)
    {
        return std::unexpected(validation.error());
    }
    config_ = std::move(candidate);
    return {};
}

std::expected<void, std::string> InputEditorModel::RemoveBinding(
    std::size_t index)
{
    if (index >= config_.bindings().size())
    {
        return std::unexpected("Controller binding index is out of range");
    }
    config_.bindings().erase(
        config_.bindings().begin() + static_cast<std::ptrdiff_t>(index));
    return {};
}

std::expected<void, std::string> InputEditorModel::AcceptCapture(
    const gc::input::CaptureResult& capture,
    std::optional<std::size_t> replacement_index)
{
    if (!capture.controller_identity ||
        *capture.controller_identity != SelectedIdentity())
    {
        return std::unexpected(
            "Captured controller identity does not match the selected device");
    }
    const auto* binding =
        std::get_if<gc::input::DigitalControlBinding>(&capture.value);
    if (binding == nullptr)
    {
        return std::unexpected(
            "Controller binding capture did not contain a controller control");
    }
    if (replacement_index)
    {
        return ReplaceBinding(*replacement_index, *binding);
    }
    return AddBinding(*binding);
}

gc::config::ControllerConfig& InputEditorModel::config() noexcept
{
    return config_;
}

const gc::config::ControllerConfig& InputEditorModel::config() const noexcept
{
    return config_;
}

std::expected<void, std::string> InputEditorModel::Validate(
    const gc::config::ControllerConfig& config)
{
    return gc::config::ValidateNativeInputFields(
        gc::config::kInputSchemaVersion,
        1000,
        50,
        40,
        gc::config::NativeKeyboardConfig{},
        config);
}

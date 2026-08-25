#pragma once
// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriverCatalog.h"
#include "Audio/Asio/AsioControlPanel.h"
#include "Audio/Asio/AsioProbeClient.h"
#include "Config/ConfigDocument.h"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::array<std::string_view, 6>
kCommonAsioDriverNames{
    "XONAR SOUND CARD",
    "ASIO4ALL v2",
    "FlexASIO",
    "KoordASIO",
    "FL Studio ASIO",
    "Generic Low Latency ASIO Driver",
};

enum class AsioCatalogState : std::uint8_t
{
    available,
    empty,
    failed,
};

enum class AsioInspectionState : std::uint8_t
{
    idle,
    probing,
    valid,
    failed,
};

struct AsioChannelPairChoice
{
    std::uint32_t base_channel{};
    std::string label;
};

class AudioBackendEditorModel final
{
public:
    explicit AudioBackendEditorModel(
        gc::config::ConfigDocument& config) noexcept;

    void ApplyCatalog(std::expected<
        std::vector<gc::audio::AsioDriverRegistration>,
        gc::audio::AsioFailure> catalog);

    [[nodiscard]] bool asio_selection_enabled() const noexcept;
    [[nodiscard]] AsioCatalogState catalog_state() const noexcept;
    [[nodiscard]] const std::optional<std::string>&
    catalog_error() const noexcept;
    [[nodiscard]] const std::vector<std::string>&
    driver_suggestions() const noexcept;

    void SetBackend(gc::audio::AudioBackend backend) noexcept;
    void SetDriverName(std::string name);
    void SetBufferFrames(std::uint32_t frames) noexcept;
    void SetOutputBaseChannel(std::uint32_t channel) noexcept;
    void NotifyConfigReloaded() noexcept;

    [[nodiscard]] std::expected<gc::audio::AsioProbeRequest, std::string>
    BeginInspection();
    [[nodiscard]] std::expected<
        gc::audio::AsioControlPanelRequest,
        std::string>
    BeginControlPanel();
    void CompleteInspection(gc::audio::AsioProbeResult result);

    [[nodiscard]] AsioInspectionState
    inspection_state() const noexcept;
    [[nodiscard]] const std::string& inspection_error() const noexcept;
    [[nodiscard]] const std::optional<gc::audio::AsioCapabilityReport>&
    capability_report() const noexcept;
    [[nodiscard]] const std::vector<AsioChannelPairChoice>&
    channel_pairs() const noexcept;

private:
    void InvalidateInspection() noexcept;
    void RebuildSuggestions();

    gc::config::ConfigDocument* config_{};
    AsioCatalogState catalog_state_{AsioCatalogState::empty};
    std::vector<gc::audio::AsioDriverRegistration> installed_;
    std::vector<std::string> suggestions_;
    std::optional<std::string> catalog_error_;
    AsioInspectionState inspection_state_{AsioInspectionState::idle};
    std::string inspection_error_;
    std::optional<gc::audio::AsioCapabilityReport> report_;
    std::vector<AsioChannelPairChoice> channel_pairs_;
};

[[nodiscard]] std::string DescribeAsioFailure(
    const gc::audio::AsioFailure& failure);

[[nodiscard]] std::expected<void, std::string> ValidateAndWriteConfig(
    const std::filesystem::path& path,
    const gc::config::ConfigDocument& config,
    gc::audio::IAsioProbeClient& asio_probe,
    const gc::config::AtomicConfigWriteActions& write_actions) noexcept;

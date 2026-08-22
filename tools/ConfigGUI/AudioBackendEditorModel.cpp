// SPDX-License-Identifier: CC0-1.0

#include "AudioBackendEditorModel.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::optional<std::wstring> Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return std::wstring{};
    }
    const auto count = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (count <= 0) {
        return std::nullopt;
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            wide.data(),
            count) != count) {
        return std::nullopt;
    }
    return wide;
}

bool EqualInsensitive(
    std::string_view left,
    std::string_view right) noexcept {
    try {
        const auto left_wide = Utf8ToWide(left);
        const auto right_wide = Utf8ToWide(right);
        if (!left_wide || !right_wide) {
            return left == right;
        }
        return CompareStringOrdinal(
                   left_wide->data(),
                   static_cast<int>(left_wide->size()),
                   right_wide->data(),
                   static_cast<int>(right_wide->size()),
                   TRUE) == CSTR_EQUAL;
    } catch (...) {
        return left == right;
    }
}

const char* FailureStageName(gc::audio::AsioFailureStage stage) noexcept {
    using enum gc::audio::AsioFailureStage;
    switch (stage) {
    case none: return "none";
    case registry: return "registry";
    case clsid: return "clsid";
    case com: return "com";
    case init: return "init";
    case identity: return "identity";
    case channels: return "channels";
    case sample_rate: return "sample_rate";
    case buffer_metadata: return "buffer_metadata";
    case channel_info: return "channel_info";
    case output_ready_probe: return "output_ready_probe";
    case callback_prepare: return "callback_prepare";
    case create_buffers: return "create_buffers";
    case latency: return "latency";
    case render_core: return "render_core";
    case start: return "start";
    case startup_clock: return "startup_clock";
    case callback: return "callback";
    case conversion: return "conversion";
    case runtime_clock: return "runtime_clock";
    case output_ready: return "output_ready";
    case stop: return "stop";
    case dispose: return "dispose";
    case restore_sample_rate: return "restore_sample_rate";
    case protocol: return "protocol";
    case process_launch: return "process_launch";
    case process_job: return "process_job";
    case probe_timeout: return "probe_timeout";
    case probe_crash: return "probe_crash";
    case control_panel: return "control_panel";
    case control_panel_crash: return "control_panel_crash";
    case multimedia_timer: return "multimedia_timer";
    }
    return "unknown";
}

const char* ResultDomainName(gc::audio::AsioResultDomain domain) noexcept {
    using enum gc::audio::AsioResultDomain;
    switch (domain) {
    case none: return "none";
    case asio: return "asio";
    case hresult: return "hresult";
    case win32: return "win32";
    case winmm: return "winmm";
    }
    return "unknown";
}

const char* SampleTypeName(ASIOSampleType type) noexcept {
    switch (type) {
    case ASIOSTInt16LSB: return "Int16LSB";
    case ASIOSTInt24LSB: return "Int24LSB";
    case ASIOSTInt32LSB: return "Int32LSB";
    case ASIOSTFloat32LSB: return "Float32LSB";
    case ASIOSTFloat64LSB: return "Float64LSB";
    case ASIOSTInt32LSB16: return "Int32LSB16";
    case ASIOSTInt32LSB18: return "Int32LSB18";
    case ASIOSTInt32LSB20: return "Int32LSB20";
    case ASIOSTInt32LSB24: return "Int32LSB24";
    default: return "Unsupported";
    }
}

std::vector<AsioChannelPairChoice> BuildChannelPairs(
    const gc::audio::AsioCapabilityReport& report) {
    std::vector<AsioChannelPairChoice> pairs;
    if (report.output_channels.size() < 2) {
        return pairs;
    }
    pairs.reserve(report.output_channels.size() - 1);
    for (std::size_t index = 0;
         index + 1 < report.output_channels.size();
         ++index) {
        const auto& left = report.output_channels[index];
        const auto& right = report.output_channels[index + 1];
        if (right.index != left.index + 1) {
            continue;
        }
        std::ostringstream label;
        label << '[' << left.index << '/' << right.index << "] "
              << left.name << " (" << SampleTypeName(left.sample_type)
              << ") + " << right.name << " ("
              << SampleTypeName(right.sample_type) << ')';
        pairs.push_back({left.index, label.str()});
    }
    return pairs;
}

std::expected<void, std::string> ValidateCapabilityForConfig(
    const InputConfig& config,
    const gc::audio::AsioCapabilityReport& report) {
    const auto& experimental = config.experimental();
    const auto requested_frames = static_cast<std::uint32_t>(
        experimental.asio_buffer_frames());
    const auto requested_base = static_cast<std::uint32_t>(
        experimental.asio_output_base_channel());
    if (!EqualInsensitive(
            report.registration.registry_name,
            experimental.asio_driver_name())) {
        return std::unexpected(
            "ASIO helper returned a different registry driver name");
    }
    if (report.sample_rate != 48'000.0) {
        return std::unexpected(
            "ASIO helper did not validate exact 48 kHz output");
    }
    if (report.effective_buffer_frames != requested_frames) {
        return std::unexpected(
            "ASIO helper returned a different effective buffer frame count");
    }
    if (report.selected_base_channel != requested_base) {
        return std::unexpected(
            "ASIO helper returned a different output base channel");
    }
    const auto pairs = BuildChannelPairs(report);
    const auto pair = std::ranges::find(
        pairs,
        requested_base,
        &AsioChannelPairChoice::base_channel);
    if (pair == pairs.end()) {
        return std::unexpected(
            "ASIO output base channel is not an adjacent output pair");
    }
    const auto descriptor = std::ranges::find(
        report.output_channels,
        requested_base,
        &gc::audio::AsioChannelDescriptor::index);
    const auto right_descriptor = std::ranges::find(
        report.output_channels,
        requested_base + 1,
        &gc::audio::AsioChannelDescriptor::index);
    if (descriptor == report.output_channels.end() ||
        right_descriptor == report.output_channels.end() ||
        std::string_view{SampleTypeName(descriptor->sample_type)} ==
            "Unsupported" ||
        std::string_view{SampleTypeName(right_descriptor->sample_type)} ==
            "Unsupported") {
        return std::unexpected(
            "ASIO adjacent output pair uses an unsupported sample format");
    }
    return {};
}

} // namespace

AudioBackendEditorModel::AudioBackendEditorModel(
    InputConfig& config) noexcept
    : config_(&config) {
    RebuildSuggestions();
}

void AudioBackendEditorModel::ApplyCatalog(std::expected<
    std::vector<gc::audio::AsioDriverRegistration>,
    gc::audio::AsioFailure> catalog) {
    installed_.clear();
    catalog_error_.reset();
    if (!catalog.has_value()) {
        catalog_state_ = AsioCatalogState::failed;
        catalog_error_ = DescribeAsioFailure(catalog.error());
    } else {
        installed_ = std::move(*catalog);
        catalog_state_ = installed_.empty()
            ? AsioCatalogState::empty
            : AsioCatalogState::available;
    }
    RebuildSuggestions();
    InvalidateInspection();
}

bool AudioBackendEditorModel::asio_selection_enabled() const noexcept {
    return catalog_state_ == AsioCatalogState::available;
}

AsioCatalogState AudioBackendEditorModel::catalog_state() const noexcept {
    return catalog_state_;
}

const std::optional<std::string>&
AudioBackendEditorModel::catalog_error() const noexcept {
    return catalog_error_;
}

const std::vector<std::string>&
AudioBackendEditorModel::driver_suggestions() const noexcept {
    return suggestions_;
}

void AudioBackendEditorModel::SetBackend(
    gc::config::AudioBackend backend) noexcept {
    if (config_->experimental().audio_backend() == backend) {
        return;
    }
    config_->experimental().audio_backend = backend;
    InvalidateInspection();
}

void AudioBackendEditorModel::SetDriverName(std::string name) {
    if (config_->experimental().asio_driver_name() == name) {
        return;
    }
    config_->experimental().asio_driver_name = std::move(name);
    InvalidateInspection();
}

void AudioBackendEditorModel::SetBufferFrames(
    std::uint32_t frames) noexcept {
    if (config_->experimental().asio_buffer_frames() == frames) {
        return;
    }
    config_->experimental().asio_buffer_frames = frames;
    InvalidateInspection();
}

void AudioBackendEditorModel::SetOutputBaseChannel(
    std::uint32_t channel) noexcept {
    if (config_->experimental().asio_output_base_channel() == channel) {
        return;
    }
    config_->experimental().asio_output_base_channel = channel;
    InvalidateInspection();
}

void AudioBackendEditorModel::NotifyConfigReloaded() noexcept {
    InvalidateInspection();
}

std::expected<gc::audio::AsioProbeRequest, std::string>
AudioBackendEditorModel::BeginInspection() {
    InvalidateInspection();
    if (config_->experimental().audio_backend() !=
        gc::config::AudioBackend::asio) {
        inspection_state_ = AsioInspectionState::failed;
        inspection_error_ = "Select ASIO before inspecting a driver";
        return std::unexpected(inspection_error_);
    }
    if (!asio_selection_enabled()) {
        inspection_state_ = AsioInspectionState::failed;
        inspection_error_ = catalog_state_ == AsioCatalogState::failed
            ? "ASIO driver catalog is unavailable"
            : "No 32-bit ASIO driver registration is installed";
        return std::unexpected(inspection_error_);
    }
    const auto& experimental = config_->experimental();
    const auto validation = gc::config::ValidateAudioBackendSettings(
        gc::config::AudioBackend::asio,
        experimental.asio_driver_name(),
        experimental.asio_buffer_frames() == 0
            ? 1U
            : static_cast<std::uint32_t>(
                  experimental.asio_buffer_frames()),
        static_cast<std::uint32_t>(
            experimental.asio_output_base_channel()));
    if (!validation.has_value()) {
        inspection_state_ = AsioInspectionState::failed;
        inspection_error_ = validation.error();
        return std::unexpected(inspection_error_);
    }

    inspection_state_ = AsioInspectionState::probing;
    return gc::audio::AsioProbeRequest{
        gc::audio::AsioProbeMode::inspect,
        experimental.asio_driver_name(),
        static_cast<std::uint32_t>(experimental.asio_buffer_frames()),
        static_cast<std::uint32_t>(
            experimental.asio_output_base_channel()),
    };
}

std::expected<gc::audio::AsioControlPanelRequest, std::string>
AudioBackendEditorModel::BeginControlPanel() {
    InvalidateInspection();
    if (config_->experimental().audio_backend() !=
        gc::config::AudioBackend::asio) {
        return std::unexpected(
            "Select ASIO before opening its control panel");
    }
    const auto& name = config_->experimental().asio_driver_name();
    if (name.empty()) {
        return std::unexpected(
            "Enter an exact ASIO driver name before opening its control panel");
    }
    return gc::audio::AsioControlPanelRequest{
        .driver_name = name,
    };
}

void AudioBackendEditorModel::CompleteInspection(
    gc::audio::AsioProbeResult result) {
    if (inspection_state_ != AsioInspectionState::probing) {
        return;
    }
    if (!result.has_value()) {
        inspection_state_ = AsioInspectionState::failed;
        inspection_error_ = DescribeAsioFailure(result.error());
        return;
    }

    auto report = std::move(*result);
    channel_pairs_ = BuildChannelPairs(report);
    report_ = report;
    auto& experimental = config_->experimental();
    if (report.sample_rate != 48'000.0 ||
        report.effective_buffer_frames == 0) {
        inspection_state_ = AsioInspectionState::failed;
        inspection_error_ =
            "Inspection did not establish exact 48 kHz and a nonzero buffer";
        return;
    }
    if (experimental.asio_buffer_frames() != 0 &&
        report.effective_buffer_frames !=
            experimental.asio_buffer_frames()) {
        inspection_state_ = AsioInspectionState::failed;
        inspection_error_ =
            "Inspection returned a different effective buffer frame count";
        return;
    }
    const auto selected_base = static_cast<std::uint32_t>(
        experimental.asio_output_base_channel());
    const auto selected = std::ranges::find(
        channel_pairs_,
        selected_base,
        &AsioChannelPairChoice::base_channel);
    if (selected == channel_pairs_.end() ||
        report.selected_base_channel != selected_base) {
        inspection_state_ = AsioInspectionState::failed;
        inspection_error_ =
            "Configured ASIO base channel is not an adjacent output pair";
        return;
    }
    if (experimental.asio_buffer_frames() == 0) {
        experimental.asio_buffer_frames = report.effective_buffer_frames;
    }
    inspection_state_ = AsioInspectionState::valid;
    inspection_error_.clear();
}

AsioInspectionState
AudioBackendEditorModel::inspection_state() const noexcept {
    return inspection_state_;
}

const std::string&
AudioBackendEditorModel::inspection_error() const noexcept {
    return inspection_error_;
}

const std::optional<gc::audio::AsioCapabilityReport>&
AudioBackendEditorModel::capability_report() const noexcept {
    return report_;
}

const std::vector<AsioChannelPairChoice>&
AudioBackendEditorModel::channel_pairs() const noexcept {
    return channel_pairs_;
}

void AudioBackendEditorModel::InvalidateInspection() noexcept {
    inspection_state_ = AsioInspectionState::idle;
    inspection_error_.clear();
    report_.reset();
    channel_pairs_.clear();
}

void AudioBackendEditorModel::RebuildSuggestions() {
    suggestions_.clear();
    suggestions_.reserve(installed_.size() + kCommonAsioDriverNames.size());
    const auto append = [&](std::string_view name) {
        if (name.empty()) {
            return;
        }
        const auto duplicate = std::ranges::find_if(
            suggestions_,
            [&](const std::string& existing) {
                return EqualInsensitive(existing, name);
            });
        if (duplicate == suggestions_.end()) {
            suggestions_.emplace_back(name);
        }
    };
    for (const auto& registration : installed_) {
        append(registration.registry_name);
    }
    for (const auto common : kCommonAsioDriverNames) {
        append(common);
    }
}

std::string DescribeAsioFailure(
    const gc::audio::AsioFailure& failure) {
    std::ostringstream text;
    text << "ASIO failure stage=" << FailureStageName(failure.stage)
         << " domain=" << ResultDomainName(failure.domain)
         << " result=" << failure.result;
    if (!failure.driver_message.empty()) {
        text << " driver_message=" << failure.driver_message;
    }
    if (!failure.detail.empty()) {
        text << " detail=" << failure.detail;
    }
    return text.str();
}

std::expected<void, std::string> ValidateAndWriteConfig(
    const std::filesystem::path& path,
    const InputConfig& config,
    gc::audio::IAsioProbeClient& asio_probe,
    const gc::config::AtomicConfigWriteActions& write_actions) noexcept {
    try {
        const auto static_validation =
            gc::config::ValidateInputConfig(config);
        if (!static_validation.has_value()) {
            return std::unexpected(static_validation.error());
        }
        const auto backend = config.experimental().audio_backend();
        if (backend == gc::config::AudioBackend::asio) {
            const gc::audio::AsioProbeRequest request{
                gc::audio::AsioProbeMode::validate,
                config.experimental().asio_driver_name(),
                static_cast<std::uint32_t>(
                    config.experimental().asio_buffer_frames()),
                static_cast<std::uint32_t>(
                    config.experimental().asio_output_base_channel()),
            };
            auto result = asio_probe.Run(
                request,
                gc::audio::kDefaultAsioProbeTimeout);
            if (!result.has_value()) {
                return std::unexpected(DescribeAsioFailure(result.error()));
            }
            const auto validated =
                ValidateCapabilityForConfig(config, *result);
            if (!validated.has_value()) {
                return std::unexpected(validated.error());
            }
        }
        return gc::config::WriteInputConfigAtomically(
            path,
            config,
            write_actions);
    } catch (const std::exception& error) {
        return std::unexpected(
            "Config save validation failed: " +
            std::string{error.what()});
    } catch (...) {
        return std::unexpected(
            "Config save validation failed unexpectedly");
    }
}

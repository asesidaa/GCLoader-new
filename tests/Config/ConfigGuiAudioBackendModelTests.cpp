// SPDX-License-Identifier: CC0-1.0

#include "AudioBackendEditorModel.h"

#include "Audio/Asio/AsioProbeClient.h"
#include "Audio/Asio/AsioTypes.h"
#include "Config/ConfigDocument.h"
#include "Config/config.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using gc::audio::AsioCapabilityReport;
using gc::audio::AsioDriverRegistration;
using gc::audio::AsioFailure;
using gc::audio::AsioFailureStage;
using gc::audio::AsioProbeMode;
using gc::audio::AsioProbeRequest;
using gc::audio::AsioResultDomain;
using gc::audio::IAsioProbeClient;
using gc::config::AudioBackend;

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

CLSID Guid(std::uint32_t data1) {
    return {data1, 0, 0, {0, 1, 2, 3, 4, 5, 6, 7}};
}

AsioCapabilityReport Report(
    std::uint32_t effective_frames = 192,
    std::uint32_t selected_base = 0) {
    AsioCapabilityReport report;
    report.registration = {"XONAR SOUND CARD", Guid(1)};
    report.reported_driver_name = "Xonar ASIO";
    report.driver_version = 5;
    report.original_sample_rate = 48'000.0;
    report.sample_rate = 48'000.0;
    report.buffer_limits = {64, 2400, 192, 1};
    report.output_channels = {
        {0, "Front Left", ASIOSTInt24LSB},
        {1, "Front Right", ASIOSTInt24LSB},
        {2, "Center", ASIOSTFloat32LSB},
        {3, "Subwoofer", ASIOSTFloat32LSB},
    };
    report.selected_base_channel = selected_base;
    report.effective_buffer_frames = effective_frames;
    report.output_latency_frames = 384;
    report.output_ready_supported = true;
    return report;
}

InputConfig ValidConfig(AudioBackend backend) {
    InputConfig config;
    config.experimental().audio_backend = backend;
    config.experimental().asio_driver_name = "XONAR SOUND CARD";
    config.experimental().asio_buffer_frames = 192;
    config.experimental().asio_output_base_channel = 0;
    return config;
}

class FakeProbe final : public IAsioProbeClient {
public:
    std::expected<AsioCapabilityReport, AsioFailure> Run(
        const AsioProbeRequest& request,
        std::chrono::milliseconds timeout) noexcept override {
        ++calls;
        last_request = request;
        last_timeout = timeout;
        return result;
    }

    int calls{};
    AsioProbeRequest last_request;
    std::chrono::milliseconds last_timeout{};
    std::expected<AsioCapabilityReport, AsioFailure> result{Report()};
};

struct WriterFake {
    bool fail_write{};
    bool fail_replace{};
    int writes{};
    int replaces{};
    int removes{};
    std::string destination_bytes{"original config bytes"};
    std::string temporary_bytes;
};

std::expected<void, std::string> FakeWrite(
    void* context,
    const std::filesystem::path&,
    std::string_view bytes) noexcept {
    auto& fake = *static_cast<WriterFake*>(context);
    ++fake.writes;
    if (fake.fail_write) {
        return std::unexpected("injected write failure");
    }
    fake.temporary_bytes.assign(bytes);
    return {};
}

std::expected<void, std::string> FakeReplace(
    void* context,
    const std::filesystem::path&,
    const std::filesystem::path&) noexcept {
    auto& fake = *static_cast<WriterFake*>(context);
    ++fake.replaces;
    if (fake.fail_replace) {
        return std::unexpected("injected replace failure");
    }
    fake.destination_bytes = fake.temporary_bytes;
    return {};
}

void FakeRemove(
    void* context,
    const std::filesystem::path&) noexcept {
    ++static_cast<WriterFake*>(context)->removes;
}

gc::config::AtomicConfigWriteActions WriteActions(
    WriterFake& fake) noexcept {
    return {
        &fake,
        &FakeWrite,
        &FakeReplace,
        &FakeRemove,
    };
}

int TestSuggestionsAndCatalogStates() {
    auto config = ValidConfig(AudioBackend::asio);
    config.experimental().asio_driver_name = "My Arcade Vendor ASIO";
    AudioBackendEditorModel model{config};
    model.ApplyCatalog(std::vector<AsioDriverRegistration>{
        {"Vendor Native ASIO", Guid(1)},
        {"flexasio", Guid(2)},
        {"XONAR SOUND CARD", Guid(3)},
        {"VENDOR NATIVE ASIO", Guid(4)},
    });

    const std::vector<std::string> expected{
        "Vendor Native ASIO",
        "flexasio",
        "XONAR SOUND CARD",
        "ASIO4ALL v2",
        "KoordASIO",
        "FL Studio ASIO",
        "Generic Low Latency ASIO Driver",
    };
    int failures{};
    failures += Expect(
        model.driver_suggestions() == expected,
        "installed names lead and common names append without case duplicates");
    failures += Expect(
        model.asio_selection_enabled() &&
            model.catalog_state() == AsioCatalogState::available &&
            config.experimental().asio_driver_name() ==
                "My Arcade Vendor ASIO",
        "arbitrary current name remains editable and selected");

    model.ApplyCatalog(std::vector<AsioDriverRegistration>{});
    failures += Expect(
        !model.asio_selection_enabled() &&
            model.catalog_state() == AsioCatalogState::empty &&
            !model.catalog_error().has_value() &&
            model.driver_suggestions().size() ==
                kCommonAsioDriverNames.size() &&
            config.experimental().asio_driver_name() ==
                "My Arcade Vendor ASIO",
        "empty successful catalog is distinct and preserves text suggestions");

    model.ApplyCatalog(std::unexpected(AsioFailure{
        .stage = AsioFailureStage::registry,
        .domain = AsioResultDomain::win32,
        .result = ERROR_ACCESS_DENIED,
        .detail = "32-bit registry access denied",
    }));
    failures += Expect(
        !model.asio_selection_enabled() &&
            model.catalog_state() == AsioCatalogState::failed &&
            model.catalog_error().has_value() &&
            model.catalog_error()->find("registry") != std::string::npos &&
            model.catalog_error()->find("denied") != std::string::npos,
        "registry enumeration failure remains a visible distinct state");
    return failures;
}

int TestInspectionStateAndEdits() {
    auto config = ValidConfig(AudioBackend::asio);
    config.experimental().asio_buffer_frames = 0;
    AudioBackendEditorModel model{config};
    model.ApplyCatalog(std::vector<AsioDriverRegistration>{
        {"XONAR SOUND CARD", Guid(1)},
    });

    auto request = model.BeginInspection();
    int failures{};
    failures += Expect(
        request.has_value() &&
            request->mode == AsioProbeMode::inspect &&
            request->driver_name == "XONAR SOUND CARD" &&
            request->buffer_frames == 0 &&
            request->output_base_channel == 0 &&
            model.inspection_state() == AsioInspectionState::probing,
        "inspection starts with exact editable fields including preferred zero");
    model.CompleteInspection(Report());
    failures += Expect(
        model.inspection_state() == AsioInspectionState::valid &&
            model.capability_report().has_value() &&
            config.experimental().asio_buffer_frames() == 192,
        "successful preferred inspection writes exact effective frame count");
    failures += Expect(
        model.channel_pairs().size() == 3 &&
            model.channel_pairs()[0].base_channel == 0 &&
            model.channel_pairs()[0].label.find("Front Left") !=
                std::string::npos &&
            model.channel_pairs()[0].label.find("Int24LSB") !=
                std::string::npos &&
            model.channel_pairs()[2].base_channel == 2,
        "all adjacent output pairs expose indexes names and sample formats");

    model.SetDriverName("任意 Unicode ASIO 名称");
    const auto panel_request = model.BeginControlPanel();
    failures += Expect(
        panel_request &&
            panel_request->driver_name == "任意 Unicode ASIO 名称" &&
            model.inspection_state() == AsioInspectionState::idle &&
            !model.capability_report(),
        "panel request preserves exact text and invalidates inspection");

    model.SetDriverName("Arbitrary Typed Name");
    failures += Expect(
        model.inspection_state() == AsioInspectionState::idle &&
            !model.capability_report().has_value() &&
            config.experimental().asio_driver_name() ==
                "Arbitrary Typed Name",
        "editing driver invalidates prior result without normalizing text");
    model.SetBufferFrames(256);
    request = model.BeginInspection();
    model.CompleteInspection(Report(256));
    failures += Expect(
        config.experimental().asio_buffer_frames() == 256 &&
            model.inspection_state() == AsioInspectionState::valid,
        "nonzero frame input is never replaced by preferred value");

    model.SetOutputBaseChannel(9);
    request = model.BeginInspection();
    model.CompleteInspection(Report(256, 9));
    failures += Expect(
        model.inspection_state() == AsioInspectionState::failed &&
            model.inspection_error().find("adjacent output pair") !=
                std::string::npos,
        "base channel absent from adjacent pairs is rejected");

    model.SetBackend(AudioBackend::wasapi_exclusive);
    const auto wrong_backend_panel = model.BeginControlPanel();
    failures += Expect(
        !wrong_backend_panel &&
            model.inspection_state() == AsioInspectionState::idle &&
            config.experimental().audio_backend() ==
                AudioBackend::wasapi_exclusive,
        "control panel requires the ASIO backend");
    model.SetBackend(AudioBackend::asio);
    model.SetDriverName({});
    failures += Expect(
        !model.BeginControlPanel(),
        "control panel rejects an empty exact driver name");
    model.SetDriverName("XONAR SOUND CARD");
    model.SetOutputBaseChannel(0);
    request = model.BeginInspection();
    model.CompleteInspection(std::unexpected(AsioFailure{
        .stage = AsioFailureStage::probe_timeout,
        .domain = AsioResultDomain::win32,
        .result = WAIT_TIMEOUT,
        .detail = "driver timed out",
    }));
    failures += Expect(
        model.inspection_state() == AsioInspectionState::failed &&
            model.inspection_error().find("probe_timeout") !=
                std::string::npos &&
            model.inspection_error().find("driver timed out") !=
                std::string::npos,
        "inspection exposes precise structured helper failure");
    return failures;
}

int TestDirectSoundAndWasapiSaveWithoutProbe() {
    int failures{};
    for (const auto backend : {
             AudioBackend::directsound,
             AudioBackend::wasapi_exclusive}) {
        auto config = ValidConfig(backend);
        FakeProbe probe;
        WriterFake writer;
        const auto result = ValidateAndWriteConfig(
            L"C:\\Arcade\\config.toml",
            config,
            probe,
            WriteActions(writer));
        failures += Expect(
            result.has_value() && probe.calls == 0 &&
                writer.writes == 1 && writer.replaces == 1 &&
                writer.destination_bytes != "original config bytes",
            "non-ASIO backend validates statically and writes without helper");
    }
    return failures;
}

int TestAsioSaveValidatesExactFieldsBeforeWriting() {
    auto config = ValidConfig(AudioBackend::asio);
    config.experimental().asio_driver_name = "Exact USER value";
    config.experimental().asio_buffer_frames = 384;
    config.experimental().asio_output_base_channel = 2;
    FakeProbe probe;
    probe.result = Report(384, 2);
    probe.result->registration.registry_name = "Exact USER value";
    WriterFake writer;
    const auto result = ValidateAndWriteConfig(
        L"C:\\Arcade\\config.toml",
        config,
        probe,
        WriteActions(writer));

    int failures{};
    failures += Expect(
        result.has_value() && probe.calls == 1 &&
            probe.last_request.mode == AsioProbeMode::validate &&
            probe.last_request.driver_name == "Exact USER value" &&
            probe.last_request.buffer_frames == 384 &&
            probe.last_request.output_base_channel == 2 &&
            probe.last_timeout == gc::audio::kDefaultAsioProbeTimeout,
        "ASIO final validation receives exact saved values and five-second bound");
    failures += Expect(
        writer.writes == 1 && writer.replaces == 1,
        "atomic writer runs only after successful final validation");
    return failures;
}

int TestProbeFailuresPerformZeroWrites() {
    const std::vector<AsioFailure> failures_to_inject{
        {AsioFailureStage::registry, AsioResultDomain::win32,
         ERROR_FILE_NOT_FOUND, {}, "missing registration"},
        {AsioFailureStage::sample_rate, AsioResultDomain::asio,
         ASE_NoClock, {}, "48 kHz unsupported"},
        {AsioFailureStage::buffer_metadata, AsioResultDomain::asio,
         ASE_InvalidMode, {}, "buffer unsupported"},
        {AsioFailureStage::channel_info, AsioResultDomain::asio,
         ASE_InvalidParameter, {}, "channel unsupported"},
        {AsioFailureStage::conversion, AsioResultDomain::none,
         0, {}, "sample format unsupported"},
        {AsioFailureStage::init, AsioResultDomain::asio,
         ASE_HWMalfunction, "driver error", "driver rejected init"},
        {AsioFailureStage::probe_timeout, AsioResultDomain::win32,
         WAIT_TIMEOUT, {}, "helper timeout"},
        {AsioFailureStage::probe_crash, AsioResultDomain::win32,
         0xC0000005, {}, "helper crash"},
        {AsioFailureStage::protocol, AsioResultDomain::none,
         0, {}, "malformed helper response"},
    };

    int failures{};
    for (const auto& injected : failures_to_inject) {
        auto config = ValidConfig(AudioBackend::asio);
        FakeProbe probe;
        probe.result = std::unexpected(injected);
        WriterFake writer;
        const auto original = writer.destination_bytes;
        const auto result = ValidateAndWriteConfig(
            L"C:\\Arcade\\config.toml",
            config,
            probe,
            WriteActions(writer));
        failures += Expect(
            !result.has_value() && probe.calls == 1 &&
                writer.writes == 0 && writer.replaces == 0 &&
                writer.removes == 0 &&
                writer.destination_bytes == original,
            "every helper or driver failure prevents all config writes");
    }

    auto mismatched = ValidConfig(AudioBackend::asio);
    FakeProbe mismatch_probe;
    mismatch_probe.result = Report(256, 0);
    WriterFake mismatch_writer;
    const auto mismatch = ValidateAndWriteConfig(
        L"C:\\Arcade\\config.toml",
        mismatched,
        mismatch_probe,
        WriteActions(mismatch_writer));
    failures += Expect(
        !mismatch.has_value() && mismatch_writer.writes == 0 &&
            mismatch_writer.replaces == 0,
        "capability response inconsistent with exact request cannot write");
    return failures;
}

int TestAtomicWriteFailuresPreserveDestination() {
    int failures{};
    for (const bool fail_replace : {false, true}) {
        auto config = ValidConfig(AudioBackend::asio);
        FakeProbe probe;
        WriterFake writer;
        writer.fail_write = !fail_replace;
        writer.fail_replace = fail_replace;
        const auto original = writer.destination_bytes;
        const auto result = ValidateAndWriteConfig(
            L"C:\\Arcade\\config.toml",
            config,
            probe,
            WriteActions(writer));
        failures += Expect(
            !result.has_value() && probe.calls == 1 &&
                writer.destination_bytes == original &&
                writer.writes == 1 &&
                writer.replaces == (fail_replace ? 1 : 0) &&
                writer.removes == 1,
            "atomic write or replace failure leaves old destination identical");
    }
    return failures;
}

int TestStaticValidationPrecedesProbe() {
    auto config = ValidConfig(AudioBackend::asio);
    config.experimental().asio_driver_name = "";
    FakeProbe probe;
    WriterFake writer;
    const auto result = ValidateAndWriteConfig(
        L"C:\\Arcade\\config.toml",
        config,
        probe,
        WriteActions(writer));
    return Expect(
        !result.has_value() && probe.calls == 0 && writer.writes == 0,
        "invalid static ASIO settings fail before helper and writer");
}

} // namespace

int main() {
    int failures{};
    failures += TestSuggestionsAndCatalogStates();
    failures += TestInspectionStateAndEdits();
    failures += TestDirectSoundAndWasapiSaveWithoutProbe();
    failures += TestAsioSaveValidatesExactFieldsBeforeWriting();
    failures += TestProbeFailuresPerformZeroWrites();
    failures += TestAtomicWriteFailuresPreserveDestination();
    failures += TestStaticValidationPrecedesProbe();
    return failures == 0 ? 0 : 1;
}

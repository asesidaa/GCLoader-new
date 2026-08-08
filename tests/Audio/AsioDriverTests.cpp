// SPDX-License-Identifier: CC0-1.0

#include "Audio/Asio/AsioDriver.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <expected>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int Expect(bool condition, std::string_view name) {
    if (condition) {
        return 0;
    }
    std::cerr << "Expectation failed: " << name << '\n';
    return 1;
}

CLSID ParseClsid(const wchar_t* text) {
    CLSID result{};
    if (FAILED(CLSIDFromString(text, &result))) {
        std::cerr << "Invalid test CLSID\n";
        std::exit(2);
    }
    return result;
}

struct DriverCalls {
    int query_interface{};
    int add_ref{};
    int release{};
    int init{};
    int get_driver_name{};
    int get_driver_version{};
    int get_error_message{};
    int start{};
    int stop{};
    int get_channels{};
    int get_latencies{};
    int get_buffer_size{};
    int can_sample_rate{};
    int get_sample_rate{};
    int set_sample_rate{};
    int get_clock_sources{};
    int set_clock_source{};
    int get_sample_position{};
    int get_channel_info{};
    int create_buffers{};
    int dispose_buffers{};
    int control_panel{};
    int future{};
    int output_ready{};
};

class FakeAsio final : public IASIO {
public:
    DriverCalls calls;
    void* initialized_with{};
    ASIOSampleRate set_rate{};

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID,
        void**) override {
        ++calls.query_interface;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        ++calls.add_ref;
        return 2;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ++calls.release;
        return 1;
    }

    ASIOBool init(void* system_reference) override {
        ++calls.init;
        initialized_with = system_reference;
        return ASIOTrue;
    }

    void getDriverName(char* name) override {
        ++calls.get_driver_name;
        name[0] = 'D';
    }

    long getDriverVersion() override {
        ++calls.get_driver_version;
        return 234;
    }

    void getErrorMessage(char* message) override {
        ++calls.get_error_message;
        message[0] = 'E';
    }

    ASIOError start() override {
        ++calls.start;
        return ASE_OK;
    }

    ASIOError stop() override {
        ++calls.stop;
        return ASE_OK;
    }

    ASIOError getChannels(long* inputs, long* outputs) override {
        ++calls.get_channels;
        *inputs = 0;
        *outputs = 8;
        return ASE_OK;
    }

    ASIOError getLatencies(long* input, long* output) override {
        ++calls.get_latencies;
        *input = 0;
        *output = 384;
        return ASE_OK;
    }

    ASIOError getBufferSize(
        long* minimum,
        long* maximum,
        long* preferred,
        long* granularity) override {
        ++calls.get_buffer_size;
        *minimum = 192;
        *maximum = 2400;
        *preferred = 192;
        *granularity = 1;
        return ASE_OK;
    }

    ASIOError canSampleRate(ASIOSampleRate rate) override {
        ++calls.can_sample_rate;
        return rate == 48'000.0 ? ASE_OK : ASE_NoClock;
    }

    ASIOError getSampleRate(ASIOSampleRate* rate) override {
        ++calls.get_sample_rate;
        *rate = 48'000.0;
        return ASE_OK;
    }

    ASIOError setSampleRate(ASIOSampleRate rate) override {
        ++calls.set_sample_rate;
        set_rate = rate;
        return ASE_OK;
    }

    ASIOError getClockSources(
        ASIOClockSource*,
        long*) override {
        ++calls.get_clock_sources;
        return ASE_NotPresent;
    }

    ASIOError setClockSource(long) override {
        ++calls.set_clock_source;
        return ASE_NotPresent;
    }

    ASIOError getSamplePosition(
        ASIOSamples* sample_position,
        ASIOTimeStamp* timestamp) override {
        ++calls.get_sample_position;
        std::memset(sample_position, 0, sizeof(*sample_position));
        std::memset(timestamp, 0, sizeof(*timestamp));
        return ASE_OK;
    }

    ASIOError getChannelInfo(ASIOChannelInfo* info) override {
        ++calls.get_channel_info;
        info->type = ASIOSTInt24LSB;
        info->name[0] = 'C';
        return ASE_OK;
    }

    ASIOError createBuffers(
        ASIOBufferInfo*,
        long,
        long,
        ASIOCallbacks*) override {
        ++calls.create_buffers;
        return ASE_OK;
    }

    ASIOError disposeBuffers() override {
        ++calls.dispose_buffers;
        return ASE_OK;
    }

    ASIOError controlPanel() override {
        ++calls.control_panel;
        return ASE_NotPresent;
    }

    ASIOError future(long, void*) override {
        ++calls.future;
        return ASE_SUCCESS;
    }

    ASIOError outputReady() override {
        ++calls.output_ready;
        return ASE_OK;
    }
};

struct ComFake {
    int calls{};
    CLSID class_id{};
    IID interface_id{};
    LPUNKNOWN outer{};
    DWORD context{};
    HRESULT result{S_OK};
    IASIO* driver{};
};

HRESULT FakeCoCreateInstance(
    void* context,
    REFCLSID class_id,
    LPUNKNOWN outer,
    DWORD class_context,
    REFIID interface_id,
    void** output) noexcept {
    auto& fake = *static_cast<ComFake*>(context);
    ++fake.calls;
    fake.class_id = class_id;
    fake.interface_id = interface_id;
    fake.outer = outer;
    fake.context = class_context;
    if (FAILED(fake.result)) {
        *output = nullptr;
        return fake.result;
    }
    *output = fake.driver;
    return fake.result;
}

struct AnsiFixture {
    std::vector<char> bytes;
    std::string utf8;
};

std::optional<AnsiFixture> FindNonAsciiAnsiFixture() {
    constexpr wchar_t candidates[]{L'é', L'中', L'あ', L'Ж', L'ä'};
    for (const wchar_t candidate : candidates) {
        BOOL used_default = FALSE;
        const int count = WideCharToMultiByte(
            CP_ACP,
            WC_NO_BEST_FIT_CHARS,
            &candidate,
            1,
            nullptr,
            0,
            nullptr,
            &used_default);
        if (count <= 0 || used_default) {
            continue;
        }
        std::vector<char> bytes(static_cast<std::size_t>(count));
        used_default = FALSE;
        if (WideCharToMultiByte(
                CP_ACP,
                WC_NO_BEST_FIT_CHARS,
                &candidate,
                1,
                bytes.data(),
                count,
                nullptr,
                &used_default) != count ||
            used_default ||
            std::none_of(bytes.begin(), bytes.end(), [](char value) {
                return static_cast<unsigned char>(value) >= 0x80U;
            })) {
            continue;
        }

        const int utf8_count = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            &candidate,
            1,
            nullptr,
            0,
            nullptr,
            nullptr);
        if (utf8_count <= 0) {
            continue;
        }
        std::string utf8(static_cast<std::size_t>(utf8_count), '\0');
        if (WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                &candidate,
                1,
                utf8.data(),
                utf8_count,
                nullptr,
                nullptr) != utf8_count) {
            continue;
        }
        return AnsiFixture{
            .bytes = std::move(bytes),
            .utf8 = std::move(utf8),
        };
    }
    return std::nullopt;
}

} // namespace

int main() {
    int failures = 0;

    FakeAsio raw_driver;
    ComFake com{.driver = &raw_driver};
    gc::audio::ProductionAsioDriverFactory factory{
        {
            .context = &com,
            .create_instance = &FakeCoCreateInstance,
        }};
    const CLSID registration = ParseClsid(
        L"{12345678-1234-5678-9ABC-DEF012345678}");
    auto created = factory.Create(registration);
    failures += Expect(
        created && com.calls == 1 &&
            IsEqualCLSID(com.class_id, registration) &&
            IsEqualIID(com.interface_id, registration) &&
            com.outer == nullptr &&
            com.context == CLSCTX_INPROC_SERVER,
        "factory uses registration CLSID as class and interface ID");
    if (!created) {
        return 1;
    }

    auto& driver = **created;
    const HWND system_reference = reinterpret_cast<HWND>(0x1234);
    failures += Expect(
        driver.Init(system_reference) == ASIOTrue &&
            raw_driver.initialized_with == system_reference,
        "init forwards the exact system reference");

    char driver_name[32];
    std::fill(
        std::begin(driver_name),
        std::end(driver_name),
        static_cast<char>(0x7F));
    driver.GetDriverName(driver_name);
    failures += Expect(
        gc::audio::AsioDisplayTextToUtf8(driver_name) == "D",
        "driver name buffer is cleared before forwarding");
    failures += Expect(
        driver.GetDriverVersion() == 234,
        "driver version forwards");

    char error_message[124];
    std::fill(
        std::begin(error_message),
        std::end(error_message),
        static_cast<char>(0x7F));
    driver.GetErrorMessage(error_message);
    failures += Expect(
        gc::audio::AsioDisplayTextToUtf8(error_message) == "E",
        "error message buffer is cleared before forwarding");

    long inputs{};
    long outputs{};
    long input_latency{};
    long output_latency{};
    long minimum{};
    long maximum{};
    long preferred{};
    long granularity{};
    ASIOSampleRate rate{};
    ASIOSamples sample_position{};
    ASIOTimeStamp timestamp{};
    ASIOChannelInfo channel_info{};
    channel_info.channel = 3;
    channel_info.isInput = ASIOFalse;
    std::fill(
        std::begin(channel_info.name),
        std::end(channel_info.name),
        static_cast<char>(0x7F));
    ASIOBufferInfo buffer_info{};
    ASIOCallbacks callbacks{};

    failures += Expect(driver.Start() == ASE_OK, "start forwards");
    failures += Expect(driver.Stop() == ASE_OK, "stop forwards");
    failures += Expect(
        driver.GetChannels(&inputs, &outputs) == ASE_OK &&
            inputs == 0 && outputs == 8,
        "channel query forwards");
    failures += Expect(
        driver.GetLatencies(&input_latency, &output_latency) == ASE_OK &&
            input_latency == 0 && output_latency == 384,
        "latency query forwards");
    failures += Expect(
        driver.GetBufferSize(
            &minimum,
            &maximum,
            &preferred,
            &granularity) == ASE_OK &&
            minimum == 192 && maximum == 2400 &&
            preferred == 192 && granularity == 1,
        "buffer query forwards");
    failures += Expect(
        driver.CanSampleRate(48'000.0) == ASE_OK,
        "sample-rate capability forwards");
    failures += Expect(
        driver.GetSampleRate(&rate) == ASE_OK && rate == 48'000.0,
        "sample-rate query forwards");
    failures += Expect(
        driver.SetSampleRate(44'100.0) == ASE_OK &&
            raw_driver.set_rate == 44'100.0,
        "sample-rate setter forwards");
    failures += Expect(
        driver.GetSamplePosition(&sample_position, &timestamp) == ASE_OK,
        "sample-position query forwards");
    failures += Expect(
        driver.GetChannelInfo(&channel_info) == ASE_OK &&
            channel_info.channel == 3 &&
            channel_info.isInput == ASIOFalse &&
            channel_info.type == ASIOSTInt24LSB &&
            gc::audio::AsioDisplayTextToUtf8(channel_info.name) == "C",
        "channel info preserves identity and clears display text");
    failures += Expect(
        driver.CreateBuffers(&buffer_info, 1, 192, &callbacks) == ASE_OK,
        "buffer creation forwards");
    failures += Expect(
        driver.DisposeBuffers() == ASE_OK,
        "buffer disposal forwards");
    failures += Expect(
        driver.Future(kAsioCanReportOverload, nullptr) == ASE_SUCCESS,
        "future forwards");
    failures += Expect(
        driver.OutputReady() == ASE_OK,
        "output-ready forwards");

    const DriverCalls& calls = raw_driver.calls;
    failures += Expect(
        calls.init == 1 && calls.get_driver_name == 1 &&
            calls.get_driver_version == 1 &&
            calls.get_error_message == 1 && calls.start == 1 &&
            calls.stop == 1 && calls.get_channels == 1 &&
            calls.get_latencies == 1 && calls.get_buffer_size == 1 &&
            calls.can_sample_rate == 1 && calls.get_sample_rate == 1 &&
            calls.set_sample_rate == 1 &&
            calls.get_sample_position == 1 &&
            calls.get_channel_info == 1 && calls.create_buffers == 1 &&
            calls.dispose_buffers == 1 && calls.future == 1 &&
            calls.output_ready == 1 && calls.query_interface == 0 &&
            calls.add_ref == 0 && calls.release == 0 &&
            calls.get_clock_sources == 0 && calls.set_clock_source == 0 &&
            calls.control_panel == 0,
        "narrow wrapper forwards every exposed method exactly once");

    created->reset();
    failures += Expect(
        raw_driver.calls.release == 1,
        "wrapped IASIO interface releases exactly once");

    ComFake failed_com{.result = E_NOINTERFACE};
    gc::audio::ProductionAsioDriverFactory failed_factory{
        {
            .context = &failed_com,
            .create_instance = &FakeCoCreateInstance,
        }};
    const auto failed_create = failed_factory.Create(registration);
    failures += Expect(
        !failed_create &&
            failed_create.error().stage ==
                gc::audio::AsioFailureStage::com &&
            failed_create.error().domain ==
                gc::audio::AsioResultDomain::hresult &&
            failed_create.error().result ==
                static_cast<std::int64_t>(E_NOINTERFACE),
        "COM creation failure preserves signed HRESULT domain and value");

    ComFake null_com{.result = S_OK, .driver = nullptr};
    gc::audio::ProductionAsioDriverFactory null_factory{
        {
            .context = &null_com,
            .create_instance = &FakeCoCreateInstance,
        }};
    const auto null_create = null_factory.Create(registration);
    failures += Expect(
        !null_create &&
            null_create.error().stage ==
                gc::audio::AsioFailureStage::com &&
            null_create.error().result ==
                static_cast<std::int64_t>(E_POINTER),
        "COM success without an IASIO pointer is rejected");

    constexpr std::array<char, 6> terminated{
        'A', 'S', 'I', 'O', '\0', 'X'};
    failures += Expect(
        gc::audio::AsioDisplayTextToUtf8(terminated) == "ASIO",
        "terminated display text ignores trailing bytes");
    failures += Expect(
        gc::audio::AsioDisplayTextToUtf8({}).empty(),
        "empty display text converts safely");
    std::array<char, 32> fully_filled;
    fully_filled.fill('Q');
    failures += Expect(
        gc::audio::AsioDisplayTextToUtf8(fully_filled) ==
            std::string(fully_filled.size(), 'Q'),
        "fully filled display text is bounded by the SDK array size");
    const auto non_ascii = FindNonAsciiAnsiFixture();
    failures += Expect(
        non_ascii &&
            gc::audio::AsioDisplayTextToUtf8(non_ascii->bytes) ==
                non_ascii->utf8,
        "non-ASCII ANSI display text converts to checked UTF-8");

    return failures == 0 ? 0 : 1;
}

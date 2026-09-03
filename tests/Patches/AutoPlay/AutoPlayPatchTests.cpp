#include "Patches/AutoPlay/AutoPlayMarker.h"
#include "Patches/AutoPlay/AutoPlayPatchTransaction.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

// Marker oracle: docs/superpowers/specs/
// 2026-09-03-native-auto-play-safety-design.md, Visible Marker Contract.
// Native ABI evidence: game471-debug-text-outer-frame-trace.json,
// SHA-256 FD2FBB6475A0368B8C7DE1356F7F16E74DB7F2612481318FB674AB3998B9BBE7.

namespace
{
    int g_failures{};

    void Expect(const bool condition, const std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "FAIL: " << message << '\n';
            ++g_failures;
        }
    }

    struct TextCall
    {
        float x{};
        float y{};
        std::uint32_t argb{};
        std::string_view text;
    };

    struct TextRecorder
    {
        std::array<TextCall, 4> calls{};
        std::size_t count{};
        std::size_t fail_at{std::numeric_limits<std::size_t>::max()};
    };

    bool RecordText(
        void* context,
        const float x,
        const float y,
        const std::uint32_t argb,
        const char* text) noexcept
    {
        auto& recorder = *static_cast<TextRecorder*>(context);
        if (recorder.count == recorder.fail_at)
        {
            return false;
        }
        if (recorder.count >= recorder.calls.size() || text == nullptr)
        {
            return false;
        }
        recorder.calls[recorder.count++] = {x, y, argb, text};
        return true;
    }

    void MarkerProducerIsInactiveOrEmitsTheFixedContract()
    {
        using enum gc::auto_play::AutoPlayMarkerFrameResult;

        const auto inactive_result =
            gc::auto_play::ProduceAutoPlayMarkerFrame(false, {});
        Expect(
            inactive_result == inactive,
            "inactive marker returns without requiring actions");

        TextRecorder recorder{};
        const gc::auto_play::AutoPlayMarkerTextActions actions{
            .context = &recorder,
            .queue = RecordText,
        };
        const auto queued_result =
            gc::auto_play::ProduceAutoPlayMarkerFrame(true, actions);
        Expect(queued_result == queued, "active marker queues its complete frame");
        Expect(recorder.count == 4, "active marker queues exactly four calls");

        constexpr std::array<TextCall, 4> expected{
            TextCall{34.0F, 34.0F, 0xFF000000U, "AUTO PLAY"},
            TextCall{34.0F, 54.0F, 0xFF000000U, "SCORE SAVE DISABLED"},
            TextCall{32.0F, 32.0F, 0xFFFFFF00U, "AUTO PLAY"},
            TextCall{32.0F, 52.0F, 0xFFFFFF00U, "SCORE SAVE DISABLED"},
        };
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            Expect(
                recorder.calls[index].x == expected[index].x &&
                    recorder.calls[index].y == expected[index].y &&
                    recorder.calls[index].argb == expected[index].argb &&
                    recorder.calls[index].text == expected[index].text,
                "marker call matches the fixed visible contract");
        }

        const auto invalid_result =
            gc::auto_play::ProduceAutoPlayMarkerFrame(true, {});
        Expect(
            invalid_result == invalid_actions,
            "active marker rejects an incomplete text action");

        TextRecorder failing_recorder{
            .fail_at = 2,
        };
        const auto failing_result = gc::auto_play::ProduceAutoPlayMarkerFrame(
            true,
            {
                .context = &failing_recorder,
                .queue = RecordText,
            });
        Expect(
            failing_result == native_text_failure,
            "marker reports the first native text failure");
        Expect(
            failing_recorder.count == 2,
            "marker stops after the first native text failure");
    }

    // Direct-site oracle: game471-autoplay-patch-contract.json,
    // SHA-256 06E2C1F788F4DD8BBE072D53E3952E2275D376954EF1A399331C2215022A423F.
    // Marker seam/native text oracle: game471-debug-text-outer-frame-trace.json,
    // SHA-256 FD2FBB6475A0368B8C7DE1356F7F16E74DB7F2612481318FB674AB3998B9BBE7.
    // The fixture intentionally does not consume the production contract table.

    template <std::uint8_t... Values>
    consteval gc::auto_play::AutoPlayBytePattern TestPattern() noexcept
    {
        static_assert(
            sizeof...(Values) <=
            gc::auto_play::kMaximumAutoPlayPatternBytes);
        gc::auto_play::AutoPlayBytePattern pattern{};
        std::size_t index{};
        ((pattern.bytes[index++] = std::byte{Values}), ...);
        pattern.size = static_cast<std::uint8_t>(sizeof...(Values));
        return pattern;
    }

    struct SiteFixture
    {
        gc::auto_play::AutoPlayContractSite site{};
        std::uint32_t rva{};
        gc::auto_play::AutoPlayBytePattern clean{};
        gc::auto_play::AutoPlayBytePattern patched{};
    };

    constexpr std::uintptr_t kImageBase{0x00400000U};
    constexpr std::array<SiteFixture, 5> kSiteFixtures{
        SiteFixture{
            gc::auto_play::AutoPlayContractSite::do_not_save_card_data,
            0x00269951U,
            TestPattern<0x0F, 0x95, 0xC1>(),
            TestPattern<0xB1, 0x01, 0x90>()},
        SiteFixture{
            gc::auto_play::AutoPlayContractSite::complete_is_mute,
            0x0003CAFAU,
            TestPattern<0x8A, 0x80, 0xA6, 0x00, 0x00, 0x00>(),
            TestPattern<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>()},
        SiteFixture{
            gc::auto_play::AutoPlayContractSite::native_auto_play,
            0x0003CADAU,
            TestPattern<0x8A, 0x80, 0xA5, 0x00, 0x00, 0x00>(),
            TestPattern<0xB0, 0x01, 0x90, 0x90, 0x90, 0x90>()},
        SiteFixture{
            gc::auto_play::AutoPlayContractSite::marker_seam,
            0x00058BE9U,
            TestPattern<
                0x8D,
                0x44,
                0x24,
                0x08,
                0x50,
                0xE8,
                0x8D,
                0x03,
                0x00,
                0x00>(),
            {}},
        SiteFixture{
            gc::auto_play::AutoPlayContractSite::native_debug_text,
            0x00069650U,
            TestPattern<0x55, 0x8B, 0xEC, 0x6A, 0xFF>(),
            {}},
    };

    enum class EventKind : std::uint8_t
    {
        resolve,
        read,
        hook_install,
        direct_write,
        hook_reset,
    };

    struct Event
    {
        EventKind kind{};
        std::uintptr_t address{};

        friend bool operator==(const Event&, const Event&) = default;
    };

    struct SiteState
    {
        std::array<std::byte, gc::auto_play::kMaximumAutoPlayPatternBytes>
            bytes{};
        std::uint8_t size{};

        friend bool operator==(const SiteState&, const SiteState&) = default;
    };

    struct FakeBackend
    {
        std::uintptr_t image_base{kImageBase};
        bool resolve_failure{};
        gc::auto_play::AutoPlayContractSite read_failure_site{
            gc::auto_play::AutoPlayContractSite::none};
        gc::auto_play::AutoPlayContractSite unknown_site{
            gc::auto_play::AutoPlayContractSite::none};
        bool hook_install_failure{};
        bool hook_reset_failure{};
        std::size_t direct_write_failure_index{
            std::numeric_limits<std::size_t>::max()};
        std::uintptr_t rollback_write_failure_address{};
        gc::auto_play::AutoPlayRuntimeState* runtime{};
        std::array<SiteState, kSiteFixtures.size()> sites{};
        std::array<Event, 32> events{};
        std::size_t event_count{};
        bool mutation_observed_active{};
    };

    FakeBackend MakeFake(gc::auto_play::AutoPlayRuntimeState& runtime)
    {
        FakeBackend fake{
            .runtime = &runtime,
        };
        for (std::size_t index = 0; index < kSiteFixtures.size(); ++index)
        {
            const auto clean = kSiteFixtures[index].clean.view();
            std::ranges::copy(clean, fake.sites[index].bytes.begin());
            fake.sites[index].size = kSiteFixtures[index].clean.size;
        }
        return fake;
    }

    void RecordEvent(
        FakeBackend& fake,
        const EventKind kind,
        const std::uintptr_t address = 0) noexcept
    {
        if (fake.event_count < fake.events.size())
        {
            fake.events[fake.event_count++] = {kind, address};
        }
    }

    void ObserveMutationState(FakeBackend& fake) noexcept
    {
        if (fake.runtime != nullptr &&
            fake.runtime->marker_active.load(std::memory_order_acquire))
        {
            fake.mutation_observed_active = true;
        }
    }

    std::expected<std::uintptr_t, DWORD> ResolveImageBase(
        void* context) noexcept
    {
        auto& fake = *static_cast<FakeBackend*>(context);
        RecordEvent(fake, EventKind::resolve, fake.image_base);
        if (fake.resolve_failure)
        {
            return std::unexpected(ERROR_MOD_NOT_FOUND);
        }
        return fake.image_base;
    }

    std::size_t FindSiteIndex(
        const FakeBackend& fake,
        const std::uintptr_t address,
        const std::size_t size) noexcept
    {
        for (std::size_t index = 0; index < kSiteFixtures.size(); ++index)
        {
            if (address == fake.image_base + kSiteFixtures[index].rva &&
                size == kSiteFixtures[index].clean.size)
            {
                return index;
            }
        }
        return kSiteFixtures.size();
    }

    gc::game_compatibility::GameBinaryMemoryResult ReadImage(
        void* context,
        const std::uintptr_t address,
        const std::span<std::byte> output) noexcept
    {
        auto& fake = *static_cast<FakeBackend*>(context);
        RecordEvent(fake, EventKind::read, address);
        const auto index = FindSiteIndex(fake, address, output.size());
        if (index == kSiteFixtures.size())
        {
            return std::unexpected(
                gc::game_compatibility::GameBinaryMemoryError{
                    gc::game_compatibility::GameBinaryMemoryStage::Read,
                    ERROR_INVALID_ADDRESS});
        }
        if (fake.read_failure_site == kSiteFixtures[index].site)
        {
            return std::unexpected(
                gc::game_compatibility::GameBinaryMemoryError{
                    gc::game_compatibility::GameBinaryMemoryStage::Read,
                    ERROR_NOACCESS});
        }

        std::ranges::copy(
            std::span{fake.sites[index].bytes}.first(output.size()),
            output.begin());
        if (fake.unknown_site == kSiteFixtures[index].site)
        {
            output.front() = std::byte{0xCC};
        }
        return {};
    }

    bool PatternEquals(
        const std::span<const std::byte> bytes,
        const gc::auto_play::AutoPlayBytePattern& pattern) noexcept
    {
        return std::ranges::equal(bytes, pattern.view());
    }

    gc::game_compatibility::GameBinaryMemoryResult WriteImage(
        void* context,
        const std::uintptr_t address,
        const std::span<const std::byte> input) noexcept
    {
        auto& fake = *static_cast<FakeBackend*>(context);
        RecordEvent(fake, EventKind::direct_write, address);
        ObserveMutationState(fake);
        const auto index = FindSiteIndex(fake, address, input.size());
        if (index >= 3)
        {
            return std::unexpected(
                gc::game_compatibility::GameBinaryMemoryError{
                    gc::game_compatibility::GameBinaryMemoryStage::Copy,
                    ERROR_INVALID_ADDRESS});
        }

        const bool forward = PatternEquals(input, kSiteFixtures[index].patched);
        const bool rollback = PatternEquals(input, kSiteFixtures[index].clean);
        if (!forward && !rollback)
        {
            return std::unexpected(
                gc::game_compatibility::GameBinaryMemoryError{
                    gc::game_compatibility::GameBinaryMemoryStage::Copy,
                    ERROR_INVALID_DATA});
        }
        if (forward && fake.direct_write_failure_index == index)
        {
            return std::unexpected(
                gc::game_compatibility::GameBinaryMemoryError{
                    gc::game_compatibility::GameBinaryMemoryStage::Copy,
                    ERROR_WRITE_FAULT});
        }
        if (rollback &&
            fake.rollback_write_failure_address == address)
        {
            return std::unexpected(
                gc::game_compatibility::GameBinaryMemoryError{
                    gc::game_compatibility::GameBinaryMemoryStage::
                        RestoreProtection,
                    ERROR_ACCESS_DENIED});
        }

        std::ranges::copy(input, fake.sites[index].bytes.begin());
        return {};
    }

    std::expected<void, std::uint32_t> InstallMarkerHook(
        void* context,
        const std::uintptr_t address) noexcept
    {
        auto& fake = *static_cast<FakeBackend*>(context);
        RecordEvent(fake, EventKind::hook_install, address);
        ObserveMutationState(fake);
        if (fake.hook_install_failure)
        {
            return std::unexpected(7U);
        }
        return {};
    }

    bool ResetMarkerHook(void* context) noexcept
    {
        auto& fake = *static_cast<FakeBackend*>(context);
        RecordEvent(fake, EventKind::hook_reset);
        ObserveMutationState(fake);
        return !fake.hook_reset_failure;
    }

    gc::auto_play::AutoPlayPatchActions ActionsFor(FakeBackend& fake) noexcept
    {
        return {
            .context = &fake,
            .resolve_image_base = ResolveImageBase,
            .read = ReadImage,
            .write = WriteImage,
            .install_marker_hook = InstallMarkerHook,
            .reset_marker_hook = ResetMarkerHook,
        };
    }

    void SetDirectPatched(FakeBackend& fake, const std::size_t index)
    {
        const auto patched = kSiteFixtures[index].patched.view();
        std::ranges::copy(patched, fake.sites[index].bytes.begin());
        fake.sites[index].size = kSiteFixtures[index].patched.size;
    }

    bool HasMutationEvent(const FakeBackend& fake) noexcept
    {
        return std::ranges::any_of(
            std::span{fake.events}.first(fake.event_count),
            [](const Event& event)
            {
                return event.kind == EventKind::hook_install ||
                    event.kind == EventKind::direct_write ||
                    event.kind == EventKind::hook_reset;
            });
    }

    std::vector<Event> MutationEvents(const FakeBackend& fake)
    {
        std::vector<Event> result;
        for (const auto& event :
             std::span{fake.events}.first(fake.event_count))
        {
            if (event.kind == EventKind::hook_install ||
                event.kind == EventKind::direct_write ||
                event.kind == EventKind::hook_reset)
            {
                result.push_back(event);
            }
        }
        return result;
    }

    void ExpectInactiveAndUnpublished(
        const gc::auto_play::AutoPlayRuntimeState& runtime)
    {
        Expect(
            !runtime.marker_active.load(std::memory_order_acquire),
            "failed transaction does not publish marker activity");
        Expect(
            runtime.native_text_address == 0,
            "failed transaction clears the native text target");
        Expect(
            runtime.direct_patched == 0 && runtime.direct_existing == 0,
            "failed transaction clears its direct-site counters");
    }

    void DisabledAndCommittedInitializationPerformNoOperations()
    {
        gc::auto_play::AutoPlayRuntimeState disabled_runtime{};
        const auto disabled = gc::auto_play::InstallAutoPlayPatch(
            false,
            disabled_runtime,
            {});
        Expect(disabled.has_value(), "disabled auto play accepts empty actions");
        if (disabled)
        {
            Expect(
                disabled->state == gc::auto_play::AutoPlayPatchState::disabled,
                "disabled auto play reports its disabled state");
        }
        ExpectInactiveAndUnpublished(disabled_runtime);

        gc::auto_play::AutoPlayRuntimeState runtime{};
        auto fake = MakeFake(runtime);
        const auto first = gc::auto_play::InstallAutoPlayPatch(
            true,
            runtime,
            ActionsFor(fake));
        Expect(first.has_value(), "initial enabled transaction commits");
        if (!first)
        {
            return;
        }
        const auto stored_patched = runtime.direct_patched;
        const auto stored_existing = runtime.direct_existing;
        fake.event_count = 0;
        const auto repeated = gc::auto_play::InstallAutoPlayPatch(
            true,
            runtime,
            {});
        Expect(repeated.has_value(), "committed transaction is idempotent");
        if (repeated)
        {
            Expect(
                repeated->state ==
                    gc::auto_play::AutoPlayPatchState::already_enabled,
                "repeated transaction reports already enabled");
            Expect(
                repeated->direct_patched == stored_patched &&
                    repeated->direct_existing == stored_existing,
                "repeated transaction returns the committed counters");
        }
        Expect(fake.event_count == 0, "repeated transaction performs no action");

        constexpr std::array stage_names{
            std::pair{gc::auto_play::AutoPlayPatchStage::none, "none"},
            std::pair{
                gc::auto_play::AutoPlayPatchStage::invalid_actions,
                "invalid_actions"},
            std::pair{
                gc::auto_play::AutoPlayPatchStage::resolve_image_base,
                "resolve_image_base"},
            std::pair{
                gc::auto_play::AutoPlayPatchStage::address_range,
                "address_range"},
            std::pair{
                gc::auto_play::AutoPlayPatchStage::preflight_read,
                "preflight_read"},
            std::pair{
                gc::auto_play::AutoPlayPatchStage::byte_mismatch,
                "byte_mismatch"},
            std::pair{
                gc::auto_play::AutoPlayPatchStage::hook_install,
                "hook_install"},
            std::pair{
                gc::auto_play::AutoPlayPatchStage::direct_write,
                "direct_write"},
        };
        for (const auto& [value, name] : stage_names)
        {
            Expect(
                std::string_view{gc::auto_play::AutoPlayPatchStageName(value)} ==
                    name,
                "every patch stage has a stable name");
        }

        constexpr std::array site_names{
            std::pair{gc::auto_play::AutoPlayContractSite::none, "none"},
            std::pair{
                gc::auto_play::AutoPlayContractSite::do_not_save_card_data,
                "do_not_save_card_data"},
            std::pair{
                gc::auto_play::AutoPlayContractSite::complete_is_mute,
                "complete_is_mute"},
            std::pair{
                gc::auto_play::AutoPlayContractSite::native_auto_play,
                "native_auto_play"},
            std::pair{
                gc::auto_play::AutoPlayContractSite::marker_seam,
                "marker_seam"},
            std::pair{
                gc::auto_play::AutoPlayContractSite::native_debug_text,
                "native_debug_text"},
        };
        for (const auto& [value, name] : site_names)
        {
            Expect(
                std::string_view{gc::auto_play::AutoPlayContractSiteName(value)} ==
                    name,
                "every contract site has a stable name");
        }

        constexpr std::array state_names{
            std::pair{gc::auto_play::AutoPlayPatchState::disabled, "disabled"},
            std::pair{gc::auto_play::AutoPlayPatchState::enabled, "enabled"},
            std::pair{
                gc::auto_play::AutoPlayPatchState::already_enabled,
                "already_enabled"},
        };
        for (const auto& [value, name] : state_names)
        {
            Expect(
                std::string_view{gc::auto_play::AutoPlayPatchStateName(value)} ==
                    name,
                "every patch state has a stable name");
        }
    }

    void EveryDirectStateCombinationCommitsInSafetyOrder()
    {
        for (unsigned int mask = 0; mask < 8; ++mask)
        {
            gc::auto_play::AutoPlayRuntimeState runtime{};
            auto fake = MakeFake(runtime);
            for (std::size_t index = 0; index < 3; ++index)
            {
                if ((mask & (1U << index)) != 0)
                {
                    SetDirectPatched(fake, index);
                }
            }

            const auto result = gc::auto_play::InstallAutoPlayPatch(
                true,
                runtime,
                ActionsFor(fake));
            Expect(result.has_value(), "every known direct state commits");
            if (!result)
            {
                continue;
            }

            const auto existing = static_cast<std::size_t>(std::popcount(mask));
            Expect(
                result->state == gc::auto_play::AutoPlayPatchState::enabled,
                "known direct state reports enabled");
            Expect(
                result->direct_existing == existing &&
                    result->direct_patched == 3 - existing,
                "known direct state reports exact ownership counts");
            Expect(
                runtime.marker_active.load(std::memory_order_acquire),
                "successful transaction publishes marker activity");
            Expect(
                runtime.native_text_address ==
                    kImageBase + kSiteFixtures[4].rva,
                "successful transaction publishes the native text target");
            Expect(
                !fake.mutation_observed_active,
                "hook and writes run before marker publication");

            Expect(
                fake.event_count == 7 + (3 - existing),
                "successful transaction records only preflight and owned writes");
            if (fake.event_count < 7)
            {
                continue;
            }
            Expect(
                fake.events[0] == Event{EventKind::resolve, kImageBase},
                "transaction resolves the image once before preflight");
            for (std::size_t index = 0; index < kSiteFixtures.size(); ++index)
            {
                Expect(
                    fake.events[index + 1] == Event{
                        EventKind::read,
                        kImageBase + kSiteFixtures[index].rva},
                    "preflight reads every contract in fixed order");
            }
            Expect(
                fake.events[6] == Event{
                    EventKind::hook_install,
                    kImageBase + kSiteFixtures[3].rva},
                "marker hook installs after complete preflight");

            std::size_t event_index = 7;
            for (std::size_t index = 0; index < 3; ++index)
            {
                if ((mask & (1U << index)) == 0)
                {
                    Expect(
                        fake.events[event_index++] == Event{
                            EventKind::direct_write,
                            kImageBase + kSiteFixtures[index].rva},
                        "owned direct writes follow safety order");
                }
                Expect(
                    std::ranges::equal(
                        std::span{fake.sites[index].bytes}.first(
                            fake.sites[index].size),
                        kSiteFixtures[index].patched.view()),
                    "every direct site ends in its patched form");
            }
        }
    }

    void EveryPreflightFailureLeavesTheImageAndHookUntouched()
    {
        for (std::size_t missing = 0; missing < 6; ++missing)
        {
            gc::auto_play::AutoPlayRuntimeState runtime{};
            auto fake = MakeFake(runtime);
            auto actions = ActionsFor(fake);
            switch (missing)
            {
            case 0: actions.context = nullptr; break;
            case 1: actions.resolve_image_base = nullptr; break;
            case 2: actions.read = nullptr; break;
            case 3: actions.write = nullptr; break;
            case 4: actions.install_marker_hook = nullptr; break;
            case 5: actions.reset_marker_hook = nullptr; break;
            default: break;
            }
            const auto result = gc::auto_play::InstallAutoPlayPatch(
                true,
                runtime,
                actions);
            Expect(!result.has_value(), "incomplete actions are rejected");
            if (!result)
            {
                Expect(
                    result.error().stage ==
                        gc::auto_play::AutoPlayPatchStage::invalid_actions,
                    "incomplete actions report invalid-actions stage");
            }
            Expect(fake.event_count == 0, "invalid actions invoke no callback");
            ExpectInactiveAndUnpublished(runtime);
        }

        {
            gc::auto_play::AutoPlayRuntimeState runtime{};
            auto fake = MakeFake(runtime);
            fake.resolve_failure = true;
            const auto result = gc::auto_play::InstallAutoPlayPatch(
                true,
                runtime,
                ActionsFor(fake));
            Expect(!result.has_value(), "image resolution failure is reported");
            if (!result)
            {
                Expect(
                    result.error().stage ==
                        gc::auto_play::AutoPlayPatchStage::resolve_image_base &&
                        result.error().win32_error == ERROR_MOD_NOT_FOUND,
                    "image resolution preserves its Win32 error");
            }
            Expect(fake.event_count == 1, "resolution failure stops immediately");
            ExpectInactiveAndUnpublished(runtime);
        }

        {
            gc::auto_play::AutoPlayRuntimeState runtime{};
            auto fake = MakeFake(runtime);
            fake.image_base =
                (std::numeric_limits<std::uintptr_t>::max)() -
                kSiteFixtures[0].rva + 1;
            const auto result = gc::auto_play::InstallAutoPlayPatch(
                true,
                runtime,
                ActionsFor(fake));
            Expect(!result.has_value(), "overflowing image address is rejected");
            if (!result)
            {
                Expect(
                    result.error().stage ==
                        gc::auto_play::AutoPlayPatchStage::address_range,
                    "overflowing image address reports address-range stage");
            }
            Expect(
                fake.event_count == 1 && !HasMutationEvent(fake),
                "address overflow performs no read or mutation");
            ExpectInactiveAndUnpublished(runtime);
        }

        for (const auto& fixture : kSiteFixtures)
        {
            gc::auto_play::AutoPlayRuntimeState runtime{};
            auto fake = MakeFake(runtime);
            const auto initial = fake.sites;
            fake.read_failure_site = fixture.site;
            const auto result = gc::auto_play::InstallAutoPlayPatch(
                true,
                runtime,
                ActionsFor(fake));
            Expect(!result.has_value(), "every native read failure is reported");
            if (!result)
            {
                Expect(
                    result.error().stage ==
                        gc::auto_play::AutoPlayPatchStage::preflight_read &&
                        result.error().site == fixture.site &&
                        result.error().memory_stage ==
                            gc::game_compatibility::GameBinaryMemoryStage::Read &&
                        result.error().win32_error == ERROR_NOACCESS,
                    "preflight read preserves site and memory failure");
            }
            Expect(!HasMutationEvent(fake), "read failure mutates no native site");
            Expect(fake.sites == initial, "read failure leaves image unchanged");
            ExpectInactiveAndUnpublished(runtime);
        }

        for (const auto& fixture : kSiteFixtures)
        {
            gc::auto_play::AutoPlayRuntimeState runtime{};
            auto fake = MakeFake(runtime);
            const auto initial = fake.sites;
            fake.unknown_site = fixture.site;
            const auto result = gc::auto_play::InstallAutoPlayPatch(
                true,
                runtime,
                ActionsFor(fake));
            Expect(!result.has_value(), "every unknown native form is rejected");
            if (!result)
            {
                Expect(
                    result.error().stage ==
                        gc::auto_play::AutoPlayPatchStage::byte_mismatch &&
                        result.error().site == fixture.site &&
                        result.error().rva == fixture.rva,
                    "byte mismatch identifies its native contract");
                Expect(
                    PatternEquals(
                        result.error().expected_clean.view(),
                        fixture.clean),
                    "byte mismatch retains the independent clean pattern");
                Expect(
                    (fixture.patched.size == 0 &&
                     result.error().expected_patched.size == 0) ||
                        PatternEquals(
                            result.error().expected_patched.view(),
                            fixture.patched),
                    "byte mismatch retains the direct patched pattern only");
            }
            Expect(!HasMutationEvent(fake), "byte mismatch mutates no native site");
            Expect(fake.sites == initial, "byte mismatch leaves image unchanged");
            ExpectInactiveAndUnpublished(runtime);
        }
    }

    void HookAndDirectWriteFailuresRollbackOnlyOwnedSites()
    {
        {
            gc::auto_play::AutoPlayRuntimeState runtime{};
            auto fake = MakeFake(runtime);
            const auto initial = fake.sites;
            fake.hook_install_failure = true;
            const auto result = gc::auto_play::InstallAutoPlayPatch(
                true,
                runtime,
                ActionsFor(fake));
            Expect(!result.has_value(), "hook installation failure is reported");
            if (!result)
            {
                Expect(
                    result.error().stage ==
                        gc::auto_play::AutoPlayPatchStage::hook_install &&
                        result.error().site ==
                            gc::auto_play::AutoPlayContractSite::marker_seam &&
                        result.error().safetyhook_error == 7 &&
                        result.error().rollback_attempted &&
                        result.error().rollback_complete,
                    "hook failure retains code and completed reset evidence");
            }
            const std::vector expected{
                Event{
                    EventKind::hook_install,
                    kImageBase + kSiteFixtures[3].rva},
                Event{EventKind::hook_reset, 0},
            };
            Expect(
                MutationEvents(fake) == expected,
                "failed hook is reset before any direct write");
            Expect(fake.sites == initial, "hook failure leaves image unchanged");
            ExpectInactiveAndUnpublished(runtime);
        }

        for (std::size_t failure_index = 0; failure_index < 3; ++failure_index)
        {
            gc::auto_play::AutoPlayRuntimeState runtime{};
            auto fake = MakeFake(runtime);
            const auto initial = fake.sites;
            fake.direct_write_failure_index = failure_index;
            const auto result = gc::auto_play::InstallAutoPlayPatch(
                true,
                runtime,
                ActionsFor(fake));
            Expect(!result.has_value(), "every direct write failure is reported");
            if (!result)
            {
                Expect(
                    result.error().stage ==
                        gc::auto_play::AutoPlayPatchStage::direct_write &&
                        result.error().site == kSiteFixtures[failure_index].site &&
                        result.error().memory_stage ==
                            gc::game_compatibility::GameBinaryMemoryStage::Copy &&
                        result.error().win32_error == ERROR_WRITE_FAULT &&
                        result.error().rollback_attempted &&
                        result.error().rollback_complete,
                    "direct write failure retains original and rollback evidence");
            }

            std::vector<Event> expected{
                Event{
                    EventKind::hook_install,
                    kImageBase + kSiteFixtures[3].rva},
            };
            for (std::size_t index = 0; index <= failure_index; ++index)
            {
                expected.push_back({
                    EventKind::direct_write,
                    kImageBase + kSiteFixtures[index].rva,
                });
            }
            expected.push_back({EventKind::hook_reset, 0});
            for (std::size_t index = failure_index; index-- > 0;)
            {
                expected.push_back({
                    EventKind::direct_write,
                    kImageBase + kSiteFixtures[index].rva,
                });
            }
            Expect(
                MutationEvents(fake) == expected,
                "rollback resets hook then restores owned sites in reverse");
            Expect(fake.sites == initial, "successful rollback restores clean image");
            Expect(
                !fake.mutation_observed_active,
                "failing mutation and rollback remain unpublished");
            ExpectInactiveAndUnpublished(runtime);
        }

        {
            gc::auto_play::AutoPlayRuntimeState runtime{};
            auto fake = MakeFake(runtime);
            SetDirectPatched(fake, 0);
            const auto initial = fake.sites;
            fake.direct_write_failure_index = 2;
            const auto result = gc::auto_play::InstallAutoPlayPatch(
                true,
                runtime,
                ActionsFor(fake));
            Expect(!result.has_value(), "mixed-state write failure is reported");
            const std::vector expected{
                Event{
                    EventKind::hook_install,
                    kImageBase + kSiteFixtures[3].rva},
                Event{
                    EventKind::direct_write,
                    kImageBase + kSiteFixtures[1].rva},
                Event{
                    EventKind::direct_write,
                    kImageBase + kSiteFixtures[2].rva},
                Event{EventKind::hook_reset, 0},
                Event{
                    EventKind::direct_write,
                    kImageBase + kSiteFixtures[1].rva},
            };
            Expect(
                MutationEvents(fake) == expected,
                "rollback never restores a pre-existing patched site");
            Expect(fake.sites == initial, "mixed-state rollback preserves ownership");
            ExpectInactiveAndUnpublished(runtime);
        }

        {
            gc::auto_play::AutoPlayRuntimeState runtime{};
            auto fake = MakeFake(runtime);
            fake.direct_write_failure_index = 2;
            fake.hook_reset_failure = true;
            const auto result = gc::auto_play::InstallAutoPlayPatch(
                true,
                runtime,
                ActionsFor(fake));
            Expect(!result.has_value(), "hook reset failure is reported");
            if (!result)
            {
                Expect(
                    result.error().stage ==
                        gc::auto_play::AutoPlayPatchStage::direct_write &&
                        result.error().site ==
                            gc::auto_play::AutoPlayContractSite::native_auto_play &&
                        result.error().rollback_attempted &&
                        !result.error().rollback_complete &&
                        result.error().rollback_site ==
                            gc::auto_play::AutoPlayContractSite::marker_seam,
                    "hook reset failure preserves the original write error");
            }
            Expect(
                std::ranges::equal(
                    std::span{fake.sites[0].bytes}.first(fake.sites[0].size),
                    kSiteFixtures[0].clean.view()) &&
                    std::ranges::equal(
                        std::span{fake.sites[1].bytes}.first(
                            fake.sites[1].size),
                        kSiteFixtures[1].clean.view()),
                "hook reset failure still attempts every owned restoration");
            ExpectInactiveAndUnpublished(runtime);
        }

        {
            gc::auto_play::AutoPlayRuntimeState runtime{};
            auto fake = MakeFake(runtime);
            fake.direct_write_failure_index = 2;
            fake.rollback_write_failure_address =
                kImageBase + kSiteFixtures[1].rva;
            const auto result = gc::auto_play::InstallAutoPlayPatch(
                true,
                runtime,
                ActionsFor(fake));
            Expect(!result.has_value(), "rollback write failure is reported");
            if (!result)
            {
                Expect(
                    result.error().stage ==
                        gc::auto_play::AutoPlayPatchStage::direct_write &&
                        result.error().site ==
                            gc::auto_play::AutoPlayContractSite::native_auto_play &&
                        result.error().memory_stage ==
                            gc::game_compatibility::GameBinaryMemoryStage::Copy &&
                        result.error().win32_error == ERROR_WRITE_FAULT &&
                        result.error().rollback_attempted &&
                        !result.error().rollback_complete &&
                        result.error().rollback_site ==
                            gc::auto_play::AutoPlayContractSite::complete_is_mute &&
                        result.error().rollback_memory_stage ==
                            gc::game_compatibility::GameBinaryMemoryStage::
                                RestoreProtection &&
                        result.error().rollback_win32_error ==
                            ERROR_ACCESS_DENIED,
                    "rollback write failure retains both original and rollback errors");
            }
            Expect(
                std::ranges::equal(
                    std::span{fake.sites[0].bytes}.first(fake.sites[0].size),
                    kSiteFixtures[0].clean.view()),
                "rollback continues after a failed restoration");
            Expect(
                std::ranges::equal(
                    std::span{fake.sites[1].bytes}.first(fake.sites[1].size),
                    kSiteFixtures[1].patched.view()),
                "failed restoration remains observable in fake memory");
            ExpectInactiveAndUnpublished(runtime);
        }
    }
} // namespace

int main()
{
    MarkerProducerIsInactiveOrEmitsTheFixedContract();
    DisabledAndCommittedInitializationPerformNoOperations();
    EveryDirectStateCombinationCommitsInSafetyOrder();
    EveryPreflightFailureLeavesTheImageAndHookUntouched();
    HookAndDirectWriteFailuresRollbackOnlyOwnedSites();
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

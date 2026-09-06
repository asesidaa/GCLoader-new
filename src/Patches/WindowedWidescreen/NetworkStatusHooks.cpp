#include "Patches/WindowedWidescreen/NetworkStatusHooks.h"
#include "Patches/WindowedWidescreen/WidescreenRuntime.h"
#include <plog/Log.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <span>
#include <string_view>

namespace gc::windowed_widescreen::detail {
NetworkStatusOriginals g_network_originals;
constexpr std::string_view kNetworkStatusNesysClipName = "imc_ico_n";
constexpr std::string_view kNetworkStatusLocalClipName = "imc_ico_l";
static_assert(sizeof(std::uintptr_t) == sizeof(std::uint32_t));

[[nodiscard]] std::uint32_t MovieClipNameHash(
    const std::string_view name, const WidescreenNativeLayout& layout) noexcept
{
    std::uint32_t value{};
    for (const auto character : name)
    {
        value = value * layout.movie_clip_name_hash_multiplier +
            static_cast<std::uint8_t>(character);
    }
    return value;
}
enum class NetworkStatusClip : std::uint8_t
{
    none,
    nesys,
    local,
};
thread_local std::uint32_t g_network_status_native_scope_depth{};
thread_local NetworkStatusClip g_network_status_active_clip{
    NetworkStatusClip::none};
thread_local bool g_network_status_local_matrix_corrected{};

class NetworkStatusNativeScope final
{
public:
    explicit NetworkStatusNativeScope(
        const NetworkStatusClip clip) noexcept
        : previous_clip_{g_network_status_active_clip},
          previous_local_matrix_corrected_{
              g_network_status_local_matrix_corrected}
    {
        ++g_network_status_native_scope_depth;
        g_network_status_active_clip = clip;
        g_network_status_local_matrix_corrected = false;
    }

    ~NetworkStatusNativeScope() noexcept
    {
        g_network_status_local_matrix_corrected =
            previous_local_matrix_corrected_;
        g_network_status_active_clip = previous_clip_;
        if (g_network_status_native_scope_depth != 0)
        {
            --g_network_status_native_scope_depth;
        }
    }

    [[nodiscard]] bool local_matrix_corrected() const noexcept
    {
        return g_network_status_local_matrix_corrected;
    }

    NetworkStatusNativeScope(const NetworkStatusNativeScope&) = delete;
    NetworkStatusNativeScope& operator=(
        const NetworkStatusNativeScope&) = delete;

private:
    NetworkStatusClip previous_clip_{NetworkStatusClip::none};
    bool previous_local_matrix_corrected_{};
};

[[nodiscard]] bool RuntimeCStringEquals(
    const std::uintptr_t address,
    const std::string_view expected) noexcept
{
    std::array<char, 16> actual{};
    if (address == 0 || expected.size() + 1 > actual.size())
    {
        return false;
    }
    const auto output = std::as_writable_bytes(std::span{actual}).
        first(expected.size() + 1);
    return ProductionRead(nullptr, address, output) &&
        std::memcmp(actual.data(), expected.data(), expected.size()) ==
        0 &&
        actual[expected.size()] == '\0';
}

[[nodiscard]] NetworkStatusClip IdentifyNetworkStatusClip(
    const WidescreenNativeLayout& layout, void* const movie_clip) noexcept
{
    if (movie_clip == nullptr)
    {
        return NetworkStatusClip::none;
    }

    const auto address = reinterpret_cast<std::uintptr_t>(movie_clip);
    std::uint32_t name_hash{};
    if (!ProductionRead(
        nullptr,
        address + layout.movie_clip_name_hash_offset,
        std::as_writable_bytes(std::span{&name_hash, 1})))
    {
        return NetworkStatusClip::none;
    }

    NetworkStatusClip target = NetworkStatusClip::none;
    std::string_view expected_name{};
    if (name_hash == MovieClipNameHash(kNetworkStatusNesysClipName, layout))
    {
        target = NetworkStatusClip::nesys;
        expected_name = kNetworkStatusNesysClipName;
    }
    else if (name_hash == MovieClipNameHash(kNetworkStatusLocalClipName, layout))
    {
        target = NetworkStatusClip::local;
        expected_name = kNetworkStatusLocalClipName;
    }
    else
    {
        return NetworkStatusClip::none;
    }

    std::uintptr_t name_address{};
    if (!ReadRuntimePointer(
            nullptr,
            address + layout.movie_clip_name_offset,
            name_address) ||
        !RuntimeCStringEquals(name_address, expected_name))
    {
        return NetworkStatusClip::none;
    }
    return target;
}

[[nodiscard]] bool IsMovieClipDrawVisitor(
    const WindowedWidescreenRuntime& runtime,
    void* const visitor) noexcept
{
    std::uintptr_t vtable{};
    return visitor != nullptr &&
        ReadRuntimePointer(
            nullptr,
            reinterpret_cast<std::uintptr_t>(visitor),
            vtable) &&
        vtable == runtime.abi.movie_clip_draw_visitor_vtable;
}

[[nodiscard]] constexpr const char* NetworkStatusClipName(
    const NetworkStatusClip clip) noexcept
{
    return clip == NetworkStatusClip::nesys
               ? "imc_ico_n"
               : clip == NetworkStatusClip::local
               ? "imc_ico_l"
               : "none";
}

[[nodiscard]] bool NetworkStatusMatrixIsWritable(
    const std::uintptr_t address) noexcept
{
    MEMORY_BASIC_INFORMATION memory{};
    if (address == 0 || VirtualQuery(
            reinterpret_cast<const void*>(address),
            &memory,
            sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
    {
        return false;
    }

    const auto protection = memory.Protect & 0xFFU;
    const bool writable = protection == PAGE_READWRITE ||
        protection == PAGE_WRITECOPY ||
        protection == PAGE_EXECUTE_READWRITE ||
        protection == PAGE_EXECUTE_WRITECOPY;
    const auto region_begin = reinterpret_cast<std::uintptr_t>(
        memory.BaseAddress);
    if (!writable || address < region_begin ||
        memory.RegionSize >
        std::numeric_limits<std::uintptr_t>::max() - region_begin)
    {
        return false;
    }
    const auto region_end = region_begin + memory.RegionSize;
    constexpr auto matrix_size =
        sizeof(NativeNetworkMatrix);
    return address <= region_end && matrix_size <= region_end - address;
}

class ScopedLocalNetworkStatusMatrix final
{
public:
    ScopedLocalNetworkStatusMatrix(
        const WindowedWidescreenRuntime& runtime,
        void* const visitor) noexcept
    {
        const auto output = runtime.resolution.output_size();
        const auto base_hud = ResolveGameplayHudViewport(
            output,
            runtime.settings.gameplay_hud_placement());
        if (visitor == nullptr || output.width <= kNativeWidth ||
            output.height != kNativeHeight || !base_hud)
        {
            return;
        }

        std::uintptr_t matrix_address{};
        if (!ReadRuntimePointer(
                nullptr,
                reinterpret_cast<std::uintptr_t>(visitor) +
                runtime.abi.layout.network_status_visitor_matrix_stack_offset,
                matrix_address) ||
            !NetworkStatusMatrixIsWritable(matrix_address) ||
            !ProductionRead(
                nullptr,
                matrix_address,
                std::as_writable_bytes(std::span{original_})))
        {
            return;
        }

        auto corrected = original_;
        const auto horizontal_scale =
            static_cast<float>(kNativeWidth) /
            static_cast<float>(output.width);
        for (const auto component : kNativeMatrixHorizontalComponents)
            corrected[component] *= horizontal_scale;
        corrected[kNativeMatrixTranslation] = horizontal_scale *
            (corrected[kNativeMatrixTranslation] + static_cast<float>(base_hud->x));

        matrix_ = reinterpret_cast<float*>(matrix_address);
        std::memcpy(
            matrix_,
            corrected.data(),
            sizeof(corrected));
        applied_ = true;
    }

    ~ScopedLocalNetworkStatusMatrix() noexcept
    {
        if (applied_)
        {
            std::memcpy(
                matrix_,
                original_.data(),
                sizeof(original_));
        }
    }

    ScopedLocalNetworkStatusMatrix(
        const ScopedLocalNetworkStatusMatrix&) = delete;
    ScopedLocalNetworkStatusMatrix& operator=(
        const ScopedLocalNetworkStatusMatrix&) = delete;

    [[nodiscard]] bool applied() const noexcept
    {
        return applied_;
    }

private:
    NativeNetworkMatrix original_{};
    float* matrix_{};
    bool applied_{};
};

void CallShapeDrawVisitOriginal(
    void* const visitor,
    void* const definition) noexcept
{
    if (g_network_originals.network_status_shape_draw_visit && visitor && definition)
        g_network_originals.network_status_shape_draw_visit(visitor, definition);

}

void __fastcall NetworkStatusShapeDrawVisitDetour(
    void* const visitor,
    void*,
    void* const definition) noexcept
{
    auto* const runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    if (runtime != nullptr && RuntimeCallbacksAreActive(*runtime) &&
        definition != nullptr &&
        g_network_status_native_scope_depth != 0 &&
        g_network_status_active_clip == NetworkStatusClip::local &&
        runtime->gameplay_frame_active.load(std::memory_order_acquire))
    {
        const ScopedLocalNetworkStatusMatrix corrected{
            *runtime,
            visitor};
        if (corrected.applied())
        {
            g_network_status_local_matrix_corrected = true;
        }
        CallShapeDrawVisitOriginal(visitor, definition);
        return;
    }
    CallShapeDrawVisitOriginal(visitor, definition);
}

[[nodiscard]] int CallMovieClipAcceptOriginal(
    WindowedWidescreenRuntime* const runtime,
    void* const movie_clip,
    void* const visitor) noexcept
{
    (void)runtime;
    return g_network_originals.network_status_movie_clip_accept && movie_clip && visitor
        ? g_network_originals.network_status_movie_clip_accept(movie_clip, visitor) : 0;

}

void LogNetworkStatusCorrection(
    WindowedWidescreenRuntime& runtime,
    const NetworkStatusClip clip,
    void* const movie_clip,
    const RenderSpace space,
    const char* const action,
    const bool succeeded) noexcept
{
    const auto clip_bit = clip == NetworkStatusClip::nesys ? 0U : 1U;
    const auto outcome_bit = succeeded ? 0U : 2U;
    const auto log_bit = 1U << (clip_bit + outcome_bit);
    const auto previous = runtime.network_status_log_mask.fetch_or(
        log_bit,
        std::memory_order_relaxed);
    if ((previous & log_bit) != 0)
    {
        return;
    }
    try
    {
        if (succeeded)
        {
            PLOG_INFO
                << "WindowedWidescreen network-status clip correction"
                << " clip="
                << NetworkStatusClipName(clip)
                << " object=0x" << std::hex
                << reinterpret_cast<std::uintptr_t>(movie_clip)
                << std::dec
                << " frame="
                << runtime.frame_sequence.load(
                    std::memory_order_relaxed)
                << " space=" << static_cast<int>(space)
                << " action=" << action;
        }
        else
        {
            PLOG_WARNING
                << "WindowedWidescreen network-status clip correction"
                << " failed clip="
                << NetworkStatusClipName(clip)
                << " frame="
                << runtime.frame_sequence.load(
                    std::memory_order_relaxed)
                << " space=" << static_cast<int>(space)
                << " action=" << action;
        }
    }
    catch (...)
    {
    }
}

int __fastcall NetworkStatusMovieClipAcceptDetour(
    void* const movie_clip,
    void*,
    void* const visitor) noexcept
{
    auto* const runtime =
        g_callback_runtime.load(std::memory_order_acquire);
    const auto original = [&]() noexcept
    {
        return CallMovieClipAcceptOriginal(
            runtime,
            movie_clip,
            visitor);
    };
    if (runtime == nullptr || !RuntimeCallbacksAreActive(*runtime) ||
        !runtime->gameplay_frame_active.load(
            std::memory_order_acquire))
    {
        return original();
    }

    if (!IsMovieClipDrawVisitor(*runtime, visitor))
    {
        return original();
    }
    const auto clip = IdentifyNetworkStatusClip(runtime->abi.layout, movie_clip);
    if (clip == NetworkStatusClip::none ||
        !runtime->compositor.frame_active())
    {
        return original();
    }
    const auto current_space = runtime->compositor.CurrentSpace();
    if (!current_space)
    {
        return original();
    }

    if (*current_space == RenderSpace::gameplay_hud)
    {
        const auto previous =
            runtime->compositor.gameplay_hud_placement();
        if (!runtime->compositor.ReapplyGameplayHudPlacement(
                runtime->settings.gameplay_hud_placement()))
        {
            LogNetworkStatusCorrection(
                *runtime,
                clip,
                movie_clip,
                *current_space,
                "gameplay-hud-base-begin",
                false);
            return original();
        }

        int result{};
        bool restored{};
        bool local_matrix_corrected{};
        {
            const NetworkStatusNativeScope native_scope{clip};
            result = original();
            local_matrix_corrected =
                native_scope.local_matrix_corrected();
            restored = runtime->compositor.
                ReapplyGameplayHudPlacement(previous).has_value();
        }
        const bool corrected = clip != NetworkStatusClip::local ||
            local_matrix_corrected;
        LogNetworkStatusCorrection(
            *runtime,
            clip,
            movie_clip,
            *current_space,
            !restored
                ? "gameplay-hud-base-restore"
                : corrected
                ? clip == NetworkStatusClip::local
                      ? "gameplay-hud-local-matrix"
                      : "gameplay-hud-base"
                : "gameplay-hud-local-matrix-unavailable",
            restored && corrected);
        return result;
    }

    const bool viewport_scope = runtime->abi.selected_hud_draws_only;
    if (*current_space != RenderSpace::physical_3d ||
        runtime->compositor.physical_gameplay_hud_overlay_active() ||
        (viewport_scope && runtime->compositor.gameplay_hud_draw_active()))
    {
        return original();
    }

    // GWDrawFunc caches bound textures/materials across sibling clips
    // (4E3CA0/4E44E0). A full D3D state restore after a clip invalidates
    // that cache. Current-target Flash draws need only a viewport scope;
    // their own projection and native pipeline retain their normal lifetime.
    const auto begun = viewport_scope
        ? runtime->compositor.BeginGameplayHudDraw(
            runtime->settings.gameplay_hud_placement(), false)
        : runtime->compositor.BeginPhysicalGameplayHudOverlay(
            runtime->settings.gameplay_hud_placement());
    if (!begun)
    {
        LogNetworkStatusCorrection(
            *runtime,
            clip,
            movie_clip,
            *current_space,
            viewport_scope ? "physical-viewport-begin" : "physical-base-overlay-begin",
            false);
        return original();
    }

    int result{};
    bool ended{};
    bool local_matrix_corrected{};
    {
        const NetworkStatusNativeScope native_scope{clip};
        result = original();
        local_matrix_corrected =
            native_scope.local_matrix_corrected();
        const auto ended_result = viewport_scope
            ? runtime->compositor.EndGameplayHudDraw()
            : runtime->compositor.EndPhysicalGameplayHudOverlay();
        ended = ended_result.has_value();
    }
    const bool corrected = clip != NetworkStatusClip::local ||
        local_matrix_corrected;
    const char* action = viewport_scope ? "physical-viewport" : "physical-base-overlay";
    if (!ended)
        action = viewport_scope ? "physical-viewport-end" : "physical-base-overlay-end";
    else if (!corrected)
        action = "physical-local-matrix-unavailable";
    else if (clip == NetworkStatusClip::local)
        action = viewport_scope ? "physical-viewport-local-matrix" : "physical-local-matrix";
    LogNetworkStatusCorrection(
        *runtime,
        clip,
        movie_clip,
        *current_space,
        action,
        ended && corrected);
    return result;
}


}

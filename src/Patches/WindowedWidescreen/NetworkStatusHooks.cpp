#include "Patches/WindowedWidescreen/NetworkStatusHooks.h"
#include "Patches/WindowedWidescreen/WidescreenRuntime.h"
#include <plog/Log.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
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
enum class CommonHudClip : std::uint8_t
{
    none,
    nesys,
    local,
    header_separator,
};
thread_local std::uint32_t g_network_status_native_scope_depth{};
thread_local CommonHudClip g_network_status_active_clip{
    CommonHudClip::none};
thread_local bool g_common_hud_shape_matrix_corrected{};

class NetworkStatusNativeScope final
{
public:
    explicit NetworkStatusNativeScope(
        const CommonHudClip clip) noexcept
        : previous_clip_{g_network_status_active_clip},
          previous_shape_matrix_corrected_{
              g_common_hud_shape_matrix_corrected}
    {
        ++g_network_status_native_scope_depth;
        g_network_status_active_clip = clip;
        g_common_hud_shape_matrix_corrected = false;
    }

    ~NetworkStatusNativeScope() noexcept
    {
        g_common_hud_shape_matrix_corrected =
            previous_shape_matrix_corrected_;
        g_network_status_active_clip = previous_clip_;
        if (g_network_status_native_scope_depth != 0)
        {
            --g_network_status_native_scope_depth;
        }
    }

    [[nodiscard]] bool shape_matrix_corrected() const noexcept
    {
        return g_common_hud_shape_matrix_corrected;
    }

    NetworkStatusNativeScope(const NetworkStatusNativeScope&) = delete;
    NetworkStatusNativeScope& operator=(
        const NetworkStatusNativeScope&) = delete;

private:
    CommonHudClip previous_clip_{CommonHudClip::none};
    bool previous_shape_matrix_corrected_{};
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

[[nodiscard]] bool IsGameplayHeaderSeparator(
    const WidescreenNativeLayout& layout, const std::uintptr_t address) noexcept
{
    if (layout.gameplay_header_separator_symbol.empty())
        return false;

    // 2.06 common.rvb/common_eng.rvb: imc_head -> unnamed UNIQUE_150.
    // Match the definition AND its direct parent, never the whole header.
    std::uintptr_t definition{}, definition_name{}, parent{}, parent_name{};
    return ReadRuntimePointer(nullptr, address + layout.movie_clip_definition_offset, definition) &&
        definition != 0 &&
        ReadRuntimePointer(nullptr, definition + layout.movie_definition_name_offset, definition_name) &&
        RuntimeCStringEquals(definition_name, layout.gameplay_header_separator_symbol) &&
        ReadRuntimePointer(nullptr, address + layout.movie_clip_parent_offset, parent) &&
        parent != 0 &&
        ReadRuntimePointer(nullptr, parent + layout.movie_clip_name_offset, parent_name) &&
        RuntimeCStringEquals(parent_name, "imc_head");
}

[[nodiscard]] CommonHudClip IdentifyCommonHudClip(
    const WidescreenNativeLayout& layout, void* const movie_clip) noexcept
{
    if (movie_clip == nullptr)
    {
        return CommonHudClip::none;
    }

    const auto address = reinterpret_cast<std::uintptr_t>(movie_clip);
    if (IsGameplayHeaderSeparator(layout, address))
        return CommonHudClip::header_separator;
    std::uint32_t name_hash{};
    if (!ProductionRead(
        nullptr,
        address + layout.movie_clip_name_hash_offset,
        std::as_writable_bytes(std::span{&name_hash, 1})))
    {
        return CommonHudClip::none;
    }

    CommonHudClip target = CommonHudClip::none;
    std::string_view expected_name{};
    if (name_hash == MovieClipNameHash(kNetworkStatusNesysClipName, layout))
    {
        target = CommonHudClip::nesys;
        expected_name = kNetworkStatusNesysClipName;
    }
    else if (name_hash == MovieClipNameHash(kNetworkStatusLocalClipName, layout))
    {
        target = CommonHudClip::local;
        expected_name = kNetworkStatusLocalClipName;
    }
    else
    {
        return CommonHudClip::none;
    }

    std::uintptr_t name_address{};
    if (!ReadRuntimePointer(
            nullptr,
            address + layout.movie_clip_name_offset,
            name_address) ||
        !RuntimeCStringEquals(name_address, expected_name))
    {
        return CommonHudClip::none;
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

[[nodiscard]] constexpr const char* CommonHudClipName(
    const CommonHudClip clip) noexcept
{
    return clip == CommonHudClip::nesys
               ? "imc_ico_n"
               : clip == CommonHudClip::local
               ? "imc_ico_l"
               : clip == CommonHudClip::header_separator
               ? "header_separator"
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

[[nodiscard]] constexpr bool RequiresCommonHudShapeMatrix(
    const CommonHudClip clip) noexcept
{
    return clip == CommonHudClip::local || clip == CommonHudClip::header_separator;
}

// These selected shapes reset the hardware viewport to full output during
// native target binding. Compensate only their composed x coordinates and
// restore the visitor matrix before the next shape or sibling clip.
class ScopedCommonHudShapeMatrix final
{
public:
    ScopedCommonHudShapeMatrix(
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

    ~ScopedCommonHudShapeMatrix() noexcept
    {
        if (applied_)
        {
            std::memcpy(
                matrix_,
                original_.data(),
                sizeof(original_));
        }
    }

    ScopedCommonHudShapeMatrix(
        const ScopedCommonHudShapeMatrix&) = delete;
    ScopedCommonHudShapeMatrix& operator=(
        const ScopedCommonHudShapeMatrix&) = delete;

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
        RequiresCommonHudShapeMatrix(g_network_status_active_clip) &&
        runtime->gameplay_frame_active.load(std::memory_order_acquire))
    {
        const ScopedCommonHudShapeMatrix corrected{*runtime, visitor};
        if (corrected.applied())
            g_common_hud_shape_matrix_corrected = true;
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

void LogCommonHudCorrection(
    WindowedWidescreenRuntime& runtime,
    const CommonHudClip clip,
    const RenderSpace space,
    const char* const action,
    const bool succeeded) noexcept
{
    if (succeeded)
        return;

    // At most one failure warning per selected clip; successful draws are silent.
    const auto log_bit = 1U << (static_cast<unsigned>(clip) - 1U);
    const auto previous = runtime.network_status_log_mask.fetch_or(
        log_bit,
        std::memory_order_relaxed);
    if ((previous & log_bit) != 0)
        return;

    try
    {
        PLOG_WARNING
            << "WindowedWidescreen common-HUD clip correction"
            << " failed clip=" << CommonHudClipName(clip)
            << " frame=" << runtime.frame_sequence.load(std::memory_order_relaxed)
            << " space=" << static_cast<int>(space)
            << " action=" << action;
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
    const auto clip = IdentifyCommonHudClip(runtime->abi.layout, movie_clip);
    if (clip == CommonHudClip::none ||
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
            LogCommonHudCorrection(
                *runtime,
                clip,
                *current_space,
                "gameplay-hud-base-begin",
                false);
            return original();
        }

        int result{};
        bool restored{};
        bool shape_matrix_corrected{};
        {
            const NetworkStatusNativeScope native_scope{clip};
            result = original();
            shape_matrix_corrected =
                native_scope.shape_matrix_corrected();
            restored = runtime->compositor.
                ReapplyGameplayHudPlacement(previous).has_value();
        }
        const bool corrected = !RequiresCommonHudShapeMatrix(clip) ||
            shape_matrix_corrected;
        LogCommonHudCorrection(
            *runtime,
            clip,
            *current_space,
            !restored
                ? "gameplay-hud-base-restore"
                : corrected
                ? clip == CommonHudClip::local
                      ? "gameplay-hud-local-matrix"
                      : clip == CommonHudClip::header_separator
                      ? "gameplay-hud-separator-matrix"
                      : "gameplay-hud-base"
                : "gameplay-hud-shape-matrix-unavailable",
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
    // (4.74 4E3CA0/4E44E0; 2.06 4D8DE0/4D9620). Full state restoration
    // after a clip invalidates that cache. Flash needs only a viewport scope;
    // their own projection and native pipeline retain their normal lifetime.
    const auto begun = viewport_scope
        ? runtime->compositor.BeginGameplayHudDraw(
            runtime->settings.gameplay_hud_placement(), false)
        : runtime->compositor.BeginPhysicalGameplayHudOverlay(
            runtime->settings.gameplay_hud_placement());
    if (!begun)
    {
        LogCommonHudCorrection(
            *runtime,
            clip,
            *current_space,
            viewport_scope ? "physical-viewport-begin" : "physical-base-overlay-begin",
            false);
        return original();
    }

    int result{};
    bool ended{};
    bool shape_matrix_corrected{};
    {
        const NetworkStatusNativeScope native_scope{clip};
        result = original();
        shape_matrix_corrected =
            native_scope.shape_matrix_corrected();
        const auto ended_result = viewport_scope
            ? runtime->compositor.EndGameplayHudDraw()
            : runtime->compositor.EndPhysicalGameplayHudOverlay();
        ended = ended_result.has_value();
    }
    const bool corrected = !RequiresCommonHudShapeMatrix(clip) ||
        shape_matrix_corrected;
    const char* action = viewport_scope ? "physical-viewport" : "physical-base-overlay";
    if (!ended)
        action = viewport_scope ? "physical-viewport-end" : "physical-base-overlay-end";
    else if (!corrected)
        action = "physical-shape-matrix-unavailable";
    else if (clip == CommonHudClip::local)
        action = viewport_scope ? "physical-viewport-local-matrix" : "physical-local-matrix";
    else if (clip == CommonHudClip::header_separator)
        action = viewport_scope ? "physical-viewport-separator-matrix" : "physical-separator-matrix";
    LogCommonHudCorrection(
        *runtime,
        clip,
        *current_space,
        action,
        ended && corrected);
    return result;
}


}

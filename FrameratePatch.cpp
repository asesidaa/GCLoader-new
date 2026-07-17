#include "FrameratePatch.h"

#include "CountdownTimerFreeze.h"
#include "Config/config.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>

#include <safetyhook.hpp>
#include "plog/Log.h"

namespace {

constexpr uintptr_t kRvaGwMainRunUpdatePhase = 0x00058B70;

constexpr uintptr_t kRvaFrameMsGameplay = 0x002FC0A0;
constexpr uintptr_t kRvaFrameMsVisualLane = 0x002F4604;
constexpr uintptr_t kRvaFrameSecondsGameplay = 0x002FC280;
constexpr uintptr_t kRvaRenderSmoothingStep4 = 0x002E8F00;
constexpr uintptr_t kRvaRenderOffsetDecayStep5 = 0x002E8F04;

constexpr uintptr_t kRvaRepeatInitialDelayImm = 0x00055CCC;
constexpr uintptr_t kRvaRepeatNextDelayImm = 0x00055CDD;

constexpr uintptr_t kRvaTuneCountdownInit = 0x002645EE;
constexpr uintptr_t kRvaTuneCountdownCompare = 0x002648F7;
constexpr uintptr_t kRvaTuneRenderCountdownEax = 0x00249A5E;
constexpr uintptr_t kRvaTuneRenderCountdownEdx = 0x00249A73;

constexpr uintptr_t kRvaPaletteSmoothingCapCompare = 0x0022BA60;
constexpr uintptr_t kRvaPaletteFsubr60Normalizer = 0x0022BACF;
constexpr uintptr_t kRvaPaletteFdiv60Normalizer = 0x0022BAD5;
constexpr uintptr_t kRvaChartSecondsToFramesMul60 = 0x00262CB6;

constexpr uintptr_t kRvaAnimMovieClipGotoFrameInternal = 0x000DEA30;
constexpr uintptr_t kRvaAnimMovieClipAdvanceOneTimelineFrame = 0x000DF940;
constexpr uintptr_t kRvaNewsTaskUpdateLoadAndStateMachine = 0x00218A50;
constexpr uintptr_t kRvaNoticeTaskUpdateStateMachine = 0x002544D0;
constexpr uintptr_t kRvaStage3dClipFrameIndexStore = 0x00244054;

constexpr uintptr_t kRvaIfblIntegerWaitStore = 0x002309D4;
constexpr uintptr_t kRvaIfblLoopCounterStore = 0x00230AB6;
constexpr uintptr_t kRvaStageBgmPreloadDelayIncrement = 0x0021001A;
constexpr uintptr_t kRvaGameplayAudioSkipMarginLoaded = 0x0024018F;
constexpr uintptr_t kRvaGameplayAudioSkipIntervalIdiv = 0x002401BD;
constexpr uintptr_t kRvaGameplayAudioResyncSeek = 0x002401C4;

constexpr double kAuthoredUiStepSeconds = 1.0 / 60.0;
constexpr double kMaxAccumulatedSeconds = 1.0 / 30.0;
constexpr float kFrameMs120Hz = 1000.0f / 120.0f;
constexpr float kFrameSeconds120Hz = 1.0f / 120.0f;
constexpr float kRenderSmoothingStep120Hz = 2.0f;
constexpr float kRenderOffsetDecayStep120Hz = 2.5f;
constexpr uint32_t kFrames2SecondsAt120Hz = 0xF0;
constexpr uint32_t kPaletteSmoothingCap120Hz = 0x78;
constexpr int32_t kMinimumAudioSkipMarginMs = 48;
constexpr int32_t kAudioSkipInterval120HzMultiplier = 2;

float g_f32_120_0 = 120.0f;

safetyhook::MidHook g_update_hook{};
safetyhook::InlineHook g_anim_movieclip_goto_frame_hook{};
safetyhook::InlineHook g_anim_movieclip_advance_hook{};
safetyhook::InlineHook g_news_task_update_hook{};
safetyhook::InlineHook g_notice_task_update_hook{};
safetyhook::MidHook g_stage_3d_clip_frame_index_hook{};
safetyhook::MidHook g_ifbl_integer_wait_store_hook{};
safetyhook::MidHook g_ifbl_loop_counter_store_hook{};
safetyhook::MidHook g_stage_bgm_preload_delay_hook{};
safetyhook::MidHook g_tune_countdown_compare_hook{};
safetyhook::MidHook g_gameplay_audio_skip_margin_hook{};
safetyhook::MidHook g_gameplay_audio_skip_interval_hook{};
safetyhook::MidHook g_gameplay_audio_resync_seek_hook{};

std::atomic_bool g_authored_60hz_tick{true};
std::atomic_uint64_t g_update_outer_calls{0};
std::atomic_uint64_t g_authored_60hz_ticks{0};
std::atomic_uint64_t g_authored_60hz_non_ticks{0};
std::atomic_uint64_t g_anim_movieclip_advance_calls{0};
std::atomic_uint64_t g_anim_movieclip_advance_skips{0};
std::atomic_uint64_t g_anim_movieclip_advance_goto_calls{0};
std::atomic_uint64_t g_news_task_update_calls{0};
std::atomic_uint64_t g_news_task_update_skips{0};
std::atomic_uint64_t g_notice_task_update_calls{0};
std::atomic_uint64_t g_notice_task_update_skips{0};
std::atomic_uint64_t g_stage_3d_clip_frame_indices{0};
std::atomic_uint64_t g_stage_3d_clip_frame_halves{0};
std::atomic_uint64_t g_ifbl_integer_wait_stores{0};
std::atomic_uint64_t g_ifbl_loop_counter_stores{0};
std::atomic_uint64_t g_stage_bgm_preload_delay_calls{0};
std::atomic_uint64_t g_stage_bgm_preload_delay_skips{0};
std::atomic_uint64_t g_tune_countdown_compare_hits{0};
std::atomic_uint64_t g_gameplay_audio_resync_seeks{0};
std::atomic_uint64_t g_gameplay_audio_resync_margin_seeks{0};
std::atomic_uint64_t g_gameplay_audio_resync_interval_seeks{0};
std::atomic_uint64_t g_gameplay_audio_skip_margin_clamps{0};
std::atomic_uint64_t g_gameplay_audio_skip_interval_conversions{0};

thread_local int g_anim_movieclip_goto_depth = 0;

uintptr_t exe_base() {
    static const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    return base;
}

void* rva_ptr(uintptr_t rva) {
    return reinterpret_cast<void*>(exe_base() + rva);
}

uintptr_t rva_addr(uintptr_t rva) {
    return exe_base() + rva;
}

bool make_writable(void* address, size_t size, DWORD& old_protect) {
    return VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &old_protect) != FALSE;
}

bool write_bytes(void* address, const void* data, size_t size) {
    DWORD old_protect = 0;
    if (!make_writable(address, size, old_protect)) {
        PLOG_ERROR << "GC120FPS: VirtualProtect failed at " << address << ", gle=" << GetLastError();
        return false;
    }

    std::memcpy(address, data, size);
    FlushInstructionCache(GetCurrentProcess(), address, size);

    DWORD ignored = 0;
    VirtualProtect(address, size, old_protect, &ignored);
    return true;
}

bool bytes_match(uintptr_t address, const uint8_t* expected, size_t size) {
    __try {
        return std::memcmp(reinterpret_cast<const void*>(address), expected, size) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
bool value_matches(uintptr_t address, const T& expected) {
    __try {
        return std::memcmp(reinterpret_cast<const void*>(address), &expected, sizeof(T)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
bool patch_value_checked(uintptr_t rva, const T& expected, const T& value, const char* name) {
    const uintptr_t address = rva_addr(rva);
    if (!value_matches(address, expected)) {
        PLOG_ERROR << "GC120FPS: " << name << " expected value mismatch at " << reinterpret_cast<void*>(address);
        return false;
    }
    if (!write_bytes(reinterpret_cast<void*>(address), &value, sizeof(T))) {
        PLOG_ERROR << "GC120FPS: failed to patch " << name;
        return false;
    }
    PLOG_INFO << "GC120FPS: patched " << name;
    return true;
}

bool patch_mov_dword_imm_checked(uintptr_t rva, uint32_t expected, uint32_t value, const char* name) {
    const uintptr_t address = rva_addr(rva);
    const uint8_t expected_prefix[] = {0xC7, 0x00};
    if (!bytes_match(address, expected_prefix, sizeof(expected_prefix))
        || !value_matches(address + 2, expected)) {
        PLOG_ERROR << "GC120FPS: " << name << " instruction mismatch at " << reinterpret_cast<void*>(address);
        return false;
    }
    if (!write_bytes(reinterpret_cast<void*>(address + 2), &value, sizeof(value))) {
        PLOG_ERROR << "GC120FPS: failed to patch " << name;
        return false;
    }
    PLOG_INFO << "GC120FPS: patched " << name;
    return true;
}

bool patch_c7_80_dword_imm_checked(uintptr_t rva, uint32_t displacement, uint32_t expected, uint32_t value, const char* name) {
    const uintptr_t address = rva_addr(rva);
    const uint8_t expected_prefix[] = {
        0xC7, 0x80,
        static_cast<uint8_t>(displacement & 0xFF),
        static_cast<uint8_t>((displacement >> 8) & 0xFF),
        static_cast<uint8_t>((displacement >> 16) & 0xFF),
        static_cast<uint8_t>((displacement >> 24) & 0xFF),
    };
    if (!bytes_match(address, expected_prefix, sizeof(expected_prefix))
        || !value_matches(address + 6, expected)) {
        PLOG_ERROR << "GC120FPS: " << name << " instruction mismatch at " << reinterpret_cast<void*>(address);
        return false;
    }
    if (!write_bytes(reinterpret_cast<void*>(address + 6), &value, sizeof(value))) {
        PLOG_ERROR << "GC120FPS: failed to patch " << name;
        return false;
    }
    PLOG_INFO << "GC120FPS: patched " << name;
    return true;
}

bool patch_cmp_mem32_imm8_checked(uintptr_t rva, uint8_t expected, uint8_t value, const char* name) {
    const uintptr_t address = rva_addr(rva);
    const uint8_t expected_prefix[] = {0x83, 0x78, 0x0C};
    if (!bytes_match(address, expected_prefix, sizeof(expected_prefix))
        || !value_matches(address + 3, expected)) {
        PLOG_ERROR << "GC120FPS: " << name << " instruction mismatch at " << reinterpret_cast<void*>(address);
        return false;
    }
    if (!write_bytes(reinterpret_cast<void*>(address + 3), &value, sizeof(value))) {
        PLOG_ERROR << "GC120FPS: failed to patch " << name;
        return false;
    }
    PLOG_INFO << "GC120FPS: patched " << name;
    return true;
}

bool patch_mov_reg_imm32_checked(uintptr_t rva, uint8_t opcode, uint32_t expected, uint32_t value, const char* name) {
    const uintptr_t address = rva_addr(rva);
    if (!value_matches(address, opcode) || !value_matches(address + 1, expected)) {
        PLOG_ERROR << "GC120FPS: " << name << " instruction mismatch at " << reinterpret_cast<void*>(address);
        return false;
    }
    if (!write_bytes(reinterpret_cast<void*>(address + 1), &value, sizeof(value))) {
        PLOG_ERROR << "GC120FPS: failed to patch " << name;
        return false;
    }
    PLOG_INFO << "GC120FPS: patched " << name;
    return true;
}

bool patch_x87_m32_operand_checked(uintptr_t rva, uint8_t opcode1, uint8_t opcode2, uintptr_t expected_address, const float* value, const char* name) {
    const uintptr_t address = rva_addr(rva);
    const uint8_t expected_prefix[] = {opcode1, opcode2};
    const auto expected_operand = static_cast<uint32_t>(expected_address);
    const auto replacement_operand = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));
    if (!bytes_match(address, expected_prefix, sizeof(expected_prefix))
        || !value_matches(address + 2, expected_operand)) {
        PLOG_ERROR << "GC120FPS: " << name << " instruction mismatch at " << reinterpret_cast<void*>(address);
        return false;
    }
    if (!write_bytes(reinterpret_cast<void*>(address + 2), &replacement_operand, sizeof(replacement_operand))) {
        PLOG_ERROR << "GC120FPS: failed to patch " << name;
        return false;
    }
    PLOG_INFO << "GC120FPS: patched " << name << " -> " << value;
    return true;
}

bool is_authored_60hz_tick() {
    return g_authored_60hz_tick.load(std::memory_order_acquire);
}

void skip_relocated_instruction(safetyhook::Context& ctx, uintptr_t size) {
    ctx.eip += size;
}

uint32_t double_positive_frame_count(uintptr_t raw_value) {
    const auto signed_value = static_cast<int32_t>(raw_value);
    if (signed_value <= 0) {
        return static_cast<uint32_t>(raw_value);
    }
    if (signed_value > (std::numeric_limits<int32_t>::max() / 2)) {
        return static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
    }
    return static_cast<uint32_t>(signed_value * 2);
}

bool write_u32_safe(uintptr_t address, uint32_t value) {
    __try {
        *reinterpret_cast<volatile uint32_t*>(address) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool read_u32_safe(uintptr_t address, uint32_t& value) {
    __try {
        value = *reinterpret_cast<volatile uint32_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int32_t read_i32_stack(safetyhook::Context& ctx, intptr_t offset, int32_t fallback = 0) {
    uint32_t raw = 0;
    if (!read_u32_safe(ctx.ebp + offset, raw)) {
        return fallback;
    }
    return static_cast<int32_t>(raw);
}

void set_zero_flag(safetyhook::Context& ctx, bool is_zero) {
    constexpr uintptr_t kZeroFlag = 0x40;
    if (is_zero) {
        ctx.eflags |= kZeroFlag;
    } else {
        ctx.eflags &= ~kZeroFlag;
    }
}

void hook_gwmain_update_phase(safetyhook::Context& ctx) {
    (void)ctx;

    static LARGE_INTEGER frequency{};
    static LARGE_INTEGER last_counter{};
    static LARGE_INTEGER last_log_counter{};
    static bool qpc_ready = false;
    static double accumulator = 0.0;

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    g_update_outer_calls.fetch_add(1, std::memory_order_relaxed);

    if (!qpc_ready) {
        QueryPerformanceFrequency(&frequency);
        last_counter = now;
        last_log_counter = now;
        qpc_ready = true;
        g_authored_60hz_tick.store(true, std::memory_order_release);
        g_authored_60hz_ticks.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    double dt = 0.0;
    if (frequency.QuadPart > 0) {
        dt = static_cast<double>(now.QuadPart - last_counter.QuadPart) / static_cast<double>(frequency.QuadPart);
    }
    last_counter = now;
    if (dt < 0.0) {
        dt = 0.0;
    }
    if (dt > kMaxAccumulatedSeconds) {
        dt = kMaxAccumulatedSeconds;
    }

    accumulator += dt;
    bool authored_tick = false;
    if (accumulator >= kAuthoredUiStepSeconds) {
        accumulator -= kAuthoredUiStepSeconds;
        authored_tick = true;
        g_authored_60hz_ticks.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_authored_60hz_non_ticks.fetch_add(1, std::memory_order_relaxed);
    }
    g_authored_60hz_tick.store(authored_tick, std::memory_order_release);

    if (frequency.QuadPart > 0 && now.QuadPart - last_log_counter.QuadPart >= frequency.QuadPart * 5) {
        last_log_counter = now;
        PLOG_INFO << "GC120FPS: full120_stats outer="
                  << g_update_outer_calls.load(std::memory_order_relaxed)
                  << " authored60="
                  << g_authored_60hz_ticks.load(std::memory_order_relaxed)
                  << " non60="
                  << g_authored_60hz_non_ticks.load(std::memory_order_relaxed)
                  << " movieclip="
                  << g_anim_movieclip_advance_calls.load(std::memory_order_relaxed)
                  << "/skip="
                  << g_anim_movieclip_advance_skips.load(std::memory_order_relaxed)
                  << "/goto="
                  << g_anim_movieclip_advance_goto_calls.load(std::memory_order_relaxed)
                  << " news="
                  << g_news_task_update_calls.load(std::memory_order_relaxed)
                  << "/skip="
                  << g_news_task_update_skips.load(std::memory_order_relaxed)
                  << " notice="
                  << g_notice_task_update_calls.load(std::memory_order_relaxed)
                  << "/skip="
                  << g_notice_task_update_skips.load(std::memory_order_relaxed)
                  << " stage3d_clip_frame="
                  << g_stage_3d_clip_frame_indices.load(std::memory_order_relaxed)
                  << "/halved="
                  << g_stage_3d_clip_frame_halves.load(std::memory_order_relaxed)
                  << " ifbl_waits="
                  << g_ifbl_integer_wait_stores.load(std::memory_order_relaxed)
                  << "/loops="
                  << g_ifbl_loop_counter_stores.load(std::memory_order_relaxed)
                  << " bgm_preload="
                  << g_stage_bgm_preload_delay_calls.load(std::memory_order_relaxed)
                  << "/skip="
                  << g_stage_bgm_preload_delay_skips.load(std::memory_order_relaxed)
                  << " countdown_cmp_hits="
                  << g_tune_countdown_compare_hits.load(std::memory_order_relaxed)
                  << " audio_resync="
                  << g_gameplay_audio_resync_seeks.load(std::memory_order_relaxed)
                  << "/margin="
                  << g_gameplay_audio_resync_margin_seeks.load(std::memory_order_relaxed)
                  << "/interval="
                  << g_gameplay_audio_resync_interval_seeks.load(std::memory_order_relaxed)
                  << "/margin_clamps="
                  << g_gameplay_audio_skip_margin_clamps.load(std::memory_order_relaxed)
                  << "/interval_120="
                  << g_gameplay_audio_skip_interval_conversions.load(std::memory_order_relaxed)
                  << " accum=" << accumulator;
    }
}

char __fastcall hook_anim_movieclip_goto_frame_internal(void* self, void*, int frame, int subframe) {
    struct GotoDepthGuard {
        GotoDepthGuard() { ++g_anim_movieclip_goto_depth; }
        ~GotoDepthGuard() { --g_anim_movieclip_goto_depth; }
    } guard;

    return g_anim_movieclip_goto_frame_hook.unsafe_thiscall<char>(self, frame, subframe);
}

char __fastcall hook_anim_movieclip_advance_one_timeline_frame(void* self, void*, char forward, char loop) {
    if (g_anim_movieclip_goto_depth > 0) {
        g_anim_movieclip_advance_goto_calls.fetch_add(1, std::memory_order_relaxed);
        return g_anim_movieclip_advance_hook.unsafe_thiscall<char>(self, forward, loop);
    }

    if (!is_authored_60hz_tick()) {
        g_anim_movieclip_advance_skips.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }

    g_anim_movieclip_advance_calls.fetch_add(1, std::memory_order_relaxed);
    return g_anim_movieclip_advance_hook.unsafe_thiscall<char>(self, forward, loop);
}

int __fastcall hook_news_task_update_load_and_state_machine(void* self, void*) {
    if (!is_authored_60hz_tick()) {
        g_news_task_update_skips.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    g_news_task_update_calls.fetch_add(1, std::memory_order_relaxed);
    return g_news_task_update_hook.unsafe_thiscall<int>(self);
}

int __fastcall hook_notice_task_update_state_machine(void* self, void*) {
    if (!is_authored_60hz_tick()) {
        g_notice_task_update_skips.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    g_notice_task_update_calls.fetch_add(1, std::memory_order_relaxed);
    return g_notice_task_update_hook.unsafe_thiscall<int>(self);
}

void hook_stage_3d_clip_frame_index_store(safetyhook::Context& ctx) {
    g_stage_3d_clip_frame_indices.fetch_add(1, std::memory_order_relaxed);
    // Stage *_clip.dat masks are authored at 60Hz; ms-based stage timelines stay unscaled.
    ctx.ecx /= 2;
    g_stage_3d_clip_frame_halves.fetch_add(1, std::memory_order_relaxed);
}

void hook_ifbl_integer_wait_store(safetyhook::Context& ctx) {
    const uint32_t doubled = double_positive_frame_count(ctx.ecx);
    if (write_u32_safe(ctx.edx + 0x3C, doubled)) {
        g_ifbl_integer_wait_stores.fetch_add(1, std::memory_order_relaxed);
        skip_relocated_instruction(ctx, 3);
    }
}

void hook_ifbl_loop_counter_store(safetyhook::Context& ctx) {
    const uint32_t doubled = double_positive_frame_count(ctx.ecx);
    if (write_u32_safe(ctx.eax + ctx.edx * 4 + 0x1C, doubled)) {
        g_ifbl_loop_counter_stores.fetch_add(1, std::memory_order_relaxed);
        skip_relocated_instruction(ctx, 4);
    }
}

void hook_stage_bgm_preload_delay_increment(safetyhook::Context& ctx) {
    g_stage_bgm_preload_delay_calls.fetch_add(1, std::memory_order_relaxed);
    if (!is_authored_60hz_tick()) {
        g_stage_bgm_preload_delay_skips.fetch_add(1, std::memory_order_relaxed);
        skip_relocated_instruction(ctx, 3);
    }
}

void hook_tune_countdown_compare(safetyhook::Context& ctx) {
    uint32_t countdown = 0;
    if (!read_u32_safe(ctx.edx + 0x1D14, countdown)) {
        return;
    }

    set_zero_flag(ctx, countdown == kFrames2SecondsAt120Hz);
    if (countdown == kFrames2SecondsAt120Hz) {
        g_tune_countdown_compare_hits.fetch_add(1, std::memory_order_relaxed);
    }
    skip_relocated_instruction(ctx, 7);
}

void hook_gameplay_audio_skip_margin_loaded(safetyhook::Context& ctx) {
    const int32_t margin_ms = read_i32_stack(ctx, -0x24);
    if (margin_ms <= 0 || margin_ms >= kMinimumAudioSkipMarginMs) {
        return;
    }

    if (write_u32_safe(ctx.ebp - 0x24, static_cast<uint32_t>(kMinimumAudioSkipMarginMs))) {
        g_gameplay_audio_skip_margin_clamps.fetch_add(1, std::memory_order_relaxed);
    }
}

void hook_gameplay_audio_skip_interval_idiv(safetyhook::Context& ctx) {
    uint32_t raw_interval = 0;
    if (!read_u32_safe(ctx.ecx + 0x3C, raw_interval)) {
        return;
    }

    const auto interval = static_cast<int32_t>(raw_interval);
    if (interval <= 0 || interval > (std::numeric_limits<int32_t>::max() / kAudioSkipInterval120HzMultiplier)) {
        return;
    }

    const int32_t divisor = interval * kAudioSkipInterval120HzMultiplier;
    const int64_t dividend =
        (static_cast<int64_t>(static_cast<int32_t>(ctx.edx)) << 32) |
        static_cast<uint32_t>(ctx.eax);
    const int64_t quotient = dividend / divisor;
    const int64_t remainder = dividend % divisor;
    if (quotient < std::numeric_limits<int32_t>::min() ||
        quotient > std::numeric_limits<int32_t>::max()) {
        return;
    }

    ctx.eax = static_cast<uint32_t>(static_cast<int32_t>(quotient));
    ctx.edx = static_cast<uint32_t>(static_cast<int32_t>(remainder));
    g_gameplay_audio_skip_interval_conversions.fetch_add(1, std::memory_order_relaxed);
    skip_relocated_instruction(ctx, 3);
}

void hook_gameplay_audio_resync_seek(safetyhook::Context& ctx) {
    const int32_t expected_ms = read_i32_stack(ctx, -0x08);
    const int32_t cursor_ms = read_i32_stack(ctx, -0x10, -1);
    const int32_t drift_ms = read_i32_stack(ctx, -0x0C);
    const int32_t margin_ms = read_i32_stack(ctx, -0x24);
    const auto tune = static_cast<uintptr_t>(read_i32_stack(ctx, -0x28));

    uint32_t frame_counter = 0;
    uint32_t frame_step = 0;
    read_u32_safe(tune + 0x10, frame_counter);
    read_u32_safe(tune + 0x14, frame_step);

    const int64_t abs_drift = drift_ms < 0 ? -static_cast<int64_t>(drift_ms) : static_cast<int64_t>(drift_ms);
    const bool margin_seek = abs_drift > margin_ms;
    const uint64_t total = g_gameplay_audio_resync_seeks.fetch_add(1, std::memory_order_relaxed) + 1;
    if (margin_seek) {
        g_gameplay_audio_resync_margin_seeks.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_gameplay_audio_resync_interval_seeks.fetch_add(1, std::memory_order_relaxed);
    }

    if (total <= 120 || (total % 60) == 0) {
        PLOG_INFO << "GC120FPS_AUDIO: resync_seek#=" << total
                  << " reason=" << (margin_seek ? "margin" : "interval")
                  << " frame=" << frame_counter
                  << " step=" << frame_step
                  << " expected_ms=" << expected_ms
                  << " cursor_ms=" << cursor_ms
                  << " drift_ms=" << drift_ms
                  << " abs_drift_ms=" << abs_drift
                  << " skip_margin_ms=" << margin_ms;
    }
}

void install_constant_patches() {
    PLOG_INFO << "GC120FPS: full-120 mode; built-in present limiter stays unpatched. Use IntervalMode=1 and an external 120 FPS cap.";

    patch_value_checked(kRvaFrameMsGameplay, 1000.0f / 60.0f, kFrameMs120Hz, "gameplay frame-ms 1000/60->1000/120");
    patch_value_checked(kRvaFrameMsVisualLane, 1000.0f / 60.0f, kFrameMs120Hz, "visual frame-ms 1000/60->1000/120");
    patch_value_checked(kRvaFrameSecondsGameplay, 1.0f / 60.0f, kFrameSeconds120Hz, "gameplay frame seconds 1/60->1/120");
    patch_value_checked(kRvaRenderSmoothingStep4, 4.0f, kRenderSmoothingStep120Hz, "render smoothing step 4->2");
    patch_value_checked(kRvaRenderOffsetDecayStep5, 5.0f, kRenderOffsetDecayStep120Hz, "render offset decay step 5->2.5");

    patch_mov_dword_imm_checked(kRvaRepeatInitialDelayImm, 0x10, 0x20, "XIO repeat initial delay 0x10->0x20");
    patch_mov_dword_imm_checked(kRvaRepeatNextDelayImm, 0x08, 0x10, "XIO repeat next delay 0x08->0x10");

    patch_c7_80_dword_imm_checked(kRvaTuneCountdownInit, 0x1D14, 0x78, kFrames2SecondsAt120Hz, "Tune+0x1D14 countdown init 0x78->0xF0");
    patch_mov_reg_imm32_checked(kRvaTuneRenderCountdownEax, 0xB8, 0x78, kFrames2SecondsAt120Hz, "render countdown EAX 0x78->0xF0");
    patch_mov_reg_imm32_checked(kRvaTuneRenderCountdownEdx, 0xBA, 0x78, kFrames2SecondsAt120Hz, "render countdown EDX 0x78->0xF0");

    patch_cmp_mem32_imm8_checked(kRvaPaletteSmoothingCapCompare, 0x3C, kPaletteSmoothingCap120Hz, "palette smoothing cap 0x3C->0x78");

    const uintptr_t shared_60 = rva_addr(0x002FBBAC);
    patch_x87_m32_operand_checked(kRvaPaletteFsubr60Normalizer, 0xD8, 0x2D, shared_60, &g_f32_120_0, "palette fsubr normalizer 60->120");
    patch_x87_m32_operand_checked(kRvaPaletteFdiv60Normalizer, 0xD8, 0x35, shared_60, &g_f32_120_0, "palette fdiv normalizer 60->120");
    patch_x87_m32_operand_checked(kRvaChartSecondsToFramesMul60, 0xD8, 0x0D, shared_60, &g_f32_120_0, "chart seconds-to-frames 60->120");
}

void install_safety_hooks() {
    g_update_hook = safetyhook::create_mid(rva_ptr(kRvaGwMainRunUpdatePhase), hook_gwmain_update_phase);
    if (!g_update_hook) {
        PLOG_ERROR << "GC120FPS: failed to midhook GWMain update phase";
    } else {
        PLOG_INFO << "GC120FPS: midhooked GWMain update phase as authored-60Hz clock only";
    }

    PLOG_INFO << "GC120FPS: no generic gameplay/render/input/audio skip gates installed";
    PLOG_INFO << "GC120FPS: XIO edge accessors remain native; input is polled by the game's normal 120Hz update path";

    g_anim_movieclip_goto_frame_hook = safetyhook::create_inline(
        rva_ptr(kRvaAnimMovieClipGotoFrameInternal),
        reinterpret_cast<void*>(hook_anim_movieclip_goto_frame_internal));
    if (!g_anim_movieclip_goto_frame_hook) {
        PLOG_ERROR << "GC120FPS: failed to hook MovieClip goto-frame internal";
    } else {
        PLOG_INFO << "GC120FPS: hooked MovieClip goto-frame depth guard";
    }

    g_anim_movieclip_advance_hook = safetyhook::create_inline(
        rva_ptr(kRvaAnimMovieClipAdvanceOneTimelineFrame),
        reinterpret_cast<void*>(hook_anim_movieclip_advance_one_timeline_frame));
    if (!g_anim_movieclip_advance_hook) {
        PLOG_ERROR << "GC120FPS: failed to hook MovieClip timeline advance";
    } else {
        PLOG_INFO << "GC120FPS: hooked MovieClip ordinary timeline 60Hz gate";
    }

    g_news_task_update_hook = safetyhook::create_inline(
        rva_ptr(kRvaNewsTaskUpdateLoadAndStateMachine),
        reinterpret_cast<void*>(hook_news_task_update_load_and_state_machine));
    if (!g_news_task_update_hook) {
        PLOG_ERROR << "GC120FPS: failed to hook news/banner task update";
    } else {
        PLOG_INFO << "GC120FPS: hooked optional news/banner authored-60Hz gate";
    }

    g_notice_task_update_hook = safetyhook::create_inline(
        rva_ptr(kRvaNoticeTaskUpdateStateMachine),
        reinterpret_cast<void*>(hook_notice_task_update_state_machine));
    if (!g_notice_task_update_hook) {
        PLOG_ERROR << "GC120FPS: failed to hook notice task update";
    } else {
        PLOG_INFO << "GC120FPS: hooked optional notice authored-60Hz gate";
    }

    const uint8_t expected_stage_3d_clip_frame_index_store[] = {0x89, 0x4D, 0xF8};
    if (!bytes_match(rva_addr(kRvaStage3dClipFrameIndexStore),
                     expected_stage_3d_clip_frame_index_store,
                     sizeof(expected_stage_3d_clip_frame_index_store))) {
        PLOG_ERROR << "GC120FPS: stage 3D clip frame-index store instruction mismatch";
    } else {
        g_stage_3d_clip_frame_index_hook = safetyhook::create_mid(
            rva_ptr(kRvaStage3dClipFrameIndexStore),
            hook_stage_3d_clip_frame_index_store);
        if (!g_stage_3d_clip_frame_index_hook) {
            PLOG_ERROR << "GC120FPS: failed to midhook stage 3D clip frame-index store";
        } else {
            PLOG_INFO << "GC120FPS: midhooked stage 3D clip frame index as authored-60Hz";
        }
    }

    g_ifbl_integer_wait_store_hook = safetyhook::create_mid(
        rva_ptr(kRvaIfblIntegerWaitStore),
        hook_ifbl_integer_wait_store);
    if (!g_ifbl_integer_wait_store_hook) {
        PLOG_ERROR << "GC120FPS: failed to midhook IFBL integer wait store";
    } else {
        PLOG_INFO << "GC120FPS: midhooked IFBL integer wait store";
    }

    g_ifbl_loop_counter_store_hook = safetyhook::create_mid(
        rva_ptr(kRvaIfblLoopCounterStore),
        hook_ifbl_loop_counter_store);
    if (!g_ifbl_loop_counter_store_hook) {
        PLOG_ERROR << "GC120FPS: failed to midhook IFBL loop counter store";
    } else {
        PLOG_INFO << "GC120FPS: midhooked IFBL loop counter store";
    }

    g_stage_bgm_preload_delay_hook = safetyhook::create_mid(
        rva_ptr(kRvaStageBgmPreloadDelayIncrement),
        hook_stage_bgm_preload_delay_increment);
    if (!g_stage_bgm_preload_delay_hook) {
        PLOG_ERROR << "GC120FPS: failed to midhook BGM preload delay increment";
    } else {
        PLOG_INFO << "GC120FPS: midhooked BGM preload delay increment";
    }

    g_tune_countdown_compare_hook = safetyhook::create_mid(
        rva_ptr(kRvaTuneCountdownCompare),
        hook_tune_countdown_compare);
    if (!g_tune_countdown_compare_hook) {
        PLOG_ERROR << "GC120FPS: failed to midhook Tune+0x1D14 countdown compare";
    } else {
        PLOG_INFO << "GC120FPS: midhooked Tune+0x1D14 countdown compare for 0xF0";
    }

    g_gameplay_audio_skip_margin_hook = safetyhook::create_mid(
        rva_ptr(kRvaGameplayAudioSkipMarginLoaded),
        hook_gameplay_audio_skip_margin_loaded);
    if (!g_gameplay_audio_skip_margin_hook) {
        PLOG_ERROR << "GC120FPS: failed to midhook gameplay audio SkipMargin clamp";
    } else {
        PLOG_INFO << "GC120FPS: midhooked gameplay audio SkipMargin clamp to >=48ms";
    }

    g_gameplay_audio_skip_interval_hook = safetyhook::create_mid(
        rva_ptr(kRvaGameplayAudioSkipIntervalIdiv),
        hook_gameplay_audio_skip_interval_idiv);
    if (!g_gameplay_audio_skip_interval_hook) {
        PLOG_ERROR << "GC120FPS: failed to midhook gameplay audio SkipInterval 120Hz conversion";
    } else {
        PLOG_INFO << "GC120FPS: midhooked gameplay audio SkipInterval 120Hz conversion";
    }

    g_gameplay_audio_resync_seek_hook = safetyhook::create_mid(
        rva_ptr(kRvaGameplayAudioResyncSeek),
        hook_gameplay_audio_resync_seek);
    if (!g_gameplay_audio_resync_seek_hook) {
        PLOG_ERROR << "GC120FPS: failed to midhook gameplay audio resync seek logger";
    } else {
        PLOG_INFO << "GC120FPS: midhooked gameplay audio resync seek logger";
    }
}

} // namespace

void FrameratePatchInit() {
    static std::atomic_bool initialized{false};
    bool expected = false;
    if (!initialized.compare_exchange_strong(expected, true)) {
        return;
    }

    PLOG_INFO << "GC120FPS: initializing framerate runtime config, exe base=" << reinterpret_cast<void*>(exe_base());
    auto& config = ConfigManager::instance();
    const bool enable_120fps_timer_patches = config.GetEnable120FpsTimerPatches();
    const bool enable_timer_freeze_patches = config.GetEnableTimerFreezePatches();

    PLOG_INFO << "GC120FPS: experimental enable_120fps_timer_patches="
              << enable_120fps_timer_patches
              << ", enable_timer_freeze_patches="
              << enable_timer_freeze_patches;

    if (enable_120fps_timer_patches) {
        install_constant_patches();
        install_safety_hooks();
    } else {
        PLOG_INFO << "GC120FPS: full-120 runtime patches disabled by config";
    }

    gc::timer_freeze::SetCountdownTimerFreezeEnabled(enable_timer_freeze_patches);
    gc::timer_freeze::CountdownTimerFreezeInit();
}

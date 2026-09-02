#pragma once

#include "Patches/RendererDeviceLoss/RendererResourceLifecycle.h"
#include "Patches/WindowedWidescreen/NativeCanvasCompositor.h"
#include "Patches/WindowedWidescreen/ResolutionModel.h"

#include <d3d9.h>
#include <wrl/client.h>

#include <cstdint>

namespace gc::windowed_widescreen
{
    inline constexpr std::size_t kRendererDeviceOffset = 0x08;
    inline constexpr std::size_t kRendererWindowOffset = 0x8C;

    enum class D3D9CompositorStage : std::uint8_t
    {
        none,
        invalid_renderer_owner,
        renderer_pointer_read,
        invalid_device,
        invalid_window,
        client_size,
        creation_parameters,
        device_caps,
        direct3d_owner,
        adapter_display_mode,
        backbuffer,
        backbuffer_description,
        backbuffer_size,
        backbuffer_multisample,
        texture_caps,
        render_target_format,
        scene_texture,
        scene_surface,
        native_texture,
        native_surface,
        depth_format,
        depth_surface,
        state_block,
    };

    struct D3D9CompositorFailure
    {
        D3D9CompositorStage stage{D3D9CompositorStage::none};
        HRESULT result{S_OK};
    };

    struct NativeBatchActions
    {
        void* context{};
        bool (*flush)(void*) noexcept{};
        bool (*empty)(void*) noexcept{};
    };

    class D3D9CompositorDevice final
    {
    public:
        explicit D3D9CompositorDevice(ResolutionModel resolution) noexcept;

        D3D9CompositorDevice(const D3D9CompositorDevice&) = delete;
        D3D9CompositorDevice& operator=(const D3D9CompositorDevice&) = delete;

        [[nodiscard]] bool Create(std::uintptr_t renderer_owner) noexcept;
        void Release() noexcept;

        void SetNativeBatchActions(NativeBatchActions actions) noexcept
        {
            native_batch_actions_ = actions;
        }

        [[nodiscard]] renderer_device_loss::RendererResourceParticipant
        ResourceParticipant() noexcept;

        [[nodiscard]] CompositorDeviceActions DeviceActions() noexcept;

        [[nodiscard]] D3D9CompositorFailure last_failure() const noexcept
        {
            return last_failure_;
        }

        [[nodiscard]] bool active() const noexcept
        {
            return active_;
        }

    private:
        enum class BoundTarget : std::uint8_t
        {
            none,
            wide_scene,
            native_canvas,
            real_backbuffer,
        };

        struct CopyVertex
        {
            float x{};
            float y{};
            float z{};
            float rhw{};
            float u{};
            float v{};
        };

        [[nodiscard]] bool Fail(
            D3D9CompositorStage stage,
            HRESULT result) noexcept;

        [[nodiscard]] bool CreateResources(
            std::uintptr_t renderer_owner) noexcept;

        [[nodiscard]] bool BindTarget(BoundTarget target) noexcept;
        [[nodiscard]] bool ConfigureCopyState() noexcept;
        [[nodiscard]] bool DrawCopy(
            IDirect3DTexture9* source,
            float source_left,
            float source_top,
            float source_right,
            float source_bottom,
            float destination_left,
            float destination_top,
            float destination_right,
            float destination_bottom) noexcept;

        [[nodiscard]] bool CaptureGameState() noexcept;
        [[nodiscard]] bool RestoreGameState() noexcept;
        [[nodiscard]] bool SetFullViewportAndScissor(
            RenderSpace space) noexcept;
        [[nodiscard]] bool SetGameplayHudViewport(
            GameplayHudPlacement placement) noexcept;
        [[nodiscard]] bool ApplyViewportAndScissor(
            const GameplayHudViewport& viewport,
            bool disable_depth) noexcept;
        [[nodiscard]] bool NativeDepthStateIsDisabled() noexcept;
        [[nodiscard]] bool AttemptRestoreAfterFailure(
            RenderSpace stable_space) noexcept;

        static bool CreateResource(
            void* context,
            std::uintptr_t renderer_owner) noexcept;
        static void ReleaseResource(void* context) noexcept;
        static bool BindWideScene(void* context) noexcept;
        static bool BindNativeCanvas(void* context) noexcept;
        static bool BindRealBackbuffer(void* context) noexcept;
        static bool CaptureState(void* context) noexcept;
        static bool RestoreState(void* context) noexcept;
        static bool DrawSceneCenterToNative(void* context) noexcept;
        static bool DrawNativeToSceneCenter(void* context) noexcept;
        static bool DrawSceneToBackbuffer(void* context) noexcept;
        static bool SetViewportAndScissor(
            void* context,
            RenderSpace space) noexcept;
        static bool SetGameplayHudViewportAction(
            void* context,
            GameplayHudPlacement placement) noexcept;
        static bool CheckNativeDepthState(void* context) noexcept;
        static bool FlushNativeBatches(void* context) noexcept;
        static bool NativeBatchesAreEmpty(void* context) noexcept;
        static bool RestoreAfterFailure(
            void* context,
            RenderSpace stable_space) noexcept;

        ResolutionModel resolution_;
        NativeBatchActions native_batch_actions_{};
        D3D9CompositorFailure last_failure_{};
        bool active_{};
        bool game_state_captured_{};
        BoundTarget bound_target_{BoundTarget::none};

        Microsoft::WRL::ComPtr<IDirect3DDevice9> device_;
        Microsoft::WRL::ComPtr<IDirect3D9> direct3d_;
        Microsoft::WRL::ComPtr<IDirect3DSurface9> real_backbuffer_;
        Microsoft::WRL::ComPtr<IDirect3DTexture9> scene_texture_;
        Microsoft::WRL::ComPtr<IDirect3DSurface9> scene_surface_;
        Microsoft::WRL::ComPtr<IDirect3DTexture9> native_texture_;
        Microsoft::WRL::ComPtr<IDirect3DSurface9> native_surface_;
        Microsoft::WRL::ComPtr<IDirect3DSurface9> scene_depth_surface_;
        Microsoft::WRL::ComPtr<IDirect3DStateBlock9> copy_state_block_;
        Microsoft::WRL::ComPtr<IDirect3DSurface9> saved_render_target_;
        Microsoft::WRL::ComPtr<IDirect3DSurface9> saved_depth_surface_;
    };
} // namespace gc::windowed_widescreen

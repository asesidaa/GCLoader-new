#include "Patches/WindowedWidescreen/D3D9CompositorDevice.h"

#include <Windows.h>

#include <array>
#include <limits>

namespace gc::windowed_widescreen
{
    namespace
    {
        constexpr DWORD kCopyFvf = D3DFVF_XYZRHW | D3DFVF_TEX1;
        constexpr DWORD kAllColorChannels =
            D3DCOLORWRITEENABLE_RED |
            D3DCOLORWRITEENABLE_GREEN |
            D3DCOLORWRITEENABLE_BLUE |
            D3DCOLORWRITEENABLE_ALPHA;

        [[nodiscard]] bool ReadRendererPointers(
            const std::uintptr_t renderer_owner,
            IDirect3DDevice9*& device,
            HWND& window) noexcept
        {
            device = nullptr;
            window = nullptr;
            if (renderer_owner == 0 ||
                renderer_owner >
                    std::numeric_limits<std::uintptr_t>::max() -
                        kRendererWindowOffset)
            {
                return false;
            }

            __try
            {
                device = *reinterpret_cast<IDirect3DDevice9* const*>(
                    renderer_owner + kRendererDeviceOffset);
                window = *reinterpret_cast<HWND const*>(
                    renderer_owner + kRendererWindowOffset);
                return device != nullptr && window != nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                device = nullptr;
                window = nullptr;
                return false;
            }
        }

        [[nodiscard]] bool ExactClientSize(
            const HWND window,
            const OutputSize expected) noexcept
        {
            RECT client{};
            if (window == nullptr || !IsWindow(window) ||
                !GetClientRect(window, &client))
            {
                return false;
            }
            const auto width =
                static_cast<std::int64_t>(client.right) - client.left;
            const auto height =
                static_cast<std::int64_t>(client.bottom) - client.top;
            return width == expected.width && height == expected.height;
        }

        [[nodiscard]] bool IsDepthFormatSupported(
            IDirect3D9* direct3d,
            const D3DDEVICE_CREATION_PARAMETERS& creation,
            const D3DFORMAT adapter_format,
            const D3DFORMAT backbuffer_format,
            const D3DFORMAT depth_format) noexcept
        {
            return SUCCEEDED(direct3d->CheckDeviceFormat(
                       creation.AdapterOrdinal,
                       creation.DeviceType,
                       adapter_format,
                       D3DUSAGE_DEPTHSTENCIL,
                       D3DRTYPE_SURFACE,
                       depth_format)) &&
                SUCCEEDED(direct3d->CheckDepthStencilMatch(
                    creation.AdapterOrdinal,
                    creation.DeviceType,
                    adapter_format,
                    backbuffer_format,
                    depth_format));
        }
    } // namespace

    D3D9CompositorDevice::D3D9CompositorDevice(
        ResolutionModel resolution) noexcept
        : resolution_{resolution}
    {
    }

    bool D3D9CompositorDevice::Fail(
        const D3D9CompositorStage stage,
        const HRESULT result) noexcept
    {
        last_failure_ = {
            .stage = stage,
            .result = result,
        };
        Release();
        return false;
    }

    bool D3D9CompositorDevice::Create(
        const std::uintptr_t renderer_owner) noexcept
    {
        Release();
        last_failure_ = {};
        return CreateResources(renderer_owner);
    }

    bool D3D9CompositorDevice::CreateResources(
        const std::uintptr_t renderer_owner) noexcept
    {
        if (renderer_owner == 0)
        {
            return Fail(
                D3D9CompositorStage::invalid_renderer_owner,
                E_INVALIDARG);
        }

        IDirect3DDevice9* raw_device = nullptr;
        HWND window = nullptr;
        if (!ReadRendererPointers(renderer_owner, raw_device, window))
        {
            return Fail(
                D3D9CompositorStage::renderer_pointer_read,
                E_POINTER);
        }
        device_ = raw_device;
        if (!device_)
        {
            return Fail(D3D9CompositorStage::invalid_device, E_POINTER);
        }
        if (!IsWindow(window))
        {
            return Fail(D3D9CompositorStage::invalid_window, E_HANDLE);
        }
        if (!ExactClientSize(window, resolution_.output_size()))
        {
            return Fail(D3D9CompositorStage::client_size, E_INVALIDARG);
        }

        D3DDEVICE_CREATION_PARAMETERS creation{};
        auto result = device_->GetCreationParameters(&creation);
        if (FAILED(result))
        {
            return Fail(D3D9CompositorStage::creation_parameters, result);
        }

        D3DCAPS9 caps{};
        result = device_->GetDeviceCaps(&caps);
        if (FAILED(result))
        {
            return Fail(D3D9CompositorStage::device_caps, result);
        }

        result = device_->GetDirect3D(direct3d_.ReleaseAndGetAddressOf());
        if (FAILED(result) || !direct3d_)
        {
            return Fail(D3D9CompositorStage::direct3d_owner, result);
        }

        D3DDISPLAYMODE display_mode{};
        result = direct3d_->GetAdapterDisplayMode(
            creation.AdapterOrdinal,
            &display_mode);
        if (FAILED(result))
        {
            return Fail(D3D9CompositorStage::adapter_display_mode, result);
        }

        result = device_->GetBackBuffer(
            0,
            0,
            D3DBACKBUFFER_TYPE_MONO,
            real_backbuffer_.ReleaseAndGetAddressOf());
        if (FAILED(result) || !real_backbuffer_)
        {
            return Fail(D3D9CompositorStage::backbuffer, result);
        }

        D3DSURFACE_DESC backbuffer_description{};
        result = real_backbuffer_->GetDesc(&backbuffer_description);
        if (FAILED(result))
        {
            return Fail(
                D3D9CompositorStage::backbuffer_description,
                result);
        }

        const auto output = resolution_.output_size();
        if (backbuffer_description.Width != output.width ||
            backbuffer_description.Height != output.height)
        {
            return Fail(
                D3D9CompositorStage::backbuffer_size,
                E_INVALIDARG);
        }
        if (backbuffer_description.MultiSampleType != D3DMULTISAMPLE_NONE)
        {
            return Fail(
                D3D9CompositorStage::backbuffer_multisample,
                D3DERR_NOTAVAILABLE);
        }
        if (caps.MaxTextureWidth < output.width ||
            caps.MaxTextureHeight < output.height ||
            caps.MaxTextureWidth < kNativeWidth ||
            caps.MaxTextureHeight < kNativeHeight)
        {
            return Fail(
                D3D9CompositorStage::texture_caps,
                D3DERR_NOTAVAILABLE);
        }

        result = direct3d_->CheckDeviceFormat(
            creation.AdapterOrdinal,
            creation.DeviceType,
            display_mode.Format,
            D3DUSAGE_RENDERTARGET,
            D3DRTYPE_TEXTURE,
            backbuffer_description.Format);
        if (FAILED(result))
        {
            return Fail(D3D9CompositorStage::render_target_format, result);
        }

        result = device_->CreateTexture(
            output.width,
            output.height,
            1,
            D3DUSAGE_RENDERTARGET,
            backbuffer_description.Format,
            D3DPOOL_DEFAULT,
            scene_texture_.ReleaseAndGetAddressOf(),
            nullptr);
        if (FAILED(result) || !scene_texture_)
        {
            return Fail(D3D9CompositorStage::scene_texture, result);
        }
        result = scene_texture_->GetSurfaceLevel(
            0,
            scene_surface_.ReleaseAndGetAddressOf());
        if (FAILED(result) || !scene_surface_)
        {
            return Fail(D3D9CompositorStage::scene_surface, result);
        }

        result = device_->CreateTexture(
            kNativeWidth,
            kNativeHeight,
            1,
            D3DUSAGE_RENDERTARGET,
            backbuffer_description.Format,
            D3DPOOL_DEFAULT,
            native_texture_.ReleaseAndGetAddressOf(),
            nullptr);
        if (FAILED(result) || !native_texture_)
        {
            return Fail(D3D9CompositorStage::native_texture, result);
        }
        result = native_texture_->GetSurfaceLevel(
            0,
            native_surface_.ReleaseAndGetAddressOf());
        if (FAILED(result) || !native_surface_)
        {
            return Fail(D3D9CompositorStage::native_surface, result);
        }

        D3DFORMAT preferred_depth = D3DFMT_UNKNOWN;
        Microsoft::WRL::ComPtr<IDirect3DSurface9> current_depth;
        result = device_->GetDepthStencilSurface(
            current_depth.ReleaseAndGetAddressOf());
        if (SUCCEEDED(result) && current_depth)
        {
            D3DSURFACE_DESC depth_description{};
            if (SUCCEEDED(current_depth->GetDesc(&depth_description)))
            {
                preferred_depth = depth_description.Format;
            }
        }
        else if (result != D3DERR_NOTFOUND)
        {
            return Fail(D3D9CompositorStage::depth_format, result);
        }

        const std::array depth_candidates{
            preferred_depth,
            D3DFMT_D24S8,
            D3DFMT_D24X8,
            D3DFMT_D16,
        };
        D3DFORMAT selected_depth = D3DFMT_UNKNOWN;
        for (const auto candidate : depth_candidates)
        {
            if (candidate != D3DFMT_UNKNOWN &&
                IsDepthFormatSupported(
                    direct3d_.Get(),
                    creation,
                    display_mode.Format,
                    backbuffer_description.Format,
                    candidate))
            {
                selected_depth = candidate;
                break;
            }
        }
        if (selected_depth == D3DFMT_UNKNOWN)
        {
            return Fail(
                D3D9CompositorStage::depth_format,
                D3DERR_NOTAVAILABLE);
        }

        result = device_->CreateDepthStencilSurface(
            output.width,
            output.height,
            selected_depth,
            D3DMULTISAMPLE_NONE,
            0,
            TRUE,
            scene_depth_surface_.ReleaseAndGetAddressOf(),
            nullptr);
        if (FAILED(result) || !scene_depth_surface_)
        {
            return Fail(D3D9CompositorStage::depth_surface, result);
        }

        result = device_->CreateStateBlock(
            D3DSBT_ALL,
            copy_state_block_.ReleaseAndGetAddressOf());
        if (FAILED(result) || !copy_state_block_)
        {
            return Fail(D3D9CompositorStage::state_block, result);
        }

        active_ = true;
        return true;
    }

    void D3D9CompositorDevice::Release() noexcept
    {
        game_state_captured_ = false;
        bound_target_ = BoundTarget::none;
        active_ = false;
        saved_depth_surface_.Reset();
        saved_render_target_.Reset();
        copy_state_block_.Reset();
        scene_depth_surface_.Reset();
        native_surface_.Reset();
        native_texture_.Reset();
        scene_surface_.Reset();
        scene_texture_.Reset();
        real_backbuffer_.Reset();
        direct3d_.Reset();
        device_.Reset();
    }

    renderer_device_loss::RendererResourceParticipant
    D3D9CompositorDevice::ResourceParticipant() noexcept
    {
        return {
            .context = this,
            .create = &CreateResource,
            .release = &ReleaseResource,
        };
    }

    CompositorDeviceActions D3D9CompositorDevice::DeviceActions() noexcept
    {
        return {
            .context = this,
            .bind_wide_scene = &BindWideScene,
            .bind_native_canvas = &BindNativeCanvas,
            .bind_real_backbuffer = &BindRealBackbuffer,
            .capture_game_state = &CaptureState,
            .restore_game_state = &RestoreState,
            .draw_scene_center_to_native = &DrawSceneCenterToNative,
            .draw_native_to_scene_center = &DrawNativeToSceneCenter,
            .draw_scene_to_backbuffer = &DrawSceneToBackbuffer,
            .set_full_viewport_and_scissor = &SetViewportAndScissor,
            .native_depth_state_is_disabled = &CheckNativeDepthState,
            .flush_native_batches = &FlushNativeBatches,
            .native_batches_are_empty = &NativeBatchesAreEmpty,
            .attempt_restore_after_failure = &RestoreAfterFailure,
        };
    }

    bool D3D9CompositorDevice::BindTarget(const BoundTarget target) noexcept
    {
        if (!active_ || !device_)
        {
            return false;
        }

        IDirect3DSurface9* render_target = nullptr;
        IDirect3DSurface9* depth_target = nullptr;
        switch (target)
        {
        case BoundTarget::wide_scene:
            render_target = scene_surface_.Get();
            depth_target = scene_depth_surface_.Get();
            break;
        case BoundTarget::native_canvas:
            render_target = native_surface_.Get();
            break;
        case BoundTarget::real_backbuffer:
            render_target = real_backbuffer_.Get();
            break;
        case BoundTarget::none:
            return false;
        }
        if (render_target == nullptr)
        {
            return false;
        }

        if (FAILED(device_->SetTexture(0, nullptr)) ||
            FAILED(device_->SetRenderTarget(0, render_target)) ||
            FAILED(device_->SetDepthStencilSurface(depth_target)))
        {
            return false;
        }
        bound_target_ = target;
        return true;
    }

    bool D3D9CompositorDevice::CaptureGameState() noexcept
    {
        if (!active_ || game_state_captured_ || !copy_state_block_)
        {
            return false;
        }

        saved_render_target_.Reset();
        saved_depth_surface_.Reset();
        auto result = device_->GetRenderTarget(
            0,
            saved_render_target_.ReleaseAndGetAddressOf());
        if (FAILED(result) || !saved_render_target_)
        {
            return false;
        }
        result = device_->GetDepthStencilSurface(
            saved_depth_surface_.ReleaseAndGetAddressOf());
        if (FAILED(result) && result != D3DERR_NOTFOUND)
        {
            saved_render_target_.Reset();
            return false;
        }
        result = copy_state_block_->Capture();
        if (FAILED(result))
        {
            saved_depth_surface_.Reset();
            saved_render_target_.Reset();
            return false;
        }
        game_state_captured_ = true;
        return true;
    }

    bool D3D9CompositorDevice::RestoreGameState() noexcept
    {
        if (!active_ || !game_state_captured_ || !copy_state_block_)
        {
            return false;
        }

        const auto intended_target = bound_target_;
        // Bind first: BindTarget must clear sampler 0 before a texture can
        // become a render target. Applying the captured block last restores
        // that sampler (and the rest of the game's pipeline state) instead of
        // immediately clearing it again.
        if (!BindTarget(intended_target) ||
            FAILED(copy_state_block_->Apply()))
        {
            return false;
        }

        saved_depth_surface_.Reset();
        saved_render_target_.Reset();
        game_state_captured_ = false;
        return true;
    }

    bool D3D9CompositorDevice::ConfigureCopyState() noexcept
    {
        if (!active_ || !device_)
        {
            return false;
        }

        if (FAILED(device_->SetVertexShader(nullptr)) ||
            FAILED(device_->SetPixelShader(nullptr)) ||
            FAILED(device_->SetFVF(kCopyFvf)) ||
            FAILED(device_->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID)) ||
            FAILED(device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE)) ||
            FAILED(device_->SetRenderState(D3DRS_ZENABLE, FALSE)) ||
            FAILED(device_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE)) ||
            FAILED(device_->SetRenderState(D3DRS_STENCILENABLE, FALSE)) ||
            FAILED(device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE)) ||
            FAILED(device_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE)) ||
            FAILED(device_->SetRenderState(D3DRS_FOGENABLE, FALSE)) ||
            FAILED(device_->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE)) ||
            FAILED(device_->SetRenderState(D3DRS_CLIPPLANEENABLE, 0)) ||
            FAILED(device_->SetRenderState(D3DRS_WRAP0, 0)) ||
            FAILED(device_->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE)) ||
            FAILED(device_->SetRenderState(
                D3DRS_COLORWRITEENABLE,
                kAllColorChannels)) ||
            FAILED(device_->SetSamplerState(
                0, D3DSAMP_MINFILTER, D3DTEXF_POINT)) ||
            FAILED(device_->SetSamplerState(
                0, D3DSAMP_MAGFILTER, D3DTEXF_POINT)) ||
            FAILED(device_->SetSamplerState(
                0, D3DSAMP_MIPFILTER, D3DTEXF_NONE)) ||
            FAILED(device_->SetSamplerState(
                0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP)) ||
            FAILED(device_->SetSamplerState(
                0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP)) ||
            FAILED(device_->SetSamplerState(
                0, D3DSAMP_SRGBTEXTURE, FALSE)) ||
            FAILED(device_->SetTextureStageState(
                0, D3DTSS_COLOROP, D3DTOP_SELECTARG1)) ||
            FAILED(device_->SetTextureStageState(
                0, D3DTSS_COLORARG1, D3DTA_TEXTURE)) ||
            FAILED(device_->SetTextureStageState(
                0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1)) ||
            FAILED(device_->SetTextureStageState(
                0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE)) ||
            FAILED(device_->SetTextureStageState(
                0, D3DTSS_RESULTARG, D3DTA_CURRENT)) ||
            FAILED(device_->SetTextureStageState(
                0, D3DTSS_TEXCOORDINDEX, 0)) ||
            FAILED(device_->SetTextureStageState(
                0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE)) ||
            FAILED(device_->SetTextureStageState(
                1, D3DTSS_COLOROP, D3DTOP_DISABLE)) ||
            FAILED(device_->SetTextureStageState(
                1, D3DTSS_ALPHAOP, D3DTOP_DISABLE)))
        {
            return false;
        }

        for (DWORD stage = 1; stage < 8; ++stage)
        {
            if (FAILED(device_->SetTexture(stage, nullptr)))
            {
                return false;
            }
        }
        return true;
    }

    bool D3D9CompositorDevice::DrawCopy(
        IDirect3DTexture9* const source,
        const float source_left,
        const float source_top,
        const float source_right,
        const float source_bottom,
        const float destination_left,
        const float destination_top,
        const float destination_right,
        const float destination_bottom) noexcept
    {
        if (source == nullptr || !ConfigureCopyState())
        {
            return false;
        }

        const std::array vertices{
            CopyVertex{
                .x = destination_left - 0.5F,
                .y = destination_top - 0.5F,
                .z = 0.0F,
                .rhw = 1.0F,
                .u = source_left,
                .v = source_top,
            },
            CopyVertex{
                .x = destination_right - 0.5F,
                .y = destination_top - 0.5F,
                .z = 0.0F,
                .rhw = 1.0F,
                .u = source_right,
                .v = source_top,
            },
            CopyVertex{
                .x = destination_left - 0.5F,
                .y = destination_bottom - 0.5F,
                .z = 0.0F,
                .rhw = 1.0F,
                .u = source_left,
                .v = source_bottom,
            },
            CopyVertex{
                .x = destination_right - 0.5F,
                .y = destination_bottom - 0.5F,
                .z = 0.0F,
                .rhw = 1.0F,
                .u = source_right,
                .v = source_bottom,
            },
        };

        if (FAILED(device_->SetTexture(0, source)))
        {
            return false;
        }
        const auto result = device_->DrawPrimitiveUP(
            D3DPT_TRIANGLESTRIP,
            2,
            vertices.data(),
            sizeof(CopyVertex));
        const auto unbound = device_->SetTexture(0, nullptr);
        return SUCCEEDED(result) && SUCCEEDED(unbound);
    }

    bool D3D9CompositorDevice::SetFullViewportAndScissor(
        const RenderSpace space) noexcept
    {
        if (!active_ || !device_ || space == RenderSpace::compositor)
        {
            return false;
        }
        const auto dimensions = SelectRenderDimensions(
            space,
            resolution_.output_size());
        if (!dimensions)
        {
            return false;
        }

        const D3DVIEWPORT9 viewport{
            .X = 0,
            .Y = 0,
            .Width = dimensions->width,
            .Height = dimensions->height,
            .MinZ = 0.0F,
            .MaxZ = 1.0F,
        };
        const RECT scissor{
            .left = 0,
            .top = 0,
            .right = static_cast<LONG>(dimensions->width),
            .bottom = static_cast<LONG>(dimensions->height),
        };
        if (FAILED(device_->SetViewport(&viewport)) ||
            FAILED(device_->SetScissorRect(&scissor)))
        {
            return false;
        }
        if (space == RenderSpace::native_2d)
        {
            return SUCCEEDED(
                       device_->SetRenderState(D3DRS_ZENABLE, FALSE)) &&
                SUCCEEDED(device_->SetRenderState(
                    D3DRS_ZWRITEENABLE,
                    FALSE)) &&
                SUCCEEDED(device_->SetRenderState(
                    D3DRS_STENCILENABLE,
                    FALSE));
        }
        return true;
    }

    bool D3D9CompositorDevice::NativeDepthStateIsDisabled() noexcept
    {
        DWORD z_enabled = TRUE;
        DWORD z_write_enabled = TRUE;
        DWORD stencil_enabled = TRUE;
        return active_ && device_ &&
            SUCCEEDED(device_->GetRenderState(D3DRS_ZENABLE, &z_enabled)) &&
            SUCCEEDED(device_->GetRenderState(
                D3DRS_ZWRITEENABLE,
                &z_write_enabled)) &&
            SUCCEEDED(device_->GetRenderState(
                D3DRS_STENCILENABLE,
                &stencil_enabled)) &&
            z_enabled == FALSE && z_write_enabled == FALSE &&
            stencil_enabled == FALSE;
    }

    bool D3D9CompositorDevice::AttemptRestoreAfterFailure(
        const RenderSpace stable_space) noexcept
    {
        if (!active_ || !device_ || stable_space == RenderSpace::compositor)
        {
            return false;
        }

        bool restored = true;
        if (game_state_captured_ && copy_state_block_)
        {
            restored = SUCCEEDED(copy_state_block_->Apply());
            if (saved_render_target_)
            {
                restored =
                    SUCCEEDED(device_->SetRenderTarget(
                        0,
                        saved_render_target_.Get())) && restored;
                restored =
                    SUCCEEDED(device_->SetDepthStencilSurface(
                        saved_depth_surface_.Get())) && restored;
            }
        }
        else
        {
            restored = BindTarget(
                stable_space == RenderSpace::native_2d
                    ? BoundTarget::native_canvas
                    : BoundTarget::wide_scene);
        }

        bound_target_ =
            stable_space == RenderSpace::native_2d
            ? BoundTarget::native_canvas
            : BoundTarget::wide_scene;
        restored = SetFullViewportAndScissor(stable_space) && restored;
        saved_depth_surface_.Reset();
        saved_render_target_.Reset();
        game_state_captured_ = false;
        return restored;
    }

    bool D3D9CompositorDevice::CreateResource(
        void* context,
        const std::uintptr_t renderer_owner) noexcept
    {
        return context != nullptr &&
            static_cast<D3D9CompositorDevice*>(context)->Create(
                renderer_owner);
    }

    void D3D9CompositorDevice::ReleaseResource(void* context) noexcept
    {
        if (context != nullptr)
        {
            static_cast<D3D9CompositorDevice*>(context)->Release();
        }
    }

    bool D3D9CompositorDevice::BindWideScene(void* context) noexcept
    {
        return context != nullptr &&
            static_cast<D3D9CompositorDevice*>(context)->BindTarget(
                BoundTarget::wide_scene);
    }

    bool D3D9CompositorDevice::BindNativeCanvas(void* context) noexcept
    {
        return context != nullptr &&
            static_cast<D3D9CompositorDevice*>(context)->BindTarget(
                BoundTarget::native_canvas);
    }

    bool D3D9CompositorDevice::BindRealBackbuffer(void* context) noexcept
    {
        return context != nullptr &&
            static_cast<D3D9CompositorDevice*>(context)->BindTarget(
                BoundTarget::real_backbuffer);
    }

    bool D3D9CompositorDevice::CaptureState(void* context) noexcept
    {
        return context != nullptr &&
            static_cast<D3D9CompositorDevice*>(context)->CaptureGameState();
    }

    bool D3D9CompositorDevice::RestoreState(void* context) noexcept
    {
        return context != nullptr &&
            static_cast<D3D9CompositorDevice*>(context)->RestoreGameState();
    }

    bool D3D9CompositorDevice::DrawSceneCenterToNative(
        void* context) noexcept
    {
        if (context == nullptr)
        {
            return false;
        }
        auto& self = *static_cast<D3D9CompositorDevice*>(context);
        const auto output = self.resolution_.output_size();
        const auto native = self.resolution_.native_rect();
        return self.DrawCopy(
            self.scene_texture_.Get(),
            static_cast<float>(native.left) / output.width,
            static_cast<float>(native.top) / output.height,
            static_cast<float>(native.right) / output.width,
            static_cast<float>(native.bottom) / output.height,
            0.0F,
            0.0F,
            static_cast<float>(kNativeWidth),
            static_cast<float>(kNativeHeight));
    }

    bool D3D9CompositorDevice::DrawNativeToSceneCenter(
        void* context) noexcept
    {
        if (context == nullptr)
        {
            return false;
        }
        auto& self = *static_cast<D3D9CompositorDevice*>(context);
        const auto native = self.resolution_.native_rect();
        return self.DrawCopy(
            self.native_texture_.Get(),
            0.0F,
            0.0F,
            1.0F,
            1.0F,
            static_cast<float>(native.left),
            static_cast<float>(native.top),
            static_cast<float>(native.right),
            static_cast<float>(native.bottom));
    }

    bool D3D9CompositorDevice::DrawSceneToBackbuffer(
        void* context) noexcept
    {
        if (context == nullptr)
        {
            return false;
        }
        auto& self = *static_cast<D3D9CompositorDevice*>(context);
        const auto output = self.resolution_.output_size();
        return self.DrawCopy(
            self.scene_texture_.Get(),
            0.0F,
            0.0F,
            1.0F,
            1.0F,
            0.0F,
            0.0F,
            static_cast<float>(output.width),
            static_cast<float>(output.height));
    }

    bool D3D9CompositorDevice::SetViewportAndScissor(
        void* context,
        const RenderSpace space) noexcept
    {
        return context != nullptr &&
            static_cast<D3D9CompositorDevice*>(context)
                ->SetFullViewportAndScissor(space);
    }

    bool D3D9CompositorDevice::CheckNativeDepthState(
        void* context) noexcept
    {
        return context != nullptr &&
            static_cast<D3D9CompositorDevice*>(context)
                ->NativeDepthStateIsDisabled();
    }

    bool D3D9CompositorDevice::FlushNativeBatches(void* context) noexcept
    {
        if (context == nullptr)
        {
            return false;
        }
        const auto actions =
            static_cast<D3D9CompositorDevice*>(context)->native_batch_actions_;
        return actions.flush != nullptr && actions.flush(actions.context);
    }

    bool D3D9CompositorDevice::NativeBatchesAreEmpty(void* context) noexcept
    {
        if (context == nullptr)
        {
            return false;
        }
        const auto actions =
            static_cast<D3D9CompositorDevice*>(context)->native_batch_actions_;
        return actions.empty != nullptr && actions.empty(actions.context);
    }

    bool D3D9CompositorDevice::RestoreAfterFailure(
        void* context,
        const RenderSpace stable_space) noexcept
    {
        return context != nullptr &&
            static_cast<D3D9CompositorDevice*>(context)
                ->AttemptRestoreAfterFailure(stable_space);
    }
} // namespace gc::windowed_widescreen

#pragma once

#include "vr/runtime/frame_prepare_context.hpp"
#include "vr/runtime/runtime_ingress_ids.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace vr::render {
class UploadHost;
class BindlessResourceSystem;
class DescriptorHost;
class FrameComposerHost;
class IblHost;
class IblBakeHost;
class PipelineHost;
class RenderTargetHost;
class SkyEnvironmentGpuHost;
} // namespace vr::render

namespace vr::asset {
class TextureHost;
}

namespace vr::resource {
class GpuMemoryHost;
class SamplerHost;
} // namespace vr::resource

namespace vr::text {
class FreeTypeHost;
class GlyphAtlasHost;
class GlyphUploadHost;
} // namespace vr::text

namespace vr::particle {
class ParticleUploadHost;
class ParticleSimulationHost;
} // namespace vr::particle

namespace vr::render {

struct FrameComposerPrepareView final {
    VulkanContext& device;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    resource::SamplerHost& sampler;
    RenderTargetHost& render_target;
    BindlessResourceSystem* bindless = nullptr;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct SceneRecorder2DPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost* gpu_memory = nullptr;
    asset::TextureHost* texture = nullptr;
    BindlessResourceSystem* bindless = nullptr;
    UploadHost* upload = nullptr;
    DescriptorHost* descriptor = nullptr;
    FrameComposerHost* frame_composer = nullptr;
    IblHost* ibl = nullptr;
    IblBakeHost* ibl_bake = nullptr;
    PipelineHost* pipeline = nullptr;
    RenderTargetHost& render_target;
    resource::SamplerHost* sampler = nullptr;
    text::FreeTypeHost* freetype = nullptr;
    text::GlyphAtlasHost* glyph_atlas = nullptr;
    text::GlyphUploadHost* glyph_upload = nullptr;
    particle::ParticleUploadHost* particle_upload = nullptr;
    particle::ParticleSimulationHost* particle_simulation = nullptr;
    bool render_graph_upload_active = false;
    bool render_graph_compute_active = false;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct SceneRecorder3DPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost* gpu_memory = nullptr;
    asset::TextureHost* texture = nullptr;
    BindlessResourceSystem* bindless = nullptr;
    UploadHost* upload = nullptr;
    DescriptorHost* descriptor = nullptr;
    FrameComposerHost* frame_composer = nullptr;
    IblHost* ibl = nullptr;
    IblBakeHost* ibl_bake = nullptr;
    SkyEnvironmentGpuHost* sky_environment = nullptr;
    PipelineHost* pipeline = nullptr;
    RenderTargetHost& render_target;
    resource::SamplerHost* sampler = nullptr;
    text::FreeTypeHost* freetype = nullptr;
    text::GlyphAtlasHost* glyph_atlas = nullptr;
    text::GlyphUploadHost* glyph_upload = nullptr;
    particle::ParticleUploadHost* particle_upload = nullptr;
    particle::ParticleSimulationHost* particle_simulation = nullptr;
    IblEnvironmentId ibl_environment_id{};
    asset::TextureId ibl_brdf_lut_texture_id{};
    bool render_graph_upload_active = false;
    bool render_graph_compute_active = false;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct IblHostPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost& gpu_memory;
    UploadHost& upload;
    DescriptorHost& descriptor;
    BindlessResourceSystem* bindless = nullptr;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct IblBakeHostPrepareView final {
    VulkanContext& device;
    UploadHost& upload;
    IblHost* ibl = nullptr;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct IblBakeCoordinatorPrepareView final {
    VulkanContext& device;
    UploadHost& upload;
    IblBakeHost& ibl_bake;
    IblHost* ibl = nullptr;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct BackgroundPass2DPrepareView final {
    VulkanContext& device;
    PipelineHost& pipeline;
    RenderTargetHost& render_target;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct SkyEnvironmentGpuPrepareView final {
    VulkanContext& device;
    asset::TextureHost& texture;
    UploadHost& upload;
    DescriptorHost& descriptor;
    resource::SamplerHost& sampler;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct SkyEnvironmentPassPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost* gpu_memory = nullptr;
    asset::TextureHost* texture = nullptr;
    BindlessResourceSystem* bindless = nullptr;
    UploadHost* upload = nullptr;
    DescriptorHost* descriptor = nullptr;
    IblHost* ibl = nullptr;
    PipelineHost& pipeline;
    RenderTargetHost& render_target;
    resource::SamplerHost* sampler = nullptr;
    IblEnvironmentId ibl_environment_id{};
    asset::TextureId ibl_brdf_lut_texture_id{};
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct RenderTargetCompositeRendererPrepareView final {
    VulkanContext& device;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    RenderTargetHost& render_target;
    resource::SamplerHost& sampler;
    BindlessResourceSystem* bindless = nullptr;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

namespace detail {

template<typename T>
[[nodiscard]] inline T& RequirePrepareService(T* pointer_,
                                              std::string_view field_name_,
                                              std::string_view view_name_) {
    if (pointer_ == nullptr) {
        throw std::runtime_error(std::string(view_name_) +
                                 " requires prepare service " +
                                 std::string(field_name_));
    }
    return *pointer_;
}

} // namespace detail

[[nodiscard]] inline RenderTargetCompositeRendererPrepareView MakeRenderTargetCompositeRendererPrepareView(
    const FrameComposerPrepareView& prepare_view_) noexcept {
    return {
        .device = prepare_view_.device,
        .descriptor = prepare_view_.descriptor,
        .pipeline = prepare_view_.pipeline,
        .render_target = prepare_view_.render_target,
        .sampler = prepare_view_.sampler,
        .bindless = prepare_view_.bindless,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline RenderTargetCompositeRendererPrepareView MakeRenderTargetCompositeRendererPrepareView(
    const SceneRecorder2DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "RenderTargetCompositeRendererPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "RenderTargetCompositeRendererPrepareView"),
        .render_target = prepare_view_.render_target,
        .sampler = detail::RequirePrepareService(prepare_view_.sampler,
                                                 "sampler",
                                                 "RenderTargetCompositeRendererPrepareView"),
        .bindless = prepare_view_.bindless,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline RenderTargetCompositeRendererPrepareView MakeRenderTargetCompositeRendererPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "RenderTargetCompositeRendererPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "RenderTargetCompositeRendererPrepareView"),
        .render_target = prepare_view_.render_target,
        .sampler = detail::RequirePrepareService(prepare_view_.sampler,
                                                 "sampler",
                                                 "RenderTargetCompositeRendererPrepareView"),
        .bindless = prepare_view_.bindless,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline BackgroundPass2DPrepareView MakeBackgroundPass2DPrepareView(
    const SceneRecorder2DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "BackgroundPass2DPrepareView"),
        .render_target = prepare_view_.render_target,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline SkyEnvironmentGpuPrepareView MakeSkyEnvironmentGpuPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .texture = detail::RequirePrepareService(prepare_view_.texture,
                                                 "texture",
                                                 "SkyEnvironmentGpuPrepareView"),
        .upload = detail::RequirePrepareService(prepare_view_.upload,
                                                "upload",
                                                "SkyEnvironmentGpuPrepareView"),
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "SkyEnvironmentGpuPrepareView"),
        .sampler = detail::RequirePrepareService(prepare_view_.sampler,
                                                 "sampler",
                                                 "SkyEnvironmentGpuPrepareView"),
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline SkyEnvironmentPassPrepareView MakeSkyEnvironmentPassPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = prepare_view_.gpu_memory,
        .texture = prepare_view_.texture,
        .bindless = prepare_view_.bindless,
        .upload = prepare_view_.upload,
        .descriptor = prepare_view_.descriptor,
        .ibl = prepare_view_.ibl,
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "SkyEnvironmentPassPrepareView"),
        .render_target = prepare_view_.render_target,
        .sampler = prepare_view_.sampler,
        .ibl_environment_id = prepare_view_.ibl_environment_id,
        .ibl_brdf_lut_texture_id = prepare_view_.ibl_brdf_lut_texture_id,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline IblHostPrepareView MakeIblHostPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = detail::RequirePrepareService(prepare_view_.gpu_memory,
                                                    "gpu_memory",
                                                    "IblHostPrepareView"),
        .upload = detail::RequirePrepareService(prepare_view_.upload,
                                                "upload",
                                                "IblHostPrepareView"),
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "IblHostPrepareView"),
        .bindless = prepare_view_.bindless,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline IblBakeHostPrepareView MakeIblBakeHostPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .upload = detail::RequirePrepareService(prepare_view_.upload,
                                                "upload",
                                                "IblBakeHostPrepareView"),
        .ibl = prepare_view_.ibl,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline IblBakeCoordinatorPrepareView MakeIblBakeCoordinatorPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .upload = detail::RequirePrepareService(prepare_view_.upload,
                                                "upload",
                                                "IblBakeCoordinatorPrepareView"),
        .ibl_bake = detail::RequirePrepareService(prepare_view_.ibl_bake,
                                                  "ibl_bake",
                                                  "IblBakeCoordinatorPrepareView"),
        .ibl = prepare_view_.ibl,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline IblBakeHostPrepareView MakeIblBakeHostPrepareView(
    const IblBakeCoordinatorPrepareView& prepare_view_) noexcept {
    return {
        .device = prepare_view_.device,
        .upload = prepare_view_.upload,
        .ibl = prepare_view_.ibl,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline IblHostPrepareView MakeIblHostPrepareView(
    const SkyEnvironmentPassPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = detail::RequirePrepareService(prepare_view_.gpu_memory,
                                                    "gpu_memory",
                                                    "IblHostPrepareView"),
        .upload = detail::RequirePrepareService(prepare_view_.upload,
                                                "upload",
                                                "IblHostPrepareView"),
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "IblHostPrepareView"),
        .bindless = prepare_view_.bindless,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}


} // namespace vr::render

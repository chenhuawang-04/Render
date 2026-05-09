#pragma once

#include "vr/vulkan_context.hpp"

#include <stdexcept>
#include <string_view>

namespace vr::render {
class UploadHost;
class DescriptorHost;
class FrameComposerHost;
class IblHost;
class IblBakeHost;
class PipelineHost;
class RenderTargetHost;
class RenderTargetPool;
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

struct FrameStaticContext final {
    std::uint32_t frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
};

struct FrameGpuProgressContext final {
    std::uint64_t last_submitted_value = 0U;
    std::uint64_t completed_submit_value = 0U;
};

struct SceneRenderTargetSetPrepareView final {
    VulkanContext& device;
    RenderTargetHost& render_target;
    RenderTargetPool* render_target_pool = nullptr;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct FrameComposerPrepareView final {
    VulkanContext& device;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    resource::SamplerHost& sampler;
    RenderTargetHost& render_target;
    RenderTargetPool* render_target_pool = nullptr;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct SceneRecorder2DPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost* gpu_memory = nullptr;
    asset::TextureHost* texture = nullptr;
    UploadHost* upload = nullptr;
    DescriptorHost* descriptor = nullptr;
    FrameComposerHost* frame_composer = nullptr;
    IblHost* ibl = nullptr;
    IblBakeHost* ibl_bake = nullptr;
    PipelineHost* pipeline = nullptr;
    RenderTargetHost& render_target;
    RenderTargetPool* render_target_pool = nullptr;
    resource::SamplerHost* sampler = nullptr;
    text::FreeTypeHost* freetype = nullptr;
    text::GlyphAtlasHost* glyph_atlas = nullptr;
    text::GlyphUploadHost* glyph_upload = nullptr;
    particle::ParticleUploadHost* particle_upload = nullptr;
    particle::ParticleSimulationHost* particle_simulation = nullptr;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct SceneRecorder3DPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost* gpu_memory = nullptr;
    asset::TextureHost* texture = nullptr;
    UploadHost* upload = nullptr;
    DescriptorHost* descriptor = nullptr;
    FrameComposerHost* frame_composer = nullptr;
    IblHost* ibl = nullptr;
    IblBakeHost* ibl_bake = nullptr;
    PipelineHost* pipeline = nullptr;
    RenderTargetHost& render_target;
    RenderTargetPool* render_target_pool = nullptr;
    resource::SamplerHost* sampler = nullptr;
    text::FreeTypeHost* freetype = nullptr;
    text::GlyphAtlasHost* glyph_atlas = nullptr;
    text::GlyphUploadHost* glyph_upload = nullptr;
    particle::ParticleUploadHost* particle_upload = nullptr;
    particle::ParticleSimulationHost* particle_simulation = nullptr;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct IblHostPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost& gpu_memory;
    UploadHost& upload;
    DescriptorHost& descriptor;
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

struct TextRenderer2DPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost& gpu_memory;
    UploadHost& upload;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    text::FreeTypeHost& freetype;
    text::GlyphAtlasHost& glyph_atlas;
    text::GlyphUploadHost& glyph_upload;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct TextRenderer3DPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost& gpu_memory;
    UploadHost& upload;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    text::FreeTypeHost& freetype;
    text::GlyphAtlasHost& glyph_atlas;
    text::GlyphUploadHost& glyph_upload;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct GeometryRenderer2DPrepareView final {
    VulkanContext& device;
    UploadHost& upload;
    PipelineHost& pipeline;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct SurfaceRenderer2DPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost& gpu_memory;
    UploadHost& upload;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    resource::SamplerHost& sampler;
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

struct RenderTargetCompositeRendererPrepareView final {
    VulkanContext& device;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    RenderTargetHost& render_target;
    resource::SamplerHost& sampler;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct ShadowRenderer2DPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost& gpu_memory;
    PipelineHost& pipeline;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct RenderTargetBloomRendererPrepareView final {
    VulkanContext& device;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    RenderTargetHost& render_target;
    RenderTargetPool& render_target_pool;
    resource::SamplerHost& sampler;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct SceneBloomPostStackPrepareView final {
    VulkanContext& device;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    RenderTargetHost& render_target;
    RenderTargetPool* render_target_pool = nullptr;
    resource::SamplerHost& sampler;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct GeometryRenderer3DPrepareView final {
    VulkanContext& device;
    UploadHost& upload;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    resource::GpuMemoryHost& gpu_memory;
    IblHost& ibl;
    resource::SamplerHost& sampler;
    RenderTargetHost* render_target = nullptr;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct SurfaceRenderer3DPrepareView final {
    VulkanContext& device;
    UploadHost& upload;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    resource::GpuMemoryHost& gpu_memory;
    IblHost& ibl;
    resource::SamplerHost& sampler;
    RenderTargetHost* render_target = nullptr;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct ShadowRenderer3DPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost& gpu_memory;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct SkyboxRendererPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost& gpu_memory;
    UploadHost& upload;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    IblHost& ibl;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct ParticleRenderer2DPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost& gpu_memory;
    UploadHost& upload;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    resource::SamplerHost& sampler;
    asset::TextureHost* texture = nullptr;
    particle::ParticleUploadHost* particle_upload = nullptr;
    particle::ParticleSimulationHost* particle_simulation = nullptr;
    RenderTargetHost* render_target = nullptr;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

struct ParticleRenderer3DPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost& gpu_memory;
    UploadHost& upload;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    resource::SamplerHost& sampler;
    asset::TextureHost* texture = nullptr;
    particle::ParticleUploadHost* particle_upload = nullptr;
    particle::ParticleSimulationHost* particle_simulation = nullptr;
    RenderTargetHost* render_target = nullptr;
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

[[nodiscard]] inline SceneRenderTargetSetPrepareView MakeSceneRenderTargetSetPrepareView(
    const FrameComposerPrepareView& prepare_view_) noexcept {
    return {
        .device = prepare_view_.device,
        .render_target = prepare_view_.render_target,
        .render_target_pool = prepare_view_.render_target_pool,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline RenderTargetCompositeRendererPrepareView MakeRenderTargetCompositeRendererPrepareView(
    const FrameComposerPrepareView& prepare_view_) noexcept {
    return {
        .device = prepare_view_.device,
        .descriptor = prepare_view_.descriptor,
        .pipeline = prepare_view_.pipeline,
        .render_target = prepare_view_.render_target,
        .sampler = prepare_view_.sampler,
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

[[nodiscard]] inline GeometryRenderer2DPrepareView MakeGeometryRenderer2DPrepareView(
    const SceneRecorder2DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .upload = detail::RequirePrepareService(prepare_view_.upload, "upload", "GeometryRenderer2DPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline, "pipeline", "GeometryRenderer2DPrepareView"),
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline ShadowRenderer2DPrepareView MakeShadowRenderer2DPrepareView(
    const SceneRecorder2DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = detail::RequirePrepareService(prepare_view_.gpu_memory,
                                                    "gpu_memory",
                                                    "ShadowRenderer2DPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "ShadowRenderer2DPrepareView"),
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline SurfaceRenderer2DPrepareView MakeSurfaceRenderer2DPrepareView(
    const SceneRecorder2DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = detail::RequirePrepareService(prepare_view_.gpu_memory,
                                                    "gpu_memory",
                                                    "SurfaceRenderer2DPrepareView"),
        .upload = detail::RequirePrepareService(prepare_view_.upload, "upload", "SurfaceRenderer2DPrepareView"),
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "SurfaceRenderer2DPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "SurfaceRenderer2DPrepareView"),
        .sampler = detail::RequirePrepareService(prepare_view_.sampler, "sampler", "SurfaceRenderer2DPrepareView"),
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline TextRenderer2DPrepareView MakeTextRenderer2DPrepareView(
    const SceneRecorder2DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = detail::RequirePrepareService(prepare_view_.gpu_memory,
                                                    "gpu_memory",
                                                    "TextRenderer2DPrepareView"),
        .upload = detail::RequirePrepareService(prepare_view_.upload, "upload", "TextRenderer2DPrepareView"),
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "TextRenderer2DPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "TextRenderer2DPrepareView"),
        .freetype = detail::RequirePrepareService(prepare_view_.freetype,
                                                  "freetype",
                                                  "TextRenderer2DPrepareView"),
        .glyph_atlas = detail::RequirePrepareService(prepare_view_.glyph_atlas,
                                                     "glyph_atlas",
                                                     "TextRenderer2DPrepareView"),
        .glyph_upload = detail::RequirePrepareService(prepare_view_.glyph_upload,
                                                      "glyph_upload",
                                                      "TextRenderer2DPrepareView"),
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline ParticleRenderer2DPrepareView MakeParticleRenderer2DPrepareView(
    const SceneRecorder2DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = detail::RequirePrepareService(prepare_view_.gpu_memory,
                                                    "gpu_memory",
                                                    "ParticleRenderer2DPrepareView"),
        .upload = detail::RequirePrepareService(prepare_view_.upload,
                                                "upload",
                                                "ParticleRenderer2DPrepareView"),
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "ParticleRenderer2DPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "ParticleRenderer2DPrepareView"),
        .sampler = detail::RequirePrepareService(prepare_view_.sampler,
                                                 "sampler",
                                                 "ParticleRenderer2DPrepareView"),
        .texture = prepare_view_.texture,
        .particle_upload = prepare_view_.particle_upload,
        .particle_simulation = prepare_view_.particle_simulation,
        .render_target = &prepare_view_.render_target,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline SceneBloomPostStackPrepareView MakeSceneBloomPostStackPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "SceneBloomPostStackPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "SceneBloomPostStackPrepareView"),
        .render_target = prepare_view_.render_target,
        .render_target_pool = prepare_view_.render_target_pool,
        .sampler = detail::RequirePrepareService(prepare_view_.sampler,
                                                 "sampler",
                                                 "SceneBloomPostStackPrepareView"),
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline ShadowRenderer3DPrepareView MakeShadowRenderer3DPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = detail::RequirePrepareService(prepare_view_.gpu_memory,
                                                    "gpu_memory",
                                                    "ShadowRenderer3DPrepareView"),
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "ShadowRenderer3DPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "ShadowRenderer3DPrepareView"),
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline TextRenderer3DPrepareView MakeTextRenderer3DPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = detail::RequirePrepareService(prepare_view_.gpu_memory,
                                                    "gpu_memory",
                                                    "TextRenderer3DPrepareView"),
        .upload = detail::RequirePrepareService(prepare_view_.upload, "upload", "TextRenderer3DPrepareView"),
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "TextRenderer3DPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "TextRenderer3DPrepareView"),
        .freetype = detail::RequirePrepareService(prepare_view_.freetype,
                                                  "freetype",
                                                  "TextRenderer3DPrepareView"),
        .glyph_atlas = detail::RequirePrepareService(prepare_view_.glyph_atlas,
                                                     "glyph_atlas",
                                                     "TextRenderer3DPrepareView"),
        .glyph_upload = detail::RequirePrepareService(prepare_view_.glyph_upload,
                                                      "glyph_upload",
                                                      "TextRenderer3DPrepareView"),
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline SkyboxRendererPrepareView MakeSkyboxRendererPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = detail::RequirePrepareService(prepare_view_.gpu_memory,
                                                    "gpu_memory",
                                                    "SkyboxRendererPrepareView"),
        .upload = detail::RequirePrepareService(prepare_view_.upload,
                                                "upload",
                                                "SkyboxRendererPrepareView"),
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "SkyboxRendererPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "SkyboxRendererPrepareView"),
        .ibl = detail::RequirePrepareService(prepare_view_.ibl,
                                             "ibl",
                                             "SkyboxRendererPrepareView"),
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline RenderTargetBloomRendererPrepareView MakeRenderTargetBloomRendererPrepareView(
    const SceneBloomPostStackPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .descriptor = prepare_view_.descriptor,
        .pipeline = prepare_view_.pipeline,
        .render_target = prepare_view_.render_target,
        .render_target_pool = detail::RequirePrepareService(prepare_view_.render_target_pool,
                                                            "render_target_pool",
                                                            "RenderTargetBloomRendererPrepareView"),
        .sampler = prepare_view_.sampler,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline IblHostPrepareView MakeIblHostPrepareView(
    const GeometryRenderer3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = prepare_view_.gpu_memory,
        .upload = prepare_view_.upload,
        .descriptor = prepare_view_.descriptor,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline IblHostPrepareView MakeIblHostPrepareView(
    const SurfaceRenderer3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = prepare_view_.gpu_memory,
        .upload = prepare_view_.upload,
        .descriptor = prepare_view_.descriptor,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline IblHostPrepareView MakeIblHostPrepareView(
    const SkyboxRendererPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = prepare_view_.gpu_memory,
        .upload = prepare_view_.upload,
        .descriptor = prepare_view_.descriptor,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline GeometryRenderer3DPrepareView MakeGeometryRenderer3DPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .upload = detail::RequirePrepareService(prepare_view_.upload,
                                                "upload",
                                                "GeometryRenderer3DPrepareView"),
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "GeometryRenderer3DPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "GeometryRenderer3DPrepareView"),
        .gpu_memory = detail::RequirePrepareService(prepare_view_.gpu_memory,
                                                    "gpu_memory",
                                                    "GeometryRenderer3DPrepareView"),
        .ibl = detail::RequirePrepareService(prepare_view_.ibl,
                                             "ibl",
                                             "GeometryRenderer3DPrepareView"),
        .sampler = detail::RequirePrepareService(prepare_view_.sampler,
                                                 "sampler",
                                                 "GeometryRenderer3DPrepareView"),
        .render_target = &prepare_view_.render_target,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline SurfaceRenderer3DPrepareView MakeSurfaceRenderer3DPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .upload = detail::RequirePrepareService(prepare_view_.upload,
                                                "upload",
                                                "SurfaceRenderer3DPrepareView"),
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "SurfaceRenderer3DPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "SurfaceRenderer3DPrepareView"),
        .gpu_memory = detail::RequirePrepareService(prepare_view_.gpu_memory,
                                                    "gpu_memory",
                                                    "SurfaceRenderer3DPrepareView"),
        .ibl = detail::RequirePrepareService(prepare_view_.ibl,
                                             "ibl",
                                             "SurfaceRenderer3DPrepareView"),
        .sampler = detail::RequirePrepareService(prepare_view_.sampler,
                                                 "sampler",
                                                 "SurfaceRenderer3DPrepareView"),
        .render_target = &prepare_view_.render_target,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline ParticleRenderer3DPrepareView MakeParticleRenderer3DPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .gpu_memory = detail::RequirePrepareService(prepare_view_.gpu_memory,
                                                    "gpu_memory",
                                                    "ParticleRenderer3DPrepareView"),
        .upload = detail::RequirePrepareService(prepare_view_.upload,
                                                "upload",
                                                "ParticleRenderer3DPrepareView"),
        .descriptor = detail::RequirePrepareService(prepare_view_.descriptor,
                                                    "descriptor",
                                                    "ParticleRenderer3DPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline,
                                                  "pipeline",
                                                  "ParticleRenderer3DPrepareView"),
        .sampler = detail::RequirePrepareService(prepare_view_.sampler,
                                                 "sampler",
                                                 "ParticleRenderer3DPrepareView"),
        .texture = prepare_view_.texture,
        .particle_upload = prepare_view_.particle_upload,
        .particle_simulation = prepare_view_.particle_simulation,
        .render_target = &prepare_view_.render_target,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

} // namespace vr::render

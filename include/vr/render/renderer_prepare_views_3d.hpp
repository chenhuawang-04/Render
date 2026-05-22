#pragma once

#include "vr/render/scene_prepare_views.hpp"

namespace vr::render {

struct TextRenderer3DPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost& gpu_memory;
    UploadHost& upload;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    BindlessResourceSystem* bindless = nullptr;
    text::FreeTypeHost& freetype;
    text::GlyphAtlasHost& glyph_atlas;
    text::GlyphUploadHost& glyph_upload;
    bool render_graph_upload_active = false;
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
    asset::TextureHost* texture = nullptr;
    BindlessResourceSystem* bindless = nullptr;
    RenderTargetHost* render_target = nullptr;
    std::uint32_t ibl_environment_id = 0U;
    std::uint32_t ibl_brdf_lut_texture_id = 0U;
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
    asset::TextureHost* texture = nullptr;
    BindlessResourceSystem* bindless = nullptr;
    RenderTargetHost* render_target = nullptr;
    std::uint32_t ibl_environment_id = 0U;
    std::uint32_t ibl_brdf_lut_texture_id = 0U;
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

struct ParticleRenderer3DPrepareView final {
    VulkanContext& device;
    resource::GpuMemoryHost& gpu_memory;
    UploadHost& upload;
    DescriptorHost& descriptor;
    PipelineHost& pipeline;
    resource::SamplerHost& sampler;
    asset::TextureHost* texture = nullptr;
    BindlessResourceSystem* bindless = nullptr;
    particle::ParticleUploadHost* particle_upload = nullptr;
    particle::ParticleSimulationHost* particle_simulation = nullptr;
    RenderTargetHost* render_target = nullptr;
    bool render_graph_compute_active = false;
    FrameStaticContext frame{};
    FrameGpuProgressContext progress{};
};

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
        .bindless = prepare_view_.bindless,
        .freetype = detail::RequirePrepareService(prepare_view_.freetype,
                                                  "freetype",
                                                  "TextRenderer3DPrepareView"),
        .glyph_atlas = detail::RequirePrepareService(prepare_view_.glyph_atlas,
                                                     "glyph_atlas",
                                                     "TextRenderer3DPrepareView"),
        .glyph_upload = detail::RequirePrepareService(prepare_view_.glyph_upload,
                                                      "glyph_upload",
                                                      "TextRenderer3DPrepareView"),
        .render_graph_upload_active = prepare_view_.render_graph_upload_active,
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
        .bindless = prepare_view_.bindless,
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
        .bindless = prepare_view_.bindless,
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
        .texture = prepare_view_.texture,
        .bindless = prepare_view_.bindless,
        .render_target = &prepare_view_.render_target,
        .ibl_environment_id = prepare_view_.ibl_environment_id,
        .ibl_brdf_lut_texture_id = prepare_view_.ibl_brdf_lut_texture_id,
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
        .texture = prepare_view_.texture,
        .bindless = prepare_view_.bindless,
        .render_target = &prepare_view_.render_target,
        .ibl_environment_id = prepare_view_.ibl_environment_id,
        .ibl_brdf_lut_texture_id = prepare_view_.ibl_brdf_lut_texture_id,
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
        .bindless = prepare_view_.bindless,
        .particle_upload = prepare_view_.particle_upload,
        .particle_simulation = prepare_view_.particle_simulation,
        .render_target = &prepare_view_.render_target,
        .render_graph_compute_active = prepare_view_.render_graph_compute_active,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

} // namespace vr::render

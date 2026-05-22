#pragma once

#include "vr/render/scene_prepare_views.hpp"

namespace vr::render {

struct TextRenderer2DPrepareView final {
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
    BindlessResourceSystem* bindless = nullptr;
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

struct ParticleRenderer2DPrepareView final {
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

[[nodiscard]] inline GeometryRenderer2DPrepareView MakeGeometryRenderer2DPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
    return {
        .device = prepare_view_.device,
        .upload = detail::RequirePrepareService(prepare_view_.upload, "upload", "GeometryRenderer2DPrepareView"),
        .pipeline = detail::RequirePrepareService(prepare_view_.pipeline, "pipeline", "GeometryRenderer2DPrepareView"),
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline SurfaceRenderer2DPrepareView MakeSurfaceRenderer2DPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
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
        .bindless = prepare_view_.bindless,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline TextRenderer2DPrepareView MakeTextRenderer2DPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
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
        .bindless = prepare_view_.bindless,
        .freetype = detail::RequirePrepareService(prepare_view_.freetype,
                                                  "freetype",
                                                  "TextRenderer2DPrepareView"),
        .glyph_atlas = detail::RequirePrepareService(prepare_view_.glyph_atlas,
                                                     "glyph_atlas",
                                                     "TextRenderer2DPrepareView"),
        .glyph_upload = detail::RequirePrepareService(prepare_view_.glyph_upload,
                                                      "glyph_upload",
                                                      "TextRenderer2DPrepareView"),
        .render_graph_upload_active = prepare_view_.render_graph_upload_active,
        .frame = prepare_view_.frame,
        .progress = prepare_view_.progress,
    };
}

[[nodiscard]] inline ParticleRenderer2DPrepareView MakeParticleRenderer2DPrepareView(
    const SceneRecorder3DPrepareView& prepare_view_) {
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
        .bindless = prepare_view_.bindless,
        .particle_upload = prepare_view_.particle_upload,
        .particle_simulation = prepare_view_.particle_simulation,
        .render_target = &prepare_view_.render_target,
        .render_graph_compute_active = prepare_view_.render_graph_compute_active,
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
        .bindless = prepare_view_.bindless,
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
        .bindless = prepare_view_.bindless,
        .freetype = detail::RequirePrepareService(prepare_view_.freetype,
                                                  "freetype",
                                                  "TextRenderer2DPrepareView"),
        .glyph_atlas = detail::RequirePrepareService(prepare_view_.glyph_atlas,
                                                     "glyph_atlas",
                                                     "TextRenderer2DPrepareView"),
        .glyph_upload = detail::RequirePrepareService(prepare_view_.glyph_upload,
                                                      "glyph_upload",
                                                      "TextRenderer2DPrepareView"),
        .render_graph_upload_active = prepare_view_.render_graph_upload_active,
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

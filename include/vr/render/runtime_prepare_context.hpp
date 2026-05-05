#pragma once

#include "vr/vulkan_context.hpp"

#include <cstdint>

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

namespace vr::render {

struct RuntimePrepareContext {
    VulkanContext* context = nullptr;
    std::uint32_t frame_index = 0U;
    std::uint64_t last_submitted_value = 0U;
    std::uint64_t completed_submit_value = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    asset::TextureHost* texture_host = nullptr;
    UploadHost* upload_host = nullptr;
    DescriptorHost* descriptor_host = nullptr;
    FrameComposerHost* frame_composer_host = nullptr;
    IblHost* ibl_host = nullptr;
    IblBakeHost* ibl_bake_host = nullptr;
    PipelineHost* pipeline_host = nullptr;
    RenderTargetHost* render_target_host = nullptr;
    RenderTargetPool* render_target_pool = nullptr;
    resource::SamplerHost* sampler_host = nullptr;
    text::FreeTypeHost* freetype_host = nullptr;
    text::GlyphAtlasHost* glyph_atlas_host = nullptr;
    text::GlyphUploadHost* glyph_upload_host = nullptr;
};

} // namespace vr::render

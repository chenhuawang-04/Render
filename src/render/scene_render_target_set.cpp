#include "vr/render/scene_render_target_set.hpp"

#include <stdexcept>

namespace vr::render {

namespace {

[[nodiscard]] RenderTargetDesc BuildColorDesc(const SceneRenderTargetSetCreateInfo& create_info_,
                                              VkFormat color_format_) {
    RenderTargetDesc desc{};
    desc.debug_name = create_info_.color_debug_name;
    desc.dimension = RenderTargetDimension::image_2d;
    desc.lifetime = RenderTargetLifetime::persistent;
    desc.scale_mode = create_info_.scale_mode;
    desc.width = create_info_.width;
    desc.height = create_info_.height;
    desc.depth = create_info_.depth;
    desc.width_scale = create_info_.width_scale;
    desc.height_scale = create_info_.height_scale;
    desc.format = color_format_;
    desc.samples = create_info_.samples;
    desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT |
                 create_info_.additional_color_usage;
    desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    desc.color_encoding = create_info_.color_encoding;
    return desc;
}

[[nodiscard]] RenderTargetDesc BuildDepthDesc(const SceneRenderTargetSetCreateInfo& create_info_,
                                              VkFormat depth_format_) {
    RenderTargetDesc desc{};
    desc.debug_name = create_info_.depth_debug_name;
    desc.dimension = RenderTargetDimension::image_2d;
    desc.lifetime = RenderTargetLifetime::persistent;
    desc.scale_mode = create_info_.scale_mode;
    desc.width = create_info_.width;
    desc.height = create_info_.height;
    desc.depth = create_info_.depth;
    desc.width_scale = create_info_.width_scale;
    desc.height_scale = create_info_.height_scale;
    desc.format = depth_format_;
    desc.samples = create_info_.samples;
    desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 create_info_.additional_depth_usage;
    desc.aspect = DepthStencilAspectMask(depth_format_);
    return desc;
}

} // namespace

void SceneRenderTargetSet::Initialize(const SceneRenderTargetSetCreateInfo& create_info_) noexcept {
    create_info_cache = create_info_;
    color_target = {};
    depth_target = {};
    color_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;
    initialized = true;
}

void SceneRenderTargetSet::Reset() noexcept {
    create_info_cache = {};
    color_target = {};
    depth_target = {};
    color_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;
    initialized = false;
}

void SceneRenderTargetSet::EnsureForSwapchain(VulkanContext& context_,
                                              RenderTargetHost& render_target_host_,
                                              VkExtent2D swapchain_extent_,
                                              std::uint64_t last_submitted_value_,
                                              std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("SceneRenderTargetSet::EnsureForSwapchain called before Initialize");
    }

    if (color_format == VK_FORMAT_UNDEFINED) {
        color_format = ResolveColorFormat(context_, create_info_cache.color_format);
    }

    const auto color_result = render_target_host_.EnsurePersistentTarget(context_,
                                                                         color_target,
                                                                         BuildColorDesc(create_info_cache, color_format),
                                                                         swapchain_extent_,
                                                                         last_submitted_value_,
                                                                         completed_submit_value_);
    color_target = color_result.handle;

    if (!create_info_cache.enable_depth) {
        depth_target = {};
        depth_format = VK_FORMAT_UNDEFINED;
        return;
    }

    if (depth_format == VK_FORMAT_UNDEFINED) {
        depth_format = ResolveDepthFormat(context_, create_info_cache.depth_format);
    }

    const auto depth_result = render_target_host_.EnsurePersistentTarget(context_,
                                                                         depth_target,
                                                                         BuildDepthDesc(create_info_cache, depth_format),
                                                                         swapchain_extent_,
                                                                         last_submitted_value_,
                                                                         completed_submit_value_);
    depth_target = depth_result.handle;
}

RenderTargetColorOutputConfig SceneRenderTargetSet::BuildColorOutputConfig(bool clear_target_,
                                                                           bool final_pass_) const {
    if (!initialized || !IsValidRenderTargetHandle(color_target)) {
        throw std::runtime_error("SceneRenderTargetSet::BuildColorOutputConfig requires ready color target");
    }

    RenderTargetColorOutputConfig output{};
    output.color_target = color_target;
    output.final_state = final_pass_
        ? create_info_cache.color_final_state
        : RenderTargetStateKind::color_attachment;
    output.use_explicit_load_op = true;
    output.load_op = clear_target_
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    output.store_op = VK_ATTACHMENT_STORE_OP_STORE;
    output.clear_color = create_info_cache.clear_color;
    return output;
}

RenderTargetDepthOutputConfig SceneRenderTargetSet::BuildDepthOutputConfig(bool clear_target_) const {
    if (!initialized || !create_info_cache.enable_depth || !IsValidRenderTargetHandle(depth_target)) {
        throw std::runtime_error("SceneRenderTargetSet::BuildDepthOutputConfig requires ready depth target");
    }

    RenderTargetDepthOutputConfig output{};
    output.depth_target = depth_target;
    output.final_state = create_info_cache.depth_final_state;
    output.use_explicit_load_op = true;
    output.load_op = clear_target_
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    output.store_op = VK_ATTACHMENT_STORE_OP_STORE;
    output.clear_depth_stencil = VkClearDepthStencilValue{
        create_info_cache.clear_depth_value,
        create_info_cache.clear_stencil_value
    };
    return output;
}

void SceneRenderTargetSet::ConfigureCompositeRenderer(RenderTargetCompositeRenderer& composite_renderer_) const noexcept {
    if (!initialized || !IsValidRenderTargetHandle(color_target)) {
        composite_renderer_.ClearSourceTarget();
        composite_renderer_.ResetOutputTargetConfig();
        return;
    }
    composite_renderer_.SetSourceTarget(color_target, create_info_cache.color_final_state);
    composite_renderer_.ResetOutputTargetConfig();
}

void SceneRenderTargetSet::ResetCompositeRenderer(RenderTargetCompositeRenderer& composite_renderer_) const noexcept {
    composite_renderer_.ClearSourceTarget();
    composite_renderer_.ResetOutputTargetConfig();
}

bool SceneRenderTargetSet::IsReady() const noexcept {
    return initialized &&
           IsValidRenderTargetHandle(color_target) &&
           (!create_info_cache.enable_depth || IsValidRenderTargetHandle(depth_target));
}

bool SceneRenderTargetSet::HasDepthTarget() const noexcept {
    return create_info_cache.enable_depth && IsValidRenderTargetHandle(depth_target);
}

RenderTargetHandle SceneRenderTargetSet::ColorTarget() const noexcept {
    return color_target;
}

RenderTargetHandle SceneRenderTargetSet::DepthTarget() const noexcept {
    return depth_target;
}

VkFormat SceneRenderTargetSet::ColorFormat() const noexcept {
    return color_format;
}

VkFormat SceneRenderTargetSet::DepthFormat() const noexcept {
    return depth_format;
}

const SceneRenderTargetSetCreateInfo& SceneRenderTargetSet::CreateInfo() const noexcept {
    return create_info_cache;
}

VkFormat SceneRenderTargetSet::ResolveColorFormat(VulkanContext& context_,
                                                  VkFormat requested_format_) {
    if (requested_format_ != VK_FORMAT_UNDEFINED) {
        if (!IsColorAttachmentSampledFormatSupported(context_, requested_format_)) {
            throw std::runtime_error("SceneRenderTargetSet requested color format is unsupported");
        }
        return requested_format_;
    }

    constexpr std::array<VkFormat, 4U> defaults{
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_FORMAT_R16G16B16A16_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM
    };
    return ResolveFirstSupportedColorAttachmentSampledFormat(context_, defaults);
}

VkFormat SceneRenderTargetSet::ResolveDepthFormat(VulkanContext& context_,
                                                  VkFormat requested_format_) {
    if (requested_format_ != VK_FORMAT_UNDEFINED) {
        if (!IsDepthStencilAttachmentFormatSupported(context_, requested_format_)) {
            throw std::runtime_error("SceneRenderTargetSet requested depth format is unsupported");
        }
        return requested_format_;
    }

    constexpr std::array<VkFormat, 3U> defaults{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D32_SFLOAT_S8_UINT
    };
    return ResolveFirstSupportedDepthStencilFormat(context_, defaults);
}

} // namespace vr::render

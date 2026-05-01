#include "vr/render/scene_render_target_set.hpp"

#include <string>
#include <stdexcept>

namespace vr::render {

namespace {

void ValidateSceneLifetime(RenderTargetLifetime lifetime_,
                           const char* label_) {
    if (lifetime_ == RenderTargetLifetime::persistent ||
        lifetime_ == RenderTargetLifetime::transient) {
        return;
    }
    throw std::invalid_argument(std::string("SceneRenderTargetSet only supports persistent/transient ") +
                                label_ +
                                " targets in v1");
}

[[nodiscard]] RenderTargetDesc BuildColorDesc(const SceneRenderTargetSetCreateInfo& create_info_,
                                              VkFormat color_format_) {
    RenderTargetDesc desc{};
    desc.debug_name = create_info_.color_debug_name;
    desc.dimension = RenderTargetDimension::image_2d;
    desc.lifetime = create_info_.color_lifetime;
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
    desc.lifetime = create_info_.depth_lifetime;
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
    cached_color_outputs = {};
    cached_depth_outputs = {};
    frame_ready = false;
    initialized = true;
}

void SceneRenderTargetSet::Reset() noexcept {
    create_info_cache = {};
    color_target = {};
    depth_target = {};
    color_format = VK_FORMAT_UNDEFINED;
    depth_format = VK_FORMAT_UNDEFINED;
    cached_color_outputs = {};
    cached_depth_outputs = {};
    frame_ready = false;
    initialized = false;
}

bool SceneRenderTargetSet::EnsureForSwapchain(VulkanContext& context_,
                                              RenderTargetHost& render_target_host_,
                                              VkExtent2D swapchain_extent_,
                                              std::uint64_t last_submitted_value_,
                                              std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("SceneRenderTargetSet::EnsureForSwapchain called before Initialize");
    }
    if (SupportsSwapchainRelativeExtent() && !HasNonZeroExtent(swapchain_extent_)) {
        InvalidateCurrentFrameTargets();
        return false;
    }

    ValidateSceneLifetime(create_info_cache.color_lifetime, "color");

    if (color_format == VK_FORMAT_UNDEFINED) {
        color_format = ResolveColorFormat(context_, create_info_cache.color_format);
    }

    const RenderTargetDesc color_desc = BuildColorDesc(create_info_cache, color_format);
    if (color_desc.lifetime == RenderTargetLifetime::persistent) {
        const auto color_result = render_target_host_.EnsurePersistentTarget(context_,
                                                                             color_target,
                                                                             color_desc,
                                                                             swapchain_extent_,
                                                                             last_submitted_value_,
                                                                             completed_submit_value_);
        color_target = color_result.handle;
    } else {
        color_target = {};
    }

    if (!create_info_cache.enable_depth) {
        depth_target = {};
        depth_format = VK_FORMAT_UNDEFINED;
        frame_ready = IsValidRenderTargetHandle(color_target);
        if (frame_ready) {
            RebuildCachedOutputConfigs();
        } else {
            cached_color_outputs = {};
            cached_depth_outputs = {};
        }
        return frame_ready;
    }

    ValidateSceneLifetime(create_info_cache.depth_lifetime, "depth");

    if (depth_format == VK_FORMAT_UNDEFINED) {
        depth_format = ResolveDepthFormat(context_, create_info_cache.depth_format);
    }

    const RenderTargetDesc depth_desc = BuildDepthDesc(create_info_cache, depth_format);
    if (depth_desc.lifetime == RenderTargetLifetime::persistent) {
        const auto depth_result = render_target_host_.EnsurePersistentTarget(context_,
                                                                             depth_target,
                                                                             depth_desc,
                                                                             swapchain_extent_,
                                                                             last_submitted_value_,
                                                                             completed_submit_value_);
        depth_target = depth_result.handle;
    } else {
        depth_target = {};
    }

    frame_ready = IsValidRenderTargetHandle(color_target) &&
                  (!create_info_cache.enable_depth || IsValidRenderTargetHandle(depth_target));
    if (frame_ready) {
        RebuildCachedOutputConfigs();
    } else {
        cached_color_outputs = {};
        cached_depth_outputs = {};
    }
    return frame_ready;
}

bool SceneRenderTargetSet::PrepareFrame(const RuntimePrepareContext& prepare_context_) {
    if (!initialized) {
        throw std::runtime_error("SceneRenderTargetSet::PrepareFrame called before Initialize");
    }
    if (prepare_context_.context == nullptr || prepare_context_.render_target_host == nullptr) {
        throw std::runtime_error("SceneRenderTargetSet::PrepareFrame requires render target runtime context");
    }
    if (SupportsSwapchainRelativeExtent() && !HasNonZeroExtent(prepare_context_.swapchain_extent)) {
        InvalidateCurrentFrameTargets();
        return false;
    }

    (void)EnsureForSwapchain(*prepare_context_.context,
                             *prepare_context_.render_target_host,
                             prepare_context_.swapchain_extent,
                             prepare_context_.last_submitted_value,
                             prepare_context_.completed_submit_value);

    if (prepare_context_.render_target_pool != nullptr) {
        AcquireTransientTargets(*prepare_context_.context,
                                *prepare_context_.render_target_host,
                                *prepare_context_.render_target_pool,
                                prepare_context_.swapchain_extent);
    } else {
        if (create_info_cache.color_lifetime == RenderTargetLifetime::transient ||
            (create_info_cache.enable_depth &&
             create_info_cache.depth_lifetime == RenderTargetLifetime::transient)) {
            throw std::runtime_error("SceneRenderTargetSet transient targets require RenderTargetPool");
        }
    }
    frame_ready = IsValidRenderTargetHandle(color_target) &&
                  (!create_info_cache.enable_depth || IsValidRenderTargetHandle(depth_target));
    if (frame_ready) {
        RebuildCachedOutputConfigs();
    } else {
        cached_color_outputs = {};
        cached_depth_outputs = {};
    }
    return frame_ready;
}

bool SceneRenderTargetSet::OnSwapchainRecreated(VulkanContext& context_,
                                                RenderTargetHost& render_target_host_,
                                                RenderTargetPool* render_target_pool_,
                                                VkExtent2D swapchain_extent_,
                                                std::uint64_t last_submitted_value_,
                                                std::uint64_t completed_submit_value_) {
    if (SupportsSwapchainRelativeExtent() && !HasNonZeroExtent(swapchain_extent_)) {
        InvalidateCurrentFrameTargets();
        return false;
    }
    (void)EnsureForSwapchain(context_,
                             render_target_host_,
                             swapchain_extent_,
                             last_submitted_value_,
                             completed_submit_value_);
    if (render_target_pool_ != nullptr) {
        AcquireTransientTargets(context_,
                                render_target_host_,
                                *render_target_pool_,
                                swapchain_extent_);
    } else if (create_info_cache.color_lifetime == RenderTargetLifetime::transient ||
               (create_info_cache.enable_depth &&
                create_info_cache.depth_lifetime == RenderTargetLifetime::transient)) {
        throw std::runtime_error("SceneRenderTargetSet transient targets require RenderTargetPool during recreate");
    }
    frame_ready = IsValidRenderTargetHandle(color_target) &&
                  (!create_info_cache.enable_depth || IsValidRenderTargetHandle(depth_target));
    return frame_ready;
}

void SceneRenderTargetSet::AcquireTransientTargets(VulkanContext& context_,
                                                   RenderTargetHost& render_target_host_,
                                                   RenderTargetPool& render_target_pool_,
                                                   VkExtent2D swapchain_extent_) {
    if (color_format == VK_FORMAT_UNDEFINED) {
        color_format = ResolveColorFormat(context_, create_info_cache.color_format);
    }

    const RenderTargetDesc color_desc = BuildColorDesc(create_info_cache, color_format);
    if (color_desc.lifetime == RenderTargetLifetime::transient) {
        const auto color_result = render_target_pool_.AcquireTransientTarget(
            context_,
            render_target_host_,
            color_desc,
            swapchain_extent_);
        color_target = color_result.handle;
    }

    if (!create_info_cache.enable_depth) {
        depth_target = {};
        depth_format = VK_FORMAT_UNDEFINED;
        return;
    }

    if (depth_format == VK_FORMAT_UNDEFINED) {
        depth_format = ResolveDepthFormat(context_, create_info_cache.depth_format);
    }

    const RenderTargetDesc depth_desc = BuildDepthDesc(create_info_cache, depth_format);
    if (depth_desc.lifetime == RenderTargetLifetime::transient) {
        const auto depth_result = render_target_pool_.AcquireTransientTarget(
            context_,
            render_target_host_,
            depth_desc,
            swapchain_extent_);
        depth_target = depth_result.handle;
    }
    frame_ready = IsValidRenderTargetHandle(color_target) &&
                  (!create_info_cache.enable_depth || IsValidRenderTargetHandle(depth_target));
    if (frame_ready) {
        RebuildCachedOutputConfigs();
    } else {
        cached_color_outputs = {};
        cached_depth_outputs = {};
    }
}

RenderTargetColorOutputConfig SceneRenderTargetSet::BuildColorOutputConfig(bool clear_target_,
                                                                           bool final_pass_) const {
    if (!IsReady()) {
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
    if (!IsReady() || !create_info_cache.enable_depth || !IsValidRenderTargetHandle(depth_target)) {
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

bool SceneRenderTargetSet::ConfigureCompositeRenderer(RenderTargetCompositeRenderer& composite_renderer_) const noexcept {
    if (!IsReady()) {
        composite_renderer_.ClearSourceTarget();
        composite_renderer_.ResetOutputTargetConfig();
        return false;
    }
    composite_renderer_.SetSourceTarget(color_target, create_info_cache.color_final_state);
    composite_renderer_.ResetOutputTargetConfig();
    return true;
}

void SceneRenderTargetSet::ResetCompositeRenderer(RenderTargetCompositeRenderer& composite_renderer_) const noexcept {
    composite_renderer_.ClearSourceTarget();
    composite_renderer_.ResetOutputTargetConfig();
}

bool SceneRenderTargetSet::IsReady() const noexcept {
    return initialized &&
           frame_ready &&
           IsValidRenderTargetHandle(color_target) &&
           (!create_info_cache.enable_depth || IsValidRenderTargetHandle(depth_target));
}

bool SceneRenderTargetSet::HasDepthTarget() const noexcept {
    return IsReady() && create_info_cache.enable_depth && IsValidRenderTargetHandle(depth_target);
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

bool SceneRenderTargetSet::SupportsSwapchainRelativeExtent() const noexcept {
    return create_info_cache.scale_mode == RenderTargetScaleMode::swapchain_relative;
}

bool SceneRenderTargetSet::HasNonZeroExtent(VkExtent2D extent_) noexcept {
    return extent_.width > 0U && extent_.height > 0U;
}

void SceneRenderTargetSet::InvalidateCurrentFrameTargets() noexcept {
    if (create_info_cache.color_lifetime == RenderTargetLifetime::transient) {
        color_target = {};
    }
    if (create_info_cache.enable_depth &&
        create_info_cache.depth_lifetime == RenderTargetLifetime::transient) {
        depth_target = {};
    }
    cached_color_outputs = {};
    cached_depth_outputs = {};
    frame_ready = false;
}

void SceneRenderTargetSet::RebuildCachedOutputConfigs() noexcept {
    cached_color_outputs = {};
    cached_depth_outputs = {};

    const auto fill_color = [this](SceneRenderPassRole pass_role_,
                                   bool clear_target_,
                                   bool final_pass_) {
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
        cached_color_outputs[PassRoleIndex(pass_role_)] = output;
    };

    fill_color(SceneRenderPassRole::single, true, true);
    fill_color(SceneRenderPassRole::first, true, false);
    fill_color(SceneRenderPassRole::middle, false, false);
    fill_color(SceneRenderPassRole::last, false, true);

    if (!create_info_cache.enable_depth || !IsValidRenderTargetHandle(depth_target)) {
        return;
    }

    const auto fill_depth = [this](SceneRenderPassRole pass_role_,
                                   bool clear_target_) {
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
        cached_depth_outputs[PassRoleIndex(pass_role_)] = output;
    };

    fill_depth(SceneRenderPassRole::single, true);
    fill_depth(SceneRenderPassRole::first, true);
    fill_depth(SceneRenderPassRole::middle, false);
    fill_depth(SceneRenderPassRole::last, false);
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

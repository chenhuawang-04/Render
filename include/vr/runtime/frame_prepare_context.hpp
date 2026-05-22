#pragma once

#include "vr/render/render_target_types.hpp"
#include "vr/render_graph/render_graph_types.hpp"
#include "vr/vulkan_context.hpp"

#include <cstdint>
#include <functional>

namespace vr::render_graph {
class RenderGraphBuilder;
}

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

using DirectGraphImportedTextureRegisterFn = std::function<void(
    render_graph::ResourceHandle,
    RenderTargetHandle)>;

struct RuntimeDirectGraphBuildView final {
    render_graph::RenderGraphBuilder& builder;
    render_graph::ResourceHandle present_target = render_graph::invalid_resource_handle;
    const render_graph::Extent3D& reference_extent;
    render_graph::ResourceVersionHandle& present_ready_version;
    const DirectGraphImportedTextureRegisterFn& register_imported_texture;
};

} // namespace vr::render

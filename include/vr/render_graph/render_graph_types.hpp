#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

namespace vr::render_graph {

inline constexpr std::uint32_t invalid_render_graph_index =
    (std::numeric_limits<std::uint32_t>::max)();

enum class ResourceKind : std::uint8_t {
    texture = 0U,
    buffer = 1U,
};

enum class ResourceLifetime : std::uint8_t {
    imported = 0U,
    persistent = 1U,
    transient = 2U,
};

enum class AccessKind : std::uint8_t {
    read = 0U,
    write = 1U,
};

enum TextureUsageFlags : std::uint32_t {
    texture_usage_none_flag = 0U,
    texture_usage_sampled_flag = 1U << 0U,
    texture_usage_color_attachment_flag = 1U << 1U,
    texture_usage_depth_stencil_attachment_flag = 1U << 2U,
    texture_usage_storage_flag = 1U << 3U,
    texture_usage_transfer_src_flag = 1U << 4U,
    texture_usage_transfer_dst_flag = 1U << 5U,
    texture_usage_present_flag = 1U << 6U,
};

enum BufferUsageFlags : std::uint32_t {
    buffer_usage_none_flag = 0U,
    buffer_usage_vertex_flag = 1U << 0U,
    buffer_usage_index_flag = 1U << 1U,
    buffer_usage_uniform_flag = 1U << 2U,
    buffer_usage_storage_flag = 1U << 3U,
    buffer_usage_transfer_src_flag = 1U << 4U,
    buffer_usage_transfer_dst_flag = 1U << 5U,
    buffer_usage_indirect_flag = 1U << 6U,
};

enum class TextureDimension : std::uint8_t {
    image_2d = 0U,
    image_2d_array = 1U,
    image_3d = 2U,
    cube = 3U,
    cube_array = 4U,
};

enum class TextureFormat : std::uint16_t {
    unknown = 0U,
    r8g8b8a8_unorm = 1U,
    r16g16b16a16_sfloat = 2U,
    d32_sfloat = 3U,
};

enum class SampleCount : std::uint8_t {
    x1 = 1U,
    x2 = 2U,
    x4 = 4U,
    x8 = 8U,
};

struct Extent3D final {
    std::uint32_t width = 1U;
    std::uint32_t height = 1U;
    std::uint32_t depth = 1U;
};

struct TextureDesc final {
    TextureDimension dimension = TextureDimension::image_2d;
    TextureFormat format = TextureFormat::unknown;
    Extent3D extent{};
    std::uint32_t usage = texture_usage_none_flag;
    std::uint32_t mip_level_count = 1U;
    std::uint32_t array_layer_count = 1U;
    SampleCount sample_count = SampleCount::x1;
    bool allow_alias = true;
    bool clear_on_first_use = false;
};

struct BufferDesc final {
    std::uint64_t size_bytes = 0U;
    std::uint32_t usage = buffer_usage_none_flag;
    bool host_visible = false;
    bool persistently_mapped = false;
    bool allow_alias = true;
};

struct ResourceHandle final {
    std::uint32_t index = invalid_render_graph_index;
    std::uint32_t generation = 0U;
};

inline constexpr ResourceHandle invalid_resource_handle{};

struct PassHandle final {
    std::uint32_t index = invalid_render_graph_index;
};

inline constexpr PassHandle invalid_pass_handle{};

struct ResourceVersionHandle final {
    std::uint32_t resource_index = invalid_render_graph_index;
    std::uint32_t version = 0U;
};

inline constexpr ResourceVersionHandle invalid_resource_version{};

[[nodiscard]] constexpr bool IsValidResourceHandle(const ResourceHandle handle_) noexcept {
    return handle_.index != invalid_render_graph_index &&
           handle_.generation != 0U;
}

[[nodiscard]] constexpr bool IsValidPassHandle(const PassHandle handle_) noexcept {
    return handle_.index != invalid_render_graph_index;
}

[[nodiscard]] constexpr bool IsValidResourceVersionHandle(
    const ResourceVersionHandle handle_) noexcept {
    return handle_.resource_index != invalid_render_graph_index;
}

[[nodiscard]] constexpr bool HasTextureUsageFlag(const std::uint32_t flags_,
                                                 const TextureUsageFlags flag_) noexcept {
    return (flags_ & static_cast<std::uint32_t>(flag_)) != 0U;
}

[[nodiscard]] constexpr bool HasBufferUsageFlag(const std::uint32_t flags_,
                                                const BufferUsageFlags flag_) noexcept {
    return (flags_ & static_cast<std::uint32_t>(flag_)) != 0U;
}

static_assert(std::is_standard_layout_v<Extent3D>);
static_assert(std::is_standard_layout_v<TextureDesc>);
static_assert(std::is_standard_layout_v<BufferDesc>);
static_assert(std::is_standard_layout_v<ResourceHandle>);
static_assert(std::is_standard_layout_v<PassHandle>);
static_assert(std::is_standard_layout_v<ResourceVersionHandle>);

} // namespace vr::render_graph

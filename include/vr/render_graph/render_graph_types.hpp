#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

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

enum class AccessKind : std::uint16_t {
    none = 0U,

    color_attachment_read = 1U,
    color_attachment_write = 2U,

    depth_stencil_read = 3U,
    depth_stencil_write = 4U,
    depth_stencil_read_write = 5U,

    shader_sample_read = 6U,
    shader_storage_read = 7U,
    shader_storage_write = 8U,
    shader_storage_read_write = 9U,

    uniform_read = 10U,
    vertex_buffer_read = 11U,
    index_buffer_read = 12U,
    indirect_command_read = 13U,

    transfer_read = 14U,
    transfer_write = 15U,

    present = 16U,
    host_read = 17U,
    host_write = 18U,
};

enum class QueueClass : std::uint8_t {
    graphics = 0U,
    compute = 1U,
    transfer = 2U,
};

enum ShaderStageFlags : std::uint32_t {
    shader_stage_none_flag = 0U,
    shader_stage_vertex_flag = 1U << 0U,
    shader_stage_fragment_flag = 1U << 1U,
    shader_stage_compute_flag = 1U << 2U,
};

enum class DescriptorBindingSource : std::uint8_t {
    none = 0U,
    bindless_table = 1U,
};

enum class DescriptorBindingKind : std::uint8_t {
    sampled_image_table = 0U,
    sampler_table = 1U,
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

struct SubresourceRange final {
    std::uint32_t base_mip_level = 0U;
    std::uint32_t level_count = 0U;
    std::uint32_t base_array_layer = 0U;
    std::uint32_t layer_count = 0U;
};

struct BufferRange final {
    std::uint64_t offset_bytes = 0U;
    std::uint64_t size_bytes = 0U;
};

struct AccessDesc final {
    ResourceVersionHandle resource{};
    AccessKind access = AccessKind::none;
    SubresourceRange subresource_range{};
    BufferRange buffer_range{};
};

enum class AttachmentLoadOp : std::uint8_t {
    load = 0U,
    clear = 1U,
    dont_care = 2U,
};

enum class AttachmentStoreOp : std::uint8_t {
    store = 0U,
    dont_care = 1U,
};

struct ClearColorValue final {
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    float alpha = 1.0F;
};

struct ClearDepthStencilValue final {
    float depth = 1.0F;
    std::uint32_t stencil = 0U;
};

struct RasterColorAttachmentDesc final {
    ResourceHandle target{};
    AttachmentLoadOp load_op = AttachmentLoadOp::load;
    AttachmentStoreOp store_op = AttachmentStoreOp::store;
    ClearColorValue clear_value{};
};

struct RasterDepthAttachmentDesc final {
    ResourceHandle target{};
    AttachmentLoadOp load_op = AttachmentLoadOp::load;
    AttachmentStoreOp store_op = AttachmentStoreOp::store;
    AttachmentLoadOp stencil_load_op = AttachmentLoadOp::dont_care;
    AttachmentStoreOp stencil_store_op = AttachmentStoreOp::dont_care;
    ClearDepthStencilValue clear_value{};
    bool read_only = false;
};

struct RasterPassDesc final {
    std::vector<RasterColorAttachmentDesc> color_attachments{};
    bool has_depth_attachment = false;
    RasterDepthAttachmentDesc depth_attachment{};
    std::uint32_t layer_count = 1U;
};

struct PassDescriptorBindingDesc final {
    std::uint32_t set = 0U;
    std::uint32_t binding = 0U;
    DescriptorBindingSource source = DescriptorBindingSource::none;
    DescriptorBindingKind kind = DescriptorBindingKind::sampled_image_table;
    std::uint32_t stage_flags = shader_stage_none_flag;
    std::uint32_t source_id = 0U;
};

struct ShaderContractBindingDesc final {
    std::uint32_t set = 0U;
    std::uint32_t binding = 0U;
    DescriptorBindingKind kind = DescriptorBindingKind::sampled_image_table;
    std::uint32_t stage_flags = shader_stage_none_flag;
    std::uint32_t descriptor_count = 1U;
};

struct PassShaderContractDesc final {
    std::string debug_name{};
    std::vector<ShaderContractBindingDesc> bindings{};
};

struct PassDescriptorLayout final {
    PassHandle pass{};
    std::vector<PassDescriptorBindingDesc> bindings{};
};

struct DescriptorWriteDesc final {
    std::uint32_t set = 0U;
    std::uint32_t binding = 0U;
    DescriptorBindingSource source = DescriptorBindingSource::none;
    DescriptorBindingKind kind = DescriptorBindingKind::sampled_image_table;
    std::uint32_t stage_flags = shader_stage_none_flag;
    std::uint32_t source_id = 0U;
};

struct DescriptorWriteBatch final {
    PassHandle pass{};
    std::vector<DescriptorWriteDesc> writes{};
};

struct BindlessAllocation final {
    std::uint32_t table_id = 0U;
    DescriptorBindingKind kind = DescriptorBindingKind::sampled_image_table;
    std::uint32_t stage_flags = shader_stage_none_flag;
};

struct DescriptorBindingPlan final {
    std::vector<PassDescriptorLayout> pass_layouts{};
    std::vector<DescriptorWriteBatch> writes{};
    std::vector<BindlessAllocation> bindless_allocations{};
};

[[nodiscard]] inline PassShaderContractDesc MakeSharedBindlessFragmentShaderContract(
    std::string_view debug_name_ = "shared_bindless_fragment") {
    PassShaderContractDesc contract{};
    contract.debug_name = std::string(debug_name_);
    contract.bindings = {
        ShaderContractBindingDesc{
            .set = 0U,
            .binding = 0U,
            .kind = DescriptorBindingKind::sampled_image_table,
            .stage_flags = shader_stage_fragment_flag,
            .descriptor_count = 3U,
        },
        ShaderContractBindingDesc{
            .set = 1U,
            .binding = 0U,
            .kind = DescriptorBindingKind::sampler_table,
            .stage_flags = shader_stage_fragment_flag,
            .descriptor_count = 1U,
        },
    };
    return contract;
}

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

[[nodiscard]] constexpr bool HasShaderStageFlag(const std::uint32_t flags_,
                                                const ShaderStageFlags flag_) noexcept {
    return (flags_ & static_cast<std::uint32_t>(flag_)) != 0U;
}

static_assert(std::is_standard_layout_v<Extent3D>);
static_assert(std::is_standard_layout_v<TextureDesc>);
static_assert(std::is_standard_layout_v<BufferDesc>);
static_assert(std::is_standard_layout_v<ResourceHandle>);
static_assert(std::is_standard_layout_v<PassHandle>);
static_assert(std::is_standard_layout_v<ResourceVersionHandle>);
static_assert(std::is_standard_layout_v<SubresourceRange>);
static_assert(std::is_standard_layout_v<BufferRange>);
static_assert(std::is_standard_layout_v<AccessDesc>);
static_assert(std::is_standard_layout_v<ClearColorValue>);
static_assert(std::is_standard_layout_v<ClearDepthStencilValue>);
static_assert(std::is_standard_layout_v<RasterColorAttachmentDesc>);
static_assert(std::is_standard_layout_v<RasterDepthAttachmentDesc>);
static_assert(std::is_standard_layout_v<PassDescriptorBindingDesc>);
static_assert(std::is_standard_layout_v<ShaderContractBindingDesc>);
static_assert(std::is_standard_layout_v<DescriptorWriteDesc>);
static_assert(std::is_standard_layout_v<BindlessAllocation>);

} // namespace vr::render_graph

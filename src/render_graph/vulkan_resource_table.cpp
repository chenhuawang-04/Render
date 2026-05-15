#include "vr/render_graph/vulkan_resource_table.hpp"

#include <algorithm>
#include <stdexcept>

namespace vr::render_graph {
namespace {

[[nodiscard]] bool TextureDescsEqual(const TextureDesc& lhs_,
                                     const TextureDesc& rhs_) noexcept {
    return lhs_.dimension == rhs_.dimension &&
           lhs_.format == rhs_.format &&
           lhs_.extent.width == rhs_.extent.width &&
           lhs_.extent.height == rhs_.extent.height &&
           lhs_.extent.depth == rhs_.extent.depth &&
           lhs_.usage == rhs_.usage &&
           lhs_.mip_level_count == rhs_.mip_level_count &&
           lhs_.array_layer_count == rhs_.array_layer_count &&
           lhs_.sample_count == rhs_.sample_count &&
           lhs_.allow_alias == rhs_.allow_alias &&
           lhs_.clear_on_first_use == rhs_.clear_on_first_use;
}

[[nodiscard]] bool BufferDescsEqual(const BufferDesc& lhs_,
                                    const BufferDesc& rhs_) noexcept {
    return lhs_.size_bytes == rhs_.size_bytes &&
           lhs_.usage == rhs_.usage &&
           lhs_.host_visible == rhs_.host_visible &&
           lhs_.persistently_mapped == rhs_.persistently_mapped &&
           lhs_.allow_alias == rhs_.allow_alias;
}

[[nodiscard]] VkFormat ResolveVkFormat(const TextureFormat format_) noexcept {
    switch (format_) {
    case TextureFormat::r8g8b8a8_unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::r16g16b16a16_sfloat:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureFormat::d32_sfloat:
        return VK_FORMAT_D32_SFLOAT;
    case TextureFormat::unknown:
    default:
        break;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] VkSampleCountFlagBits ResolveSampleCount(const SampleCount sample_count_) noexcept {
    switch (sample_count_) {
    case SampleCount::x2:
        return VK_SAMPLE_COUNT_2_BIT;
    case SampleCount::x4:
        return VK_SAMPLE_COUNT_4_BIT;
    case SampleCount::x8:
        return VK_SAMPLE_COUNT_8_BIT;
    case SampleCount::x1:
    default:
        break;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

[[nodiscard]] VkImageUsageFlags ResolveImageUsage(const TextureDesc& desc_) noexcept {
    VkImageUsageFlags usage = 0U;
    if (HasTextureUsageFlag(desc_.usage, texture_usage_sampled_flag)) {
        usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_color_attachment_flag)) {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_depth_stencil_attachment_flag)) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_storage_flag)) {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_transfer_src_flag)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (HasTextureUsageFlag(desc_.usage, texture_usage_transfer_dst_flag)) {
        usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    return usage;
}

[[nodiscard]] VkBufferUsageFlags ResolveBufferUsage(const BufferDesc& desc_) noexcept {
    VkBufferUsageFlags usage = 0U;
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_vertex_flag)) {
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_index_flag)) {
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_uniform_flag)) {
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_storage_flag)) {
        usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_transfer_src_flag)) {
        usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_transfer_dst_flag)) {
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    if (HasBufferUsageFlag(desc_.usage, buffer_usage_indirect_flag)) {
        usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }
    return usage;
}

[[nodiscard]] VkImageAspectFlags ResolveImageAspect(const TextureDesc& desc_) noexcept {
    if (HasTextureUsageFlag(desc_.usage, texture_usage_depth_stencil_attachment_flag) ||
        desc_.format == TextureFormat::d32_sfloat) {
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

[[nodiscard]] render::RenderTargetLifetime ResolveRenderTargetLifetime(
    const ResourceLifetime lifetime_) noexcept {
    switch (lifetime_) {
    case ResourceLifetime::imported:
        return render::RenderTargetLifetime::imported;
    case ResourceLifetime::transient:
        return render::RenderTargetLifetime::transient;
    case ResourceLifetime::persistent:
    default:
        break;
    }
    return render::RenderTargetLifetime::persistent;
}

[[nodiscard]] VkImageType ResolveImageType(const TextureDimension dimension_) noexcept {
    switch (dimension_) {
    case TextureDimension::image_3d:
        return VK_IMAGE_TYPE_3D;
    case TextureDimension::image_2d_array:
    case TextureDimension::cube:
    case TextureDimension::cube_array:
    case TextureDimension::image_2d:
    default:
        break;
    }
    return VK_IMAGE_TYPE_2D;
}

[[nodiscard]] VkImageViewType ResolveImageViewType(const TextureDesc& desc_) noexcept {
    switch (desc_.dimension) {
    case TextureDimension::image_2d_array:
        return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureDimension::image_3d:
        return VK_IMAGE_VIEW_TYPE_3D;
    case TextureDimension::cube:
        return VK_IMAGE_VIEW_TYPE_CUBE;
    case TextureDimension::cube_array:
        return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    case TextureDimension::image_2d:
    default:
        break;
    }
    return VK_IMAGE_VIEW_TYPE_2D;
}

[[nodiscard]] auto FindTextureRecord(std::vector<PhysicalTextureRecord>& records_,
                                     const ResourceHandle logical_) {
    return std::find_if(records_.begin(),
                        records_.end(),
                        [&](const PhysicalTextureRecord& record_) {
                            return record_.logical.index == logical_.index;
                        });
}

[[nodiscard]] auto FindBufferRecord(std::vector<PhysicalBufferRecord>& records_,
                                    const ResourceHandle logical_) {
    return std::find_if(records_.begin(),
                        records_.end(),
                        [&](const PhysicalBufferRecord& record_) {
                            return record_.logical.index == logical_.index;
                        });
}

[[nodiscard]] bool HasImportedTextureBinding(const std::vector<render::RenderTargetHandle>& bindings_,
                                             const ResourceHandle logical_) noexcept {
    return logical_.index < bindings_.size() &&
           render::IsValidRenderTargetHandle(bindings_[logical_.index]);
}

[[nodiscard]] bool HasImportedBufferBinding(const std::vector<ImportedBufferBinding>& bindings_,
                                            const ResourceHandle logical_) noexcept {
    return logical_.index < bindings_.size() &&
           bindings_[logical_.index].buffer != VK_NULL_HANDLE;
}

} // namespace

void VulkanResourceTable::BeginFrame(VulkanContext& device_,
                                     render::RenderTargetHost& render_target_host_,
                                     const std::uint64_t last_submitted_value_,
                                     const std::uint64_t completed_submit_value_) {
    DestroyTransientRecords(device_,
                            render_target_host_,
                            last_submitted_value_,
                            completed_submit_value_);
    imported_textures.clear();
    imported_buffers.clear();
    RefreshStats();
}

void VulkanResourceTable::RegisterImportedTexture(const ResourceHandle logical_,
                                                  const render::RenderTargetHandle render_target_) noexcept {
    if (logical_.index >= imported_textures.size()) {
        imported_textures.resize(logical_.index + 1U);
    }
    imported_textures[logical_.index] = render_target_;
}

void VulkanResourceTable::RegisterImportedBuffer(const ResourceHandle logical_,
                                                 const ImportedBufferBinding& imported_buffer_) noexcept {
    if (logical_.index >= imported_buffers.size()) {
        imported_buffers.resize(logical_.index + 1U);
    }
    imported_buffers[logical_.index] = imported_buffer_;
}

void VulkanResourceTable::Resolve(VulkanContext& device_,
                                  resource::GpuMemoryHost& gpu_memory_host_,
                                  render::RenderTargetHost& render_target_host_,
                                  const CompiledRenderGraph& compiled_graph_,
                                  const std::uint64_t last_submitted_value_,
                                  const std::uint64_t completed_submit_value_) {
    std::vector<PhysicalTextureRecord> previous_textures = std::move(textures);
    std::vector<PhysicalBufferRecord> previous_buffers = std::move(buffers);
    textures.clear();
    buffers.clear();
    textures.reserve(compiled_graph_.Resources().size());
    buffers.reserve(compiled_graph_.Resources().size());

    for (const auto& resource_ : compiled_graph_.Resources()) {
        if (resource_.kind == ResourceKind::texture) {
            auto previous = FindTextureRecord(previous_textures, resource_.handle);
            PhysicalTextureRecord record{};
            record.logical = resource_.handle;
            record.debug_name = resource_.debug_name;
            record.lifetime = resource_.lifetime;
            record.desc = resource_.texture;

            if (resource_.lifetime == ResourceLifetime::imported) {
                if (!HasImportedTextureBinding(imported_textures, resource_.handle)) {
                    throw std::invalid_argument(
                        "VulkanResourceTable missing imported texture binding for logical resource");
                }
                record.render_target = imported_textures[resource_.handle.index];
                record.imported = true;
            } else if (resource_.lifetime == ResourceLifetime::persistent &&
                       previous != previous_textures.end() &&
                       !previous->imported &&
                       TextureDescsEqual(previous->desc, resource_.texture) &&
                       render::IsValidRenderTargetHandle(previous->render_target)) {
                record.render_target = previous->render_target;
                previous->render_target = render::invalid_render_target_handle;
            } else {
                if (previous != previous_textures.end() &&
                    !previous->imported &&
                    render::IsValidRenderTargetHandle(previous->render_target)) {
                    (void)render_target_host_.DestroyTarget(device_,
                                                            previous->render_target,
                                                            last_submitted_value_,
                                                            completed_submit_value_);
                    previous->render_target = render::invalid_render_target_handle;
                }

                render::RenderTargetDesc desc = BuildRenderTargetDesc(resource_.texture);
                desc.debug_name = record.debug_name.c_str();
                desc.lifetime = ResolveRenderTargetLifetime(resource_.lifetime);
                record.render_target = (resource_.lifetime == ResourceLifetime::persistent)
                    ? render_target_host_.CreatePersistentTarget(device_, desc)
                    : render_target_host_.CreateTransientTarget(device_, desc);
            }

            textures.push_back(std::move(record));
            continue;
        }

        auto previous = FindBufferRecord(previous_buffers, resource_.handle);
        PhysicalBufferRecord record{};
        record.logical = resource_.handle;
        record.debug_name = resource_.debug_name;
        record.lifetime = resource_.lifetime;
        record.desc = resource_.buffer;

        if (resource_.lifetime == ResourceLifetime::imported) {
            if (!HasImportedBufferBinding(imported_buffers, resource_.handle)) {
                throw std::invalid_argument(
                    "VulkanResourceTable missing imported buffer binding for logical resource");
            }
            record.imported = true;
            record.imported_buffer = imported_buffers[resource_.handle.index];
        } else if (resource_.lifetime == ResourceLifetime::persistent &&
                   previous != previous_buffers.end() &&
                   !previous->imported &&
                   BufferDescsEqual(previous->desc, resource_.buffer) &&
                   previous->owned_resource.buffer != VK_NULL_HANDLE) {
            record.owned_resource = previous->owned_resource;
            previous->owned_resource = {};
        } else {
            if (previous != previous_buffers.end() &&
                !previous->imported &&
                previous->owned_resource.buffer != VK_NULL_HANDLE) {
                resource::BufferHost::DestroyBuffer(device_, previous->owned_resource);
                previous->owned_resource = {};
            }
            record.owned_resource = resource::BufferHost::CreateBuffer(
                device_,
                BuildBufferCreateInfo(resource_.buffer),
                gpu_memory_host_);
        }

        buffers.push_back(std::move(record));
    }

    for (auto& record_ : previous_textures) {
        if (!record_.imported && render::IsValidRenderTargetHandle(record_.render_target)) {
            (void)render_target_host_.DestroyTarget(device_,
                                                    record_.render_target,
                                                    last_submitted_value_,
                                                    completed_submit_value_);
        }
    }

    for (auto& record_ : previous_buffers) {
        if (!record_.imported && record_.owned_resource.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(device_, record_.owned_resource);
        }
    }

    RefreshStats();
}

void VulkanResourceTable::Shutdown(VulkanContext& device_,
                                   render::RenderTargetHost& render_target_host_,
                                   const std::uint64_t last_submitted_value_,
                                   const std::uint64_t completed_submit_value_) noexcept {
    DestroyAllRecords(device_,
                      render_target_host_,
                      last_submitted_value_,
                      completed_submit_value_);
    imported_textures.clear();
    imported_buffers.clear();
    RefreshStats();
}

const PhysicalTextureRecord* VulkanResourceTable::FindTexture(const ResourceHandle logical_) const noexcept {
    const auto existing = std::find_if(textures.begin(),
                                       textures.end(),
                                       [&](const PhysicalTextureRecord& record_) {
                                           return record_.logical.index == logical_.index;
                                       });
    return (existing != textures.end()) ? &(*existing) : nullptr;
}

const PhysicalBufferRecord* VulkanResourceTable::FindBuffer(const ResourceHandle logical_) const noexcept {
    const auto existing = std::find_if(buffers.begin(),
                                       buffers.end(),
                                       [&](const PhysicalBufferRecord& record_) {
                                           return record_.logical.index == logical_.index;
                                       });
    return (existing != buffers.end()) ? &(*existing) : nullptr;
}

const std::vector<PhysicalTextureRecord>& VulkanResourceTable::Textures() const noexcept {
    return textures;
}

const std::vector<PhysicalBufferRecord>& VulkanResourceTable::Buffers() const noexcept {
    return buffers;
}

const VulkanResourceTableStats& VulkanResourceTable::Stats() const noexcept {
    return stats;
}

render::RenderTargetDesc VulkanResourceTable::BuildRenderTargetDesc(
    const TextureDesc& desc_) noexcept {
    render::RenderTargetDesc target_desc{};
    target_desc.dimension = static_cast<render::RenderTargetDimension>(desc_.dimension);
    target_desc.lifetime = ResolveRenderTargetLifetime(ResourceLifetime::persistent);
    target_desc.scale_mode = render::RenderTargetScaleMode::absolute;
    target_desc.width = desc_.extent.width;
    target_desc.height = desc_.extent.height;
    target_desc.depth = desc_.extent.depth;
    target_desc.format = ResolveVkFormat(desc_.format);
    target_desc.samples = ResolveSampleCount(desc_.sample_count);
    target_desc.usage = ResolveImageUsage(desc_);
    target_desc.aspect = ResolveImageAspect(desc_);
    target_desc.mip_levels = desc_.mip_level_count;
    target_desc.array_layers = desc_.array_layer_count;
    target_desc.color_encoding = render::RenderTargetColorEncoding::linear;
    target_desc.memory_policy = render::RenderTargetMemoryPolicy::auto_select;
    target_desc.allow_uav = HasTextureUsageFlag(desc_.usage, texture_usage_storage_flag);
    target_desc.allow_alias = desc_.allow_alias;
    return target_desc;
}

resource::BufferCreateInfo VulkanResourceTable::BuildBufferCreateInfo(
    const BufferDesc& desc_) noexcept {
    resource::BufferCreateInfo create_info{};
    create_info.size = desc_.size_bytes;
    create_info.usage = ResolveBufferUsage(desc_);
    create_info.memory_properties = desc_.host_visible
        ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    create_info.persistently_mapped = desc_.persistently_mapped;
    return create_info;
}

resource::ImageCreateInfo VulkanResourceTable::BuildImageCreateInfo(
    const TextureDesc& desc_) noexcept {
    resource::ImageCreateInfo create_info{};
    create_info.image_type = ResolveImageType(desc_.dimension);
    create_info.format = ResolveVkFormat(desc_.format);
    create_info.extent = VkExtent3D{desc_.extent.width, desc_.extent.height, desc_.extent.depth};
    create_info.mip_levels = desc_.mip_level_count;
    create_info.array_layers = desc_.array_layer_count;
    create_info.samples = ResolveSampleCount(desc_.sample_count);
    create_info.usage = ResolveImageUsage(desc_);
    create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    create_info.create_default_view = true;
    create_info.default_view_type = ResolveImageViewType(desc_);
    create_info.default_view_aspect = ResolveImageAspect(desc_);
    return create_info;
}

void VulkanResourceTable::DestroyTransientRecords(VulkanContext& device_,
                                                  render::RenderTargetHost& render_target_host_,
                                                  const std::uint64_t last_submitted_value_,
                                                  const std::uint64_t completed_submit_value_) noexcept {
    auto texture_it = textures.begin();
    while (texture_it != textures.end()) {
        if (texture_it->lifetime == ResourceLifetime::transient &&
            !texture_it->imported &&
            render::IsValidRenderTargetHandle(texture_it->render_target)) {
            (void)render_target_host_.DestroyTarget(device_,
                                                    texture_it->render_target,
                                                    last_submitted_value_,
                                                    completed_submit_value_);
            texture_it = textures.erase(texture_it);
            continue;
        }
        ++texture_it;
    }

    auto buffer_it = buffers.begin();
    while (buffer_it != buffers.end()) {
        if (buffer_it->lifetime == ResourceLifetime::transient &&
            !buffer_it->imported &&
            buffer_it->owned_resource.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(device_, buffer_it->owned_resource);
            buffer_it = buffers.erase(buffer_it);
            continue;
        }
        ++buffer_it;
    }
}

void VulkanResourceTable::DestroyAllRecords(VulkanContext& device_,
                                            render::RenderTargetHost& render_target_host_,
                                            const std::uint64_t last_submitted_value_,
                                            const std::uint64_t completed_submit_value_) noexcept {
    for (auto& texture_ : textures) {
        if (!texture_.imported && render::IsValidRenderTargetHandle(texture_.render_target)) {
            (void)render_target_host_.DestroyTarget(device_,
                                                    texture_.render_target,
                                                    last_submitted_value_,
                                                    completed_submit_value_);
            texture_.render_target = render::invalid_render_target_handle;
        }
    }
    textures.clear();

    for (auto& buffer_ : buffers) {
        if (!buffer_.imported && buffer_.owned_resource.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(device_, buffer_.owned_resource);
            buffer_.owned_resource = {};
        }
    }
    buffers.clear();
}

void VulkanResourceTable::RefreshStats() noexcept {
    stats = {};
    for (const auto& texture_ : textures) {
        if (texture_.imported) {
            stats.imported_texture_count += 1U;
        } else if (texture_.lifetime == ResourceLifetime::persistent) {
            stats.persistent_texture_count += 1U;
        } else if (texture_.lifetime == ResourceLifetime::transient) {
            stats.transient_texture_count += 1U;
        }
    }
    for (const auto& buffer_ : buffers) {
        if (buffer_.imported) {
            stats.imported_buffer_count += 1U;
        } else if (buffer_.lifetime == ResourceLifetime::persistent) {
            stats.persistent_buffer_count += 1U;
        } else if (buffer_.lifetime == ResourceLifetime::transient) {
            stats.transient_buffer_count += 1U;
        }
    }
}

} // namespace vr::render_graph

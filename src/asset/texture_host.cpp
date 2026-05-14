#include "vr/asset/texture_host.hpp"

#include "vr/render/descriptor_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"

#include <bit>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vr::asset {

namespace {

[[nodiscard]] float HalfBitsToFloat(std::uint16_t value_) noexcept {
    const std::uint32_t sign = (static_cast<std::uint32_t>(value_ & 0x8000U)) << 16U;
    const std::uint32_t exponent = (value_ >> 10U) & 0x1FU;
    std::uint32_t mantissa = value_ & 0x03FFU;

    if (exponent == 0U) {
        if (mantissa == 0U) {
            return std::bit_cast<float>(sign);
        }

        std::int32_t adjusted_exponent = -14;
        while ((mantissa & 0x0400U) == 0U) {
            mantissa <<= 1U;
            --adjusted_exponent;
        }
        mantissa &= 0x03FFU;
        const std::uint32_t bits = sign |
                                   static_cast<std::uint32_t>((adjusted_exponent + 127) << 23U) |
                                   (mantissa << 13U);
        return std::bit_cast<float>(bits);
    } else if (exponent == 31U) {
        const std::uint32_t inf_nan_bits = sign | 0x7F800000U | (mantissa << 13U);
        return std::bit_cast<float>(inf_nan_bits);
    }

    const std::uint32_t adjusted_exponent = exponent + (127U - 15U);
    const std::uint32_t bits = sign | (adjusted_exponent << 23U) | (mantissa << 13U);
    return std::bit_cast<float>(bits);
}

[[nodiscard]] bool SupportsCpuSnapshotFormat(VkFormat format_) noexcept {
    switch (format_) {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R8G8B8A8_UNORM:
        return true;
    default:
        break;
    }
    return false;
}

[[nodiscard]] std::uint32_t BytesPerTexelForSnapshot(VkFormat format_) noexcept {
    switch (format_) {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return 8U;
    case VK_FORMAT_R8G8B8A8_UNORM:
        return 4U;
    default:
        break;
    }
    return 0U;
}

[[nodiscard]] ecs::Float4 DecodeSnapshotTexel(const std::uint8_t* pixels_,
                                              VkFormat format_) noexcept {
    switch (format_) {
    case VK_FORMAT_R16G16B16A16_SFLOAT: {
        const auto* half = reinterpret_cast<const std::uint16_t*>(pixels_);
        return ecs::Float4{
            .x = HalfBitsToFloat(half[0U]),
            .y = HalfBitsToFloat(half[1U]),
            .z = HalfBitsToFloat(half[2U]),
            .w = HalfBitsToFloat(half[3U]),
        };
    }
    case VK_FORMAT_R8G8B8A8_UNORM:
        return ecs::Float4{
            .x = static_cast<float>(pixels_[0U]) / 255.0F,
            .y = static_cast<float>(pixels_[1U]) / 255.0F,
            .z = static_cast<float>(pixels_[2U]) / 255.0F,
            .w = static_cast<float>(pixels_[3U]) / 255.0F,
        };
    default:
        break;
    }
    return {};
}

void CaptureCpuBaseLevelSnapshot(TextureHost::TextureRecord& record_,
                                 const TextureUploadInfo& upload_info_) {
    record_.cpu_base_level_snapshot = {};
    if (!record_.retain_cpu_upload_data ||
        !SupportsCpuSnapshotFormat(record_.format)) {
        return;
    }

    const std::uint32_t bytes_per_texel = BytesPerTexelForSnapshot(record_.format);
    if (bytes_per_texel == 0U) {
        return;
    }

    for (std::uint32_t subresource_index = 0U;
         subresource_index < upload_info_.subresource_count;
         ++subresource_index) {
        const TextureSubresourceUploadInfo& subresource = upload_info_.subresources[subresource_index];
        if (subresource.mip_level != 0U ||
            subresource.pixels == nullptr ||
            subresource.image_offset.x != 0 ||
            subresource.image_offset.y != 0 ||
            subresource.image_offset.z != 0 ||
            subresource.image_extent.depth != 1U ||
            subresource.layer_count == 0U) {
            continue;
        }

        const std::uint32_t row_pitch_pixels =
            subresource.buffer_row_length > 0U ? subresource.buffer_row_length
                                               : subresource.image_extent.width;
        const std::uint32_t image_height =
            subresource.buffer_image_height > 0U ? subresource.buffer_image_height
                                                 : subresource.image_extent.height;
        if (row_pitch_pixels < subresource.image_extent.width ||
            image_height < subresource.image_extent.height) {
            continue;
        }

        const std::size_t layer_texel_count =
            static_cast<std::size_t>(row_pitch_pixels) * image_height;
        const std::size_t layer_byte_count = layer_texel_count * bytes_per_texel;
        if (subresource.size_bytes <
            static_cast<VkDeviceSize>(layer_byte_count * subresource.layer_count)) {
            continue;
        }

        const auto* layer_bytes = static_cast<const std::uint8_t*>(subresource.pixels);
        for (std::uint32_t layer_index = 0U; layer_index < subresource.layer_count; ++layer_index) {
            TextureHost::CpuFloatLayerSnapshot snapshot_layer{};
            snapshot_layer.array_layer = subresource.base_array_layer + layer_index;
            snapshot_layer.width = subresource.image_extent.width;
            snapshot_layer.height = subresource.image_extent.height;
            snapshot_layer.row_pitch_pixels = subresource.image_extent.width;
            snapshot_layer.pixels.resize(
                static_cast<std::size_t>(snapshot_layer.width) * snapshot_layer.height);

            const std::uint8_t* source_bytes = layer_bytes + layer_byte_count * layer_index;
            for (std::uint32_t y = 0U; y < snapshot_layer.height; ++y) {
                for (std::uint32_t x = 0U; x < snapshot_layer.width; ++x) {
                    const std::size_t source_index =
                        (static_cast<std::size_t>(y) * row_pitch_pixels + x) * bytes_per_texel;
                    const std::size_t target_index =
                        static_cast<std::size_t>(y) * snapshot_layer.width + x;
                    snapshot_layer.pixels[target_index] =
                        DecodeSnapshotTexel(source_bytes + source_index, record_.format);
                }
            }
            record_.cpu_base_level_snapshot.layers.push_back(std::move(snapshot_layer));
        }
    }

    record_.cpu_base_level_snapshot.valid = !record_.cpu_base_level_snapshot.layers.empty();
    record_.cpu_base_level_snapshot.format = record_.format;
    record_.cpu_base_level_snapshot.default_view_type = record_.default_view_type;
}

} // namespace

void TextureHost::Initialize(VulkanContext& context_,
                             resource::GpuMemoryHost& gpu_memory_host_,
                             const TextureHostCreateInfo& create_info_) {
    if (initialized) {
        Shutdown(context_);
    }

    gpu_memory_host = &gpu_memory_host_;
    bindless_config = {};
    create_info_cache = create_info_;
    textures.clear();
    retired_textures.Clear();
    stats = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;

    if (create_info_cache.reserve_texture_count > 0U) {
        textures.reserve(create_info_cache.reserve_texture_count);
    }
    if (create_info_cache.reserve_retired_texture_count > 0U) {
        retired_textures.Reserve(create_info_cache.reserve_retired_texture_count);
    }

    initialized = true;
}

void TextureHost::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(context_.Device());
    }

    for (auto& texture : textures) {
        resource::ImageHost::DestroyImage(context_, texture.resource);
    }
    textures.clear();

    DestroyRetiredTextures(context_);

    gpu_memory_host = nullptr;
    bindless_config = {};
    create_info_cache = {};
    stats = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    initialized = false;
}

void TextureHost::BeginFrame(VulkanContext& context_,
                             std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("TextureHost::BeginFrame called before Initialize");
    }

    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);
    CollectRetiredTextures(context_, completed_submit_value_);
    SyncBindlessRecords();
    stats.texture_count = static_cast<std::uint32_t>(textures.size());
    stats.retired_texture_count = retired_textures.PendingCount();
}

void TextureHost::ConfigureBindless(const TextureHostBindlessConfig& bindless_config_) {
    if (bindless_config.SameBinding(bindless_config_)) {
        return;
    }
    InvalidateBindlessRecords(bindless_config);
    bindless_config = bindless_config_;
    SyncBindlessRecords();
    if (initialized) {
        ++stats.revision;
        stats.texture_count = static_cast<std::uint32_t>(textures.size());
        stats.retired_texture_count = retired_textures.PendingCount();
    }
}

void TextureHost::UploadTexture(VulkanContext& context_,
                                render::UploadHost& upload_host_,
                                std::uint32_t frame_index_,
                                std::uint64_t last_submitted_value_,
                                std::uint64_t completed_submit_value_,
                                const TextureUploadInfo& upload_info_) {
    if (!initialized || gpu_memory_host == nullptr) {
        throw std::runtime_error("TextureHost::UploadTexture called before Initialize");
    }
    if (!upload_info_.create.texture_id.IsValid()) {
        throw std::invalid_argument("TextureHost::UploadTexture texture_id must be valid");
    }
    if (upload_info_.subresources == nullptr || upload_info_.subresource_count == 0U) {
        throw std::invalid_argument("TextureHost::UploadTexture requires at least one subresource");
    }
    if (upload_info_.create.extent.width == 0U ||
        upload_info_.create.extent.height == 0U ||
        upload_info_.create.extent.depth == 0U) {
        throw std::invalid_argument("TextureHost::UploadTexture extent must be > 0");
    }
    if (upload_info_.create.mip_levels == 0U || upload_info_.create.array_layers == 0U) {
        throw std::invalid_argument("TextureHost::UploadTexture mip_levels/array_layers must be > 0");
    }
    if (context_.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("TextureHost::UploadTexture requires Vulkan 1.3 synchronization2");
    }

    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);
    CollectRetiredTextures(context_, completed_submit_value_);
    const std::size_t lower_bound_index = LowerBoundTextureIndex(upload_info_.create.texture_id);

    const bool exists = lower_bound_index < textures.size() &&
                        textures[lower_bound_index].texture_id.value == upload_info_.create.texture_id.value;
    if (!exists) {
        const std::size_t old_size = textures.size();
        textures.resize(old_size + 1U);
        for (std::size_t index = old_size; index > lower_bound_index; --index) {
            textures[index] = std::move(textures[index - 1U]);
        }
        textures[lower_bound_index] = {};
        textures[lower_bound_index].texture_id = upload_info_.create.texture_id;
    }

    TextureRecord& record = textures[lower_bound_index];
    record.retain_cpu_upload_data = upload_info_.create.retain_cpu_upload_data;
    const bool compatible_existing =
        record.resource.image != VK_NULL_HANDLE &&
        !upload_info_.create.force_recreate &&
        record.image_flags == upload_info_.create.image_flags &&
        record.image_type == upload_info_.create.image_type &&
        record.default_view_type == upload_info_.create.default_view_type &&
        record.extent.width == upload_info_.create.extent.width &&
        record.extent.height == upload_info_.create.extent.height &&
        record.extent.depth == upload_info_.create.extent.depth &&
        record.mip_levels == upload_info_.create.mip_levels &&
        record.array_layers == upload_info_.create.array_layers &&
        record.samples == upload_info_.create.samples &&
        record.format == upload_info_.create.format &&
        record.usage == upload_info_.create.usage &&
        record.aspect_mask == upload_info_.create.aspect_mask &&
        record.shader_read_layout == upload_info_.create.shader_read_layout;

    if (!compatible_existing) {
        if (record.resource.image != VK_NULL_HANDLE) {
            RetireTexture(record, last_submitted_value_);
            ++stats.updated_texture_count;
        } else {
            ++stats.uploaded_texture_count;
        }

        record.resource = CreateImageResource(context_,
                                              *gpu_memory_host,
                                              create_info_cache,
                                              upload_info_.create);
        record.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        record.image_flags = upload_info_.create.image_flags;
        record.image_type = upload_info_.create.image_type;
        record.default_view_type = upload_info_.create.default_view_type;
        record.extent = upload_info_.create.extent;
        record.mip_levels = upload_info_.create.mip_levels;
        record.array_layers = upload_info_.create.array_layers;
        record.samples = upload_info_.create.samples;
        record.format = upload_info_.create.format;
        record.usage = upload_info_.create.usage;
        record.shader_read_layout = upload_info_.create.shader_read_layout;
        record.aspect_mask = upload_info_.create.aspect_mask;
        record.cpu_base_level_snapshot = {};
    } else {
        ++stats.updated_texture_count;
    }

    const VkImageLayout old_layout = record.current_layout;
    if (old_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        RecordImageBarrier(upload_host_,
                           frame_index_,
                           record.resource.image,
                           old_layout,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           record.aspect_mask,
                           0U,
                           record.mip_levels,
                           0U,
                           record.array_layers,
                           old_layout == VK_IMAGE_LAYOUT_UNDEFINED
                               ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
                               : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                           0U,
                           VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT);
        ++stats.barrier_count;
    }

    std::uint64_t uploaded_bytes = 0U;
    for (std::uint32_t i = 0U; i < upload_info_.subresource_count; ++i) {
        const TextureSubresourceUploadInfo& subresource = upload_info_.subresources[i];
        if (subresource.pixels == nullptr) {
            throw std::invalid_argument("TextureHost::UploadTexture subresource pixels must be non-null");
        }
        if (subresource.size_bytes == 0U) {
            throw std::invalid_argument("TextureHost::UploadTexture subresource size_bytes must be > 0");
        }
        if (subresource.mip_level >= record.mip_levels) {
            throw std::out_of_range("TextureHost::UploadTexture mip_level out of range");
        }
        if (subresource.layer_count == 0U) {
            throw std::invalid_argument("TextureHost::UploadTexture subresource layer_count must be > 0");
        }
        if (subresource.base_array_layer >= record.array_layers ||
            subresource.layer_count > record.array_layers - subresource.base_array_layer) {
            throw std::out_of_range("TextureHost::UploadTexture array layers out of range");
        }
        if (subresource.image_extent.width == 0U ||
            subresource.image_extent.height == 0U ||
            subresource.image_extent.depth == 0U) {
            throw std::invalid_argument("TextureHost::UploadTexture subresource extent must be > 0");
        }

        VkBufferImageCopy copy_region{};
        copy_region.bufferOffset = 0U;
        copy_region.bufferRowLength = subresource.buffer_row_length;
        copy_region.bufferImageHeight = subresource.buffer_image_height;
        copy_region.imageSubresource.aspectMask = record.aspect_mask;
        copy_region.imageSubresource.mipLevel = subresource.mip_level;
        copy_region.imageSubresource.baseArrayLayer = subresource.base_array_layer;
        copy_region.imageSubresource.layerCount = subresource.layer_count;
        copy_region.imageOffset = subresource.image_offset;
        copy_region.imageExtent = subresource.image_extent;

        upload_host_.StageAndRecordCopyImage(frame_index_,
                                             record.resource.image,
                                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                             copy_region,
                                             subresource.pixels,
                                             subresource.size_bytes,
                                             16U);
        uploaded_bytes += static_cast<std::uint64_t>(subresource.size_bytes);
    }

    if (record.shader_read_layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        RecordImageBarrier(upload_host_,
                           frame_index_,
                           record.resource.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           record.shader_read_layout,
                           record.aspect_mask,
                           0U,
                           record.mip_levels,
                           0U,
                           record.array_layers,
                           VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
                           VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        ++stats.barrier_count;
    }

    record.current_layout = record.shader_read_layout;
    CaptureCpuBaseLevelSnapshot(record, upload_info_);
    ++record.revision;
    SyncBindlessRecords();
    stats.uploaded_bytes += uploaded_bytes;
    stats.texture_count = static_cast<std::uint32_t>(textures.size());
    stats.retired_texture_count = retired_textures.PendingCount();
    ++stats.revision;
}

bool TextureHost::RemoveTexture(VulkanContext& context_,
                                TextureId texture_id_,
                                std::uint64_t last_submitted_value_,
                                std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("TextureHost::RemoveTexture called before Initialize");
    }
    if (!texture_id_.IsValid()) {
        return false;
    }

    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);
    CollectRetiredTextures(context_, completed_submit_value_);
    const std::size_t lower_bound_index = LowerBoundTextureIndex(texture_id_);
    if (lower_bound_index >= textures.size() ||
        textures[lower_bound_index].texture_id.value != texture_id_.value) {
        return false;
    }

    TextureRecord& record = textures[lower_bound_index];
    if (bindless_config.Enabled() && record.bindless.image_slot.IsValid()) {
        bindless_config.descriptor_host->QueueBindlessPlaceholderWrite(
            bindless_config.image_table,
            record.bindless.image_slot);
        bindless_config.descriptor_host->FreeBindlessSlotDeferred(
            bindless_config.image_table,
            record.bindless.image_slot,
            last_submitted_value_);
        record.bindless.retire_value = last_submitted_value_;
    }
    RetireTexture(record, last_submitted_value_);
    textures.erase(textures.begin() + static_cast<std::ptrdiff_t>(lower_bound_index));

    ++stats.removed_texture_count;
    stats.texture_count = static_cast<std::uint32_t>(textures.size());
    stats.retired_texture_count = retired_textures.PendingCount();
    ++stats.revision;
    return true;
}

const TextureHost::TextureRecord* TextureHost::FindTexture(TextureId texture_id_) const noexcept {
    if (!initialized || !texture_id_.IsValid()) {
        return nullptr;
    }

    const std::size_t lower_bound_index = LowerBoundTextureIndex(texture_id_);
    if (lower_bound_index >= textures.size()) {
        return nullptr;
    }
    const TextureRecord& record = textures[lower_bound_index];
    if (record.texture_id.value != texture_id_.value) {
        return nullptr;
    }
    return &record;
}

render::BindlessSlot TextureHost::ResolveBindlessImageSlot(TextureId texture_id_) const noexcept {
    const TextureRecord* record = FindTexture(texture_id_);
    return record != nullptr ? record->bindless.image_slot : render::BindlessSlot{};
}

render::BindlessSlot TextureHost::ResolveBindlessSamplerSlot(TextureId texture_id_) const noexcept {
    (void)texture_id_;
    return bindless_config.default_sampler_slot;
}

const TextureHost::CpuFloatBaseLevelSnapshot* TextureHost::FindCpuFloatBaseLevelSnapshot(
    TextureId texture_id_) const noexcept {
    const TextureRecord* record = FindTexture(texture_id_);
    if (record == nullptr || !record->retain_cpu_upload_data || !record->cpu_base_level_snapshot.valid) {
        return nullptr;
    }
    return &record->cpu_base_level_snapshot;
}

bool TextureHost::IsInitialized() const noexcept {
    return initialized;
}

const TextureHostStats& TextureHost::Stats() const noexcept {
    return stats;
}

const TextureHostBindlessConfig& TextureHost::BindlessConfig() const noexcept {
    return bindless_config;
}

bool TextureHost::SupportsSampledFormat(VulkanContext& context_,
                                        VkFormat format_) noexcept {
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), format_, &properties);
    constexpr VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    return (properties.optimalTilingFeatures & required) == required;
}

bool TextureHost::SupportsHdrEnvironmentFormat(VulkanContext& context_,
                                               VkFormat format_) noexcept {
    if (!SupportsSampledFormat(context_, format_)) {
        return false;
    }

    if (format_ == VK_FORMAT_BC6H_UFLOAT_BLOCK || format_ == VK_FORMAT_BC6H_SFLOAT_BLOCK) {
        return context_.EnabledFeatures().textureCompressionBC == VK_TRUE;
    }

    return format_ == VK_FORMAT_R16G16B16A16_SFLOAT;
}

VkFormat TextureHost::ResolveHdrEnvironmentFormat(VulkanContext& context_,
                                                  bool prefer_bc6h_) noexcept {
    if (prefer_bc6h_ &&
        SupportsHdrEnvironmentFormat(context_, VK_FORMAT_BC6H_UFLOAT_BLOCK)) {
        return VK_FORMAT_BC6H_UFLOAT_BLOCK;
    }
    if (SupportsHdrEnvironmentFormat(context_, VK_FORMAT_R16G16B16A16_SFLOAT)) {
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    if (SupportsHdrEnvironmentFormat(context_, VK_FORMAT_BC6H_SFLOAT_BLOCK)) {
        return VK_FORMAT_BC6H_SFLOAT_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}

std::size_t TextureHost::LowerBoundTextureIndex(TextureId texture_id_) const noexcept {
    std::size_t first = 0U;
    std::size_t count = textures.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (textures[it].texture_id.value < texture_id_.value) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

void TextureHost::RetireTexture(TextureRecord& record_,
                                std::uint64_t retire_value_) {
    if (record_.resource.image == VK_NULL_HANDLE) {
        return;
    }

    retired_textures.Retire(std::move(record_.resource), retire_value_);
    record_.resource = {};
    record_.current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    record_.revision = 0U;
    record_.bindless.image_revision_written = 0U;
}

void TextureHost::CollectRetiredTextures(VulkanContext& context_,
                                         std::uint64_t completed_submit_value_) {
    (void)retired_textures.Collect(completed_submit_value_,
                                   [&](resource::ImageResource& resource_) {
                                       resource::ImageHost::DestroyImage(context_, resource_);
                                   });
}

void TextureHost::DestroyRetiredTextures(VulkanContext& context_) noexcept {
    (void)retired_textures.Flush([&](resource::ImageResource& resource_) {
        resource::ImageHost::DestroyImage(context_, resource_);
    });
}

void TextureHost::InvalidateBindlessRecords(const TextureHostBindlessConfig& bindless_config_) {
    const std::uint64_t retire_value = ComputeBindlessRetireValue();
    for (TextureRecord& record : textures) {
        if (bindless_config_.Enabled() && record.bindless.image_slot.IsValid()) {
            bindless_config_.descriptor_host->QueueBindlessPlaceholderWrite(
                bindless_config_.image_table,
                record.bindless.image_slot);
            bindless_config_.descriptor_host->FreeBindlessSlotDeferred(
                bindless_config_.image_table,
                record.bindless.image_slot,
                retire_value);
            record.bindless.retire_value = retire_value;
        }
        record.bindless = {};
    }
}

void TextureHost::SyncBindlessRecords() {
    if (!bindless_config.Enabled()) {
        return;
    }

    for (TextureRecord& record : textures) {
        if (record.resource.default_view == VK_NULL_HANDLE ||
            record.current_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
            continue;
        }

        if (!record.bindless.image_slot.IsValid()) {
            record.bindless.image_slot =
                bindless_config.descriptor_host->AllocateBindlessSlot(bindless_config.image_table);
            record.bindless.image_revision_written = 0U;
        }

        if (record.bindless.image_revision_written == record.revision) {
            continue;
        }

        bindless_config.descriptor_host->QueueBindlessImageWrite(bindless_config.image_table,
                                                                 record.bindless.image_slot,
                                                                 record.resource.default_view,
                                                                 record.shader_read_layout);
        record.bindless.image_revision_written = record.revision;
    }
}

std::uint64_t TextureHost::ComputeBindlessRetireValue() const noexcept {
    return std::max(last_submitted_value_seen, completed_submit_value_seen);
}

resource::ImageResource TextureHost::CreateImageResource(VulkanContext& context_,
                                                         resource::GpuMemoryHost& gpu_memory_host_,
                                                         const TextureHostCreateInfo& create_info_,
                                                         const TextureCreateInfo& create_info_desc_) {
    resource::ImageCreateInfo image_create_info{};
    image_create_info.flags = create_info_desc_.image_flags;
    image_create_info.image_type = create_info_desc_.image_type;
    image_create_info.format = create_info_desc_.format;
    image_create_info.extent = create_info_desc_.extent;
    image_create_info.mip_levels = create_info_desc_.mip_levels;
    image_create_info.array_layers = create_info_desc_.array_layers;
    image_create_info.samples = create_info_desc_.samples;
    image_create_info.usage = create_info_desc_.usage;
    image_create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_create_info.memory_properties = create_info_.memory_properties;
    image_create_info.create_default_view = true;
    image_create_info.default_view_type = create_info_desc_.default_view_type;
    image_create_info.default_view_aspect = create_info_desc_.aspect_mask;
    image_create_info.default_base_mip_level = 0U;
    image_create_info.default_level_count = create_info_desc_.mip_levels;
    image_create_info.default_base_array_layer = 0U;
    image_create_info.default_layer_count = create_info_desc_.array_layers;
    return resource::ImageHost::CreateImage(context_, image_create_info, gpu_memory_host_);
}

void TextureHost::RecordImageBarrier(render::UploadHost& upload_host_,
                                     std::uint32_t frame_index_,
                                     VkImage image_,
                                     VkImageLayout old_layout_,
                                     VkImageLayout new_layout_,
                                     VkImageAspectFlags aspect_mask_,
                                     std::uint32_t base_mip_level_,
                                     std::uint32_t level_count_,
                                     std::uint32_t base_array_layer_,
                                     std::uint32_t layer_count_,
                                     VkPipelineStageFlags2 src_stage_mask_,
                                     VkAccessFlags2 src_access_mask_,
                                     VkPipelineStageFlags2 dst_stage_mask_,
                                     VkAccessFlags2 dst_access_mask_) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = src_stage_mask_;
    barrier.srcAccessMask = src_access_mask_;
    barrier.dstStageMask = dst_stage_mask_;
    barrier.dstAccessMask = dst_access_mask_;
    barrier.oldLayout = old_layout_;
    barrier.newLayout = new_layout_;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = aspect_mask_;
    barrier.subresourceRange.baseMipLevel = base_mip_level_;
    barrier.subresourceRange.levelCount = level_count_;
    barrier.subresourceRange.baseArrayLayer = base_array_layer_;
    barrier.subresourceRange.layerCount = layer_count_;
    upload_host_.RecordImageBarrier2(frame_index_, barrier);
}

} // namespace vr::asset


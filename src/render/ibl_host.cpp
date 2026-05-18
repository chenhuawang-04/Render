#include "vr/render/ibl_host.hpp"

#include "vr/render/bindless_resource_system.hpp"
#include "vr/resource/gpu_memory_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <stdexcept>

namespace vr::render {

namespace {

constexpr asset::TextureId kDefaultSpecularCubeTextureId{0xFFFF0001U};
constexpr asset::TextureId kDefaultBrdfLutTextureId{0xFFFF0002U};

[[nodiscard]] VkFormat ResolveBrdfLutFormat(VulkanContext& context_) noexcept {
    if (asset::TextureHost::SupportsSampledFormat(context_, VK_FORMAT_R16G16_SFLOAT)) {
        return VK_FORMAT_R16G16_SFLOAT;
    }
    if (asset::TextureHost::SupportsSampledFormat(context_, VK_FORMAT_R16G16B16A16_SFLOAT)) {
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    if (asset::TextureHost::SupportsSampledFormat(context_, VK_FORMAT_R8G8B8A8_UNORM)) {
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] bool IblParamsEqual(const IblGpuParams& lhs_,
                                  const IblGpuParams& rhs_) noexcept {
    return std::memcmp(&lhs_, &rhs_, sizeof(IblGpuParams)) == 0;
}

} // namespace

void IblHost::Initialize(VulkanContext& context_,
                         asset::TextureHost& texture_host_,
                         DescriptorHost& descriptor_host_,
                         resource::SamplerHost& sampler_host_,
                         const IblHostCreateInfo& create_info_) {
    if (initialized) {
        Shutdown(context_);
    }

    texture_host = &texture_host_;
    descriptor_host = &descriptor_host_;
    sampler_host = &sampler_host_;
    create_info_cache = create_info_;
    environments.clear();
    frame_resources.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();
    stats = {};
    active_params = {};
    active_params.tint_intensity = {1.0F, 1.0F, 1.0F, 0.0F};
    active_params.rotation_max_lod_flags = {0.0F, 1.0F, 0.0F, 0.0F};
    active_brdf_lut_texture_id = {};
    active_specular_texture_id = {};
    active_skybox_texture_id = {};
    active_specular_texture_slot = 0U;
    active_brdf_lut_texture_slot = 0U;
    active_skybox_texture_slot = 0U;
    active_sampler_slot = 0U;
    next_environment_id = 1U;
    default_specular_cube_texture_id = kDefaultSpecularCubeTextureId;
    default_brdf_lut_texture_id = kDefaultBrdfLutTextureId;
    default_specular_cube_uploaded = false;
    default_brdf_lut_uploaded = false;

    if (create_info_cache.reserve_environment_count > 0U) {
        environments.reserve(create_info_cache.reserve_environment_count);
    }
    if (create_info_cache.frames_in_flight > 0U) {
        frame_resources.resize(create_info_cache.frames_in_flight);
    }

    if (create_info_cache.frames_in_flight == 0U) {
        throw std::invalid_argument("IblHost::Initialize frames_in_flight must be > 0");
    }

    resource::SamplerDesc sampler_desc{};
    sampler_desc.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_desc.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_desc.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_desc.max_lod = VK_LOD_CLAMP_NONE;
    sampler_id = sampler_host_.RegisterSampler(context_, sampler_desc);

    DescriptorSetLayoutDesc layout_desc{};
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0U;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1U;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    layout_desc.bindings.push_back(binding);

    params_descriptor_layout_id = descriptor_host_.RegisterLayout(context_, layout_desc);

    initialized = true;
}

void IblHost::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    DestroyFrameResources(context_);

    if (texture_host != nullptr) {
        const std::uint64_t completed_submit_value = std::numeric_limits<std::uint64_t>::max();
        (void)texture_host->RemoveTexture(context_,
                                          default_brdf_lut_texture_id,
                                          completed_submit_value,
                                          completed_submit_value);
        (void)texture_host->RemoveTexture(context_,
                                          default_specular_cube_texture_id,
                                          completed_submit_value,
                                          completed_submit_value);
        texture_host->BeginFrame(context_, completed_submit_value);
    }

    texture_host = nullptr;
    descriptor_host = nullptr;
    sampler_host = nullptr;
    create_info_cache = {};
    environments.clear();
    frame_resources.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();
    params_descriptor_layout_id = {};
    sampler_id = {};
    default_specular_cube_texture_id = {};
    default_brdf_lut_texture_id = {};
    active_brdf_lut_texture_id = {};
    active_specular_texture_id = {};
    active_skybox_texture_id = {};
    active_specular_texture_slot = 0U;
    active_brdf_lut_texture_slot = 0U;
    active_skybox_texture_slot = 0U;
    active_sampler_slot = 0U;
    active_params = {};
    stats = {};
    next_environment_id = 1U;
    default_specular_cube_uploaded = false;
    default_brdf_lut_uploaded = false;
    initialized = false;
}

IblEnvironmentId IblHost::RegisterEnvironment(VulkanContext& context_,
                                              const IblEnvironmentAssetDesc& desc_) {
    (void)context_;
    if (!initialized || texture_host == nullptr) {
        throw std::runtime_error("IblHost::RegisterEnvironment called before Initialize");
    }
    if (desc_.specular_cube.IsValid()) {
        (void)RequireCubeTexture(desc_.specular_cube, "IblHost::RegisterEnvironment specular cube");
    }
    if (desc_.skybox_cube.IsValid()) {
        (void)RequireCubeTexture(desc_.skybox_cube, "IblHost::RegisterEnvironment skybox cube");
    }

    IblEnvironmentAssetDesc stored_desc = desc_;
    if (!stored_desc.environment_id.IsValid()) {
        if (next_environment_id == 0U) {
            throw std::overflow_error("IblHost::RegisterEnvironment ran out of environment ids");
        }
        stored_desc.environment_id = IblEnvironmentId{next_environment_id++};
    } else if (stored_desc.environment_id.value >= next_environment_id) {
        next_environment_id = stored_desc.environment_id.value + 1U;
    }

    const std::size_t lower_bound_index = LowerBoundEnvironmentIndex(stored_desc.environment_id);
    const bool exists = lower_bound_index < environments.size() &&
                        environments[lower_bound_index].desc.environment_id.value ==
                            stored_desc.environment_id.value;
    if (exists && !stored_desc.replace_existing) {
        throw std::runtime_error("IblHost::RegisterEnvironment duplicate environment id without replacement");
    }

    if (!exists) {
        const std::size_t old_size = environments.size();
        environments.resize(old_size + 1U);
        for (std::size_t index = old_size; index > lower_bound_index; --index) {
            environments[index] = std::move(environments[index - 1U]);
        }
        environments[lower_bound_index] = {};
    }

    environments[lower_bound_index].desc = stored_desc;
    stats.environment_count = static_cast<std::uint32_t>(environments.size());
    ++stats.revision;
    return stored_desc.environment_id;
}

bool IblHost::RemoveEnvironment(IblEnvironmentId environment_id_) noexcept {
    if (!initialized || !environment_id_.IsValid()) {
        return false;
    }

    const std::size_t lower_bound_index = LowerBoundEnvironmentIndex(environment_id_);
    if (lower_bound_index >= environments.size() ||
        environments[lower_bound_index].desc.environment_id.value != environment_id_.value) {
        return false;
    }

    environments.erase(environments.begin() + static_cast<std::ptrdiff_t>(lower_bound_index));
    stats.environment_count = static_cast<std::uint32_t>(environments.size());
    ++stats.removed_environment_count;
    ++stats.revision;
    return true;
}

void IblHost::SetBrdfLut(asset::TextureId brdf_lut_) noexcept {
    if (active_brdf_lut_texture_id.value == brdf_lut_.value) {
        return;
    }
    active_brdf_lut_texture_id = brdf_lut_;
    ++stats.revision;
}

void IblHost::PrepareFrame(const IblHostPrepareView& prepare_view_) {
    PrepareResolvedFrame(prepare_view_, nullptr, {}, active_brdf_lut_texture_id);
}

void IblHost::PrepareEnvironmentFrame(const IblHostPrepareView& prepare_view_,
                                      IblEnvironmentId environment_id_,
                                      asset::TextureId brdf_lut_texture_id_) {
    PrepareResolvedFrame(prepare_view_,
                         FindEnvironmentRecord(environment_id_),
                         environment_id_,
                         brdf_lut_texture_id_.IsValid() ? brdf_lut_texture_id_ : active_brdf_lut_texture_id);
}

void IblHost::PrepareResolvedFrame(const IblHostPrepareView& prepare_view_,
                                   const EnvironmentRecord* record_,
                                   IblEnvironmentId prepared_environment_id_,
                                   asset::TextureId brdf_lut_texture_id_) {
    if (!initialized || texture_host == nullptr || descriptor_host == nullptr || sampler_host == nullptr) {
        throw std::runtime_error("IblHost::PrepareFrame called before Initialize");
    }
    if (prepare_view_.frame.frame_index >= frame_resources.size()) {
        throw std::out_of_range("IblHost::PrepareFrame frame index out of range");
    }

    EnsureFrameResources(prepare_view_);
    EnsureDefaultTextures(prepare_view_);

    const asset::TextureHost::TextureRecord* specular_record = nullptr;
    const asset::TextureHost::TextureRecord* skybox_record = nullptr;

    if (record_ != nullptr) {
        if (record_->desc.specular_cube.IsValid()) {
            specular_record = &RequireCubeTexture(record_->desc.specular_cube,
                                                  "IblHost::PrepareFrame active specular cube");
        }
        if (record_->desc.skybox_cube.IsValid()) {
            skybox_record = &RequireCubeTexture(record_->desc.skybox_cube,
                                                "IblHost::PrepareFrame active skybox cube");
        }
    }

    if (specular_record == nullptr) {
        specular_record = &RequireCubeTexture(default_specular_cube_texture_id,
                                              "IblHost::PrepareFrame default specular cube");
    }
    if (skybox_record == nullptr) {
        skybox_record = specular_record;
    }

    asset::TextureId brdf_texture_id = brdf_lut_texture_id_;
    if (!brdf_texture_id.IsValid() && create_info_cache.create_default_brdf_lut) {
        brdf_texture_id = default_brdf_lut_texture_id;
    }
    if (!brdf_texture_id.IsValid()) {
        throw std::runtime_error("IblHost::PrepareFrame requires a BRDF LUT texture");
    }

    const asset::TextureHost::TextureRecord& brdf_record =
        RequireBrdfTexture(brdf_texture_id, "IblHost::PrepareFrame BRDF LUT");

    active_params = BuildResolvedParams(record_,
                                        *specular_record,
                                        skybox_record->texture_id.value != specular_record->texture_id.value);
    active_brdf_lut_texture_id = brdf_record.texture_id;
    active_specular_texture_id = specular_record->texture_id;
    active_skybox_texture_id = skybox_record->texture_id;
    active_specular_texture_slot = 0U;
    active_brdf_lut_texture_slot = 0U;
    active_skybox_texture_slot = 0U;
    active_sampler_slot = 0U;
    if (prepare_view_.bindless != nullptr && prepare_view_.bindless->IsInitialized()) {
        active_specular_texture_slot =
            prepare_view_.bindless->ResolveTextureImageSlot(*texture_host, specular_record->texture_id).index;
        active_brdf_lut_texture_slot =
            prepare_view_.bindless->ResolveTextureImageSlot(*texture_host, brdf_record.texture_id).index;
        active_skybox_texture_slot =
            prepare_view_.bindless->ResolveTextureImageSlot(*texture_host, skybox_record->texture_id).index;
        active_sampler_slot =
            prepare_view_.bindless->ResolveRegisteredSamplerSlot(sampler_id).index;
    }
    active_params.texture_sampler_slots = {
        active_specular_texture_slot,
        active_brdf_lut_texture_slot,
        active_skybox_texture_slot,
        active_sampler_slot
    };

    FrameResources& frame = frame_resources[prepare_view_.frame.frame_index];
    const bool frame_already_prepared =
        frame.prepared &&
        frame.prepared_frame_index == prepare_view_.frame.frame_index &&
        frame.prepared_last_submitted_value == prepare_view_.progress.last_submitted_value &&
        frame.prepared_environment_id.value == prepared_environment_id_.value &&
        frame.prepared_brdf_lut.value == brdf_record.texture_id.value &&
        frame.prepared_specular_texture.value == specular_record->texture_id.value &&
        frame.prepared_skybox_texture.value == skybox_record->texture_id.value &&
        IblParamsEqual(frame.prepared_params, active_params);

    if (!frame_already_prepared) {
        std::memcpy(frame.gpu_params_buffer.mapped_ptr, &active_params, sizeof(IblGpuParams));
        if (!resource::BufferHost::IsHostCoherent(frame.gpu_params_buffer)) {
            resource::BufferHost::Flush(prepare_view_.device, frame.gpu_params_buffer);
        }

        UpdateDescriptorSetsForFrame(prepare_view_.device,
                                     prepare_view_.frame.frame_index,
                                     *specular_record,
                                     brdf_record,
                                     *skybox_record);

        frame.prepared_environment_id = prepared_environment_id_;
        frame.prepared_brdf_lut = brdf_record.texture_id;
        frame.prepared_specular_texture = specular_record->texture_id;
        frame.prepared_skybox_texture = skybox_record->texture_id;
        frame.prepared_params = active_params;
        frame.prepared_frame_index = prepare_view_.frame.frame_index;
        frame.prepared_last_submitted_value = prepare_view_.progress.last_submitted_value;
        frame.prepared = true;
    }

    ++stats.prepared_frame_count;
    ++stats.revision;
}

const IblEnvironmentAssetDesc* IblHost::FindEnvironment(IblEnvironmentId environment_id_) const noexcept {
    if (!initialized || !environment_id_.IsValid()) {
        return nullptr;
    }

    const std::size_t lower_bound_index = LowerBoundEnvironmentIndex(environment_id_);
    if (lower_bound_index >= environments.size()) {
        return nullptr;
    }

    const EnvironmentRecord& record = environments[lower_bound_index];
    if (record.desc.environment_id.value != environment_id_.value) {
        return nullptr;
    }

    return &record.desc;
}

const IblHost::EnvironmentRecord* IblHost::FindEnvironmentRecord(
    IblEnvironmentId environment_id_) const noexcept {
    if (!initialized || !environment_id_.IsValid()) {
        return nullptr;
    }

    const std::size_t lower_bound_index = LowerBoundEnvironmentIndex(environment_id_);
    if (lower_bound_index >= environments.size()) {
        return nullptr;
    }

    const EnvironmentRecord& record = environments[lower_bound_index];
    if (record.desc.environment_id.value != environment_id_.value) {
        return nullptr;
    }

    return &record;
}

VkDescriptorSet IblHost::ActiveParamsDescriptorSet(std::uint32_t frame_index_) const {
    if (!initialized) {
        throw std::runtime_error("IblHost::ActiveParamsDescriptorSet called before Initialize");
    }
    if (frame_index_ >= frame_resources.size()) {
        throw std::out_of_range("IblHost::ActiveParamsDescriptorSet frame index out of range");
    }
    return frame_resources[frame_index_].params_descriptor_set;
}

DescriptorBufferBindingView IblHost::ActiveParamsBufferBinding(std::uint32_t frame_index_) const {
    if (!initialized) {
        throw std::runtime_error("IblHost::ActiveParamsBufferBinding called before Initialize");
    }
    if (frame_index_ >= frame_resources.size()) {
        throw std::out_of_range("IblHost::ActiveParamsBufferBinding frame index out of range");
    }
    const FrameResources& frame = frame_resources[frame_index_];
    return DescriptorBufferBindingView{
        .buffer = frame.gpu_params_buffer.buffer,
        .offset = 0U,
        .range = sizeof(IblGpuParams),
    };
}

DescriptorSetLayoutId IblHost::ParamsDescriptorLayoutId() const noexcept {
    return params_descriptor_layout_id;
}

const IblGpuParams& IblHost::ActiveParams() const noexcept {
    return active_params;
}

asset::TextureId IblHost::BrdfLut() const noexcept {
    if (active_brdf_lut_texture_id.IsValid()) {
        return active_brdf_lut_texture_id;
    }
    if (default_brdf_lut_uploaded) {
        return default_brdf_lut_texture_id;
    }
    return {};
}

asset::TextureId IblHost::ActiveSpecularTexture() const noexcept {
    return active_specular_texture_id;
}

asset::TextureId IblHost::ActiveSkyboxTexture() const noexcept {
    return active_skybox_texture_id;
}

std::uint32_t IblHost::ActiveSpecularTextureSlot() const noexcept {
    return active_specular_texture_slot;
}

std::uint32_t IblHost::ActiveBrdfLutTextureSlot() const noexcept {
    return active_brdf_lut_texture_slot;
}

std::uint32_t IblHost::ActiveSkyboxTextureSlot() const noexcept {
    return active_skybox_texture_slot;
}

std::uint32_t IblHost::ActiveSamplerSlot() const noexcept {
    return active_sampler_slot;
}

const IblHostStats& IblHost::Stats() const noexcept {
    return stats;
}

bool IblHost::IsInitialized() const noexcept {
    return initialized;
}

std::size_t IblHost::LowerBoundEnvironmentIndex(IblEnvironmentId environment_id_) const noexcept {
    std::size_t first = 0U;
    std::size_t count = environments.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (environments[it].desc.environment_id.value < environment_id_.value) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

void IblHost::EnsureFrameResources(const IblHostPrepareView& prepare_view_) {
    if (frame_resources.empty()) {
        frame_resources.resize(create_info_cache.frames_in_flight);
    }

    for (FrameResources& frame : frame_resources) {
        if (frame.gpu_params_buffer.buffer != VK_NULL_HANDLE) {
            continue;
        }

        resource::BufferCreateInfo buffer_create_info{};
        buffer_create_info.size = sizeof(IblGpuParams);
        buffer_create_info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        buffer_create_info.memory_properties =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        buffer_create_info.persistently_mapped = true;
        frame.gpu_params_buffer =
            resource::BufferHost::CreateBuffer(prepare_view_.device,
                                               buffer_create_info,
                                               prepare_view_.gpu_memory);
        if (frame.gpu_params_buffer.mapped_ptr == nullptr) {
            frame.gpu_params_buffer.mapped_ptr =
                resource::BufferHost::Map(prepare_view_.device, frame.gpu_params_buffer);
        }
        std::memset(frame.gpu_params_buffer.mapped_ptr, 0, sizeof(IblGpuParams));
    }
}

void IblHost::EnsureDefaultTextures(const IblHostPrepareView& prepare_view_) {
    if (!default_specular_cube_uploaded && create_info_cache.create_default_environment_textures) {
        UploadDefaultSpecularCube(prepare_view_);
    }
    const bool needs_default_brdf = create_info_cache.create_default_brdf_lut &&
                                    !active_brdf_lut_texture_id.IsValid();
    if (!default_brdf_lut_uploaded && needs_default_brdf) {
        UploadDefaultBrdfLut(prepare_view_);
    }
}

void IblHost::UploadDefaultSpecularCube(const IblHostPrepareView& prepare_view_) {
    if (!asset::TextureHost::SupportsHdrEnvironmentFormat(prepare_view_.device,
                                                          VK_FORMAT_R16G16B16A16_SFLOAT)) {
        throw std::runtime_error(
            "IblHost::UploadDefaultSpecularCube requires VK_FORMAT_R16G16B16A16_SFLOAT support");
    }

    std::array<std::uint16_t, 4U> face_pixels{0U, 0U, 0U, 0x3C00U};
    std::array<asset::TextureSubresourceUploadInfo, 6U> subresources{};
    for (std::uint32_t face_index = 0U; face_index < subresources.size(); ++face_index) {
        subresources[face_index].pixels = face_pixels.data();
        subresources[face_index].size_bytes = sizeof(face_pixels);
        subresources[face_index].mip_level = 0U;
        subresources[face_index].base_array_layer = face_index;
        subresources[face_index].layer_count = 1U;
        subresources[face_index].image_extent = VkExtent3D{1U, 1U, 1U};
    }

    asset::TextureUploadInfo upload_info{};
    upload_info.create.texture_id = default_specular_cube_texture_id;
    upload_info.create.image_flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    upload_info.create.default_view_type = VK_IMAGE_VIEW_TYPE_CUBE;
    upload_info.create.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    upload_info.create.extent = VkExtent3D{1U, 1U, 1U};
    upload_info.create.mip_levels = 1U;
    upload_info.create.array_layers = 6U;
    upload_info.create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    upload_info.create.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    upload_info.create.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    upload_info.subresources = subresources.data();
    upload_info.subresource_count = static_cast<std::uint32_t>(subresources.size());

    texture_host->UploadTexture(prepare_view_.device,
                                prepare_view_.upload,
                                prepare_view_.frame.frame_index,
                                prepare_view_.progress.last_submitted_value,
                                prepare_view_.progress.completed_submit_value,
                                upload_info);
    default_specular_cube_uploaded = true;
    ++stats.default_texture_build_count;
}

void IblHost::UploadDefaultBrdfLut(const IblHostPrepareView& prepare_view_) {
    const VkFormat brdf_lut_format = ResolveBrdfLutFormat(prepare_view_.device);
    if (brdf_lut_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("IblHost::UploadDefaultBrdfLut could not resolve a sampled BRDF LUT format");
    }

    std::array<std::uint16_t, 2U> rg16_pixel{0U, 0U};
    std::array<std::uint16_t, 4U> rgba16_pixel{0U, 0U, 0U, 0x3C00U};
    std::array<std::uint8_t, 4U> rgba8_pixel{0U, 0U, 0U, 255U};

    asset::TextureSubresourceUploadInfo subresource{};
    subresource.mip_level = 0U;
    subresource.base_array_layer = 0U;
    subresource.layer_count = 1U;
    subresource.image_extent = VkExtent3D{1U, 1U, 1U};

    switch (brdf_lut_format) {
    case VK_FORMAT_R16G16_SFLOAT:
        subresource.pixels = rg16_pixel.data();
        subresource.size_bytes = sizeof(rg16_pixel);
        break;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        subresource.pixels = rgba16_pixel.data();
        subresource.size_bytes = sizeof(rgba16_pixel);
        break;
    case VK_FORMAT_R8G8B8A8_UNORM:
        subresource.pixels = rgba8_pixel.data();
        subresource.size_bytes = sizeof(rgba8_pixel);
        break;
    default:
        throw std::runtime_error("IblHost::UploadDefaultBrdfLut encountered unsupported fallback format");
    }

    asset::TextureUploadInfo upload_info{};
    upload_info.create.texture_id = default_brdf_lut_texture_id;
    upload_info.create.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
    upload_info.create.format = brdf_lut_format;
    upload_info.create.extent = VkExtent3D{1U, 1U, 1U};
    upload_info.create.mip_levels = 1U;
    upload_info.create.array_layers = 1U;
    upload_info.create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    upload_info.create.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    upload_info.create.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    upload_info.subresources = &subresource;
    upload_info.subresource_count = 1U;

    texture_host->UploadTexture(prepare_view_.device,
                                prepare_view_.upload,
                                prepare_view_.frame.frame_index,
                                prepare_view_.progress.last_submitted_value,
                                prepare_view_.progress.completed_submit_value,
                                upload_info);
    default_brdf_lut_uploaded = true;
    ++stats.default_texture_build_count;
}

void IblHost::DestroyFrameResources(VulkanContext& context_) noexcept {
    for (FrameResources& frame : frame_resources) {
        if (frame.gpu_params_buffer.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(context_, frame.gpu_params_buffer);
        }
        frame = {};
    }
}

const asset::TextureHost::TextureRecord& IblHost::RequireCubeTexture(asset::TextureId texture_id_,
                                                                     const char* label_) const {
    const asset::TextureHost::TextureRecord* record = texture_host->FindTexture(texture_id_);
    if (record == nullptr) {
        throw std::runtime_error(std::string(label_) + " is not registered in TextureHost");
    }
    if (record->resource.default_view == VK_NULL_HANDLE) {
        throw std::runtime_error(std::string(label_) + " has no default image view");
    }
    if (record->default_view_type != VK_IMAGE_VIEW_TYPE_CUBE &&
        record->default_view_type != VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
        throw std::runtime_error(std::string(label_) + " must be created with a cube-compatible default view");
    }
    return *record;
}

const asset::TextureHost::TextureRecord& IblHost::RequireBrdfTexture(asset::TextureId texture_id_,
                                                                     const char* label_) const {
    const asset::TextureHost::TextureRecord* record = texture_host->FindTexture(texture_id_);
    if (record == nullptr) {
        throw std::runtime_error(std::string(label_) + " is not registered in TextureHost");
    }
    if (record->resource.default_view == VK_NULL_HANDLE) {
        throw std::runtime_error(std::string(label_) + " has no default image view");
    }
    if (record->default_view_type != VK_IMAGE_VIEW_TYPE_2D &&
        record->default_view_type != VK_IMAGE_VIEW_TYPE_2D_ARRAY) {
        throw std::runtime_error(std::string(label_) + " must be created with a 2D default view");
    }
    return *record;
}

IblGpuParams IblHost::BuildResolvedParams(const EnvironmentRecord* record_,
                                          const asset::TextureHost::TextureRecord& specular_record_,
                                          bool has_explicit_skybox_) const noexcept {
    IblGpuParams params{};
    params.rotation_max_lod_flags = {0.0F, 1.0F, 0.0F, has_explicit_skybox_ ? 1.0F : 0.0F};
    params.tint_intensity = {1.0F, 1.0F, 1.0F, 0.0F};

    if (record_ == nullptr) {
        return params;
    }

    params.sh9 = record_->desc.sh9;
    params.tint_intensity = {
        record_->desc.tint_color[0],
        record_->desc.tint_color[1],
        record_->desc.tint_color[2],
        record_->desc.intensity
    };

    const float max_lod = record_->desc.max_specular_lod >= 0.0F
        ? record_->desc.max_specular_lod
        : static_cast<float>(specular_record_.mip_levels > 0U ? specular_record_.mip_levels - 1U : 0U);

    params.rotation_max_lod_flags = {
        std::sin(record_->desc.rotation_y_radians),
        std::cos(record_->desc.rotation_y_radians),
        max_lod,
        has_explicit_skybox_ ? 1.0F : 0.0F
    };
    return params;
}

void IblHost::UpdateDescriptorSetsForFrame(VulkanContext& context_,
                                           std::uint32_t frame_index_,
                                           const asset::TextureHost::TextureRecord& specular_record_,
                                           const asset::TextureHost::TextureRecord& brdf_record_,
                                           const asset::TextureHost::TextureRecord& skybox_record_) {
    if (descriptor_host == nullptr || sampler_host == nullptr) {
        throw std::runtime_error("IblHost::UpdateDescriptorSetsForFrame missing descriptor or sampler host");
    }
    (void)specular_record_;
    (void)brdf_record_;
    (void)skybox_record_;

    FrameResources& frame = frame_resources[frame_index_];
    frame.params_descriptor_set =
        descriptor_host->AllocateSet(context_, frame_index_, params_descriptor_layout_id);

    descriptor_buffer_write_scratch.clear();
    descriptor_texel_write_scratch.clear();

    descriptor_buffer_write_scratch.push_back({
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .buffer = frame.gpu_params_buffer.buffer,
        .offset = 0U,
        .range = sizeof(IblGpuParams)
    });

    descriptor_host->UpdateSet(context_,
                               frame.params_descriptor_set,
                               descriptor_buffer_write_scratch,
                               {},
                               descriptor_texel_write_scratch);
    ++stats.descriptor_update_count;
}

} // namespace vr::render


#include "vr/render/environment/sky_environment_gpu_host.hpp"

#include <stdexcept>

namespace vr::render {

void SkyEnvironmentGpuHost::Initialize(VulkanContext&,
                                       asset::TextureHost& texture_host_,
                                       DescriptorHost& descriptor_host_,
                                       resource::SamplerHost& sampler_host_,
                                       const SkyEnvironmentGpuHostCreateInfo& create_info_) {
    texture_host = &texture_host_;
    descriptor_host = &descriptor_host_;
    sampler_host = &sampler_host_;
    create_info_cache = create_info_;
    environments.clear();
    environments.reserve(create_info_cache.reserve_environment_count);
    descriptor_set_cache.clear();
    descriptor_set_cache.resize(create_info_cache.frames_in_flight);
    for (VkDescriptorSet& descriptor_set : descriptor_set_cache) {
        descriptor_set = VK_NULL_HANDLE;
    }
    stats = {};
    next_environment_index = 1U;
    initialized = true;
}

void SkyEnvironmentGpuHost::Shutdown(VulkanContext&) {
    if (!initialized) {
        return;
    }
    texture_host = nullptr;
    descriptor_host = nullptr;
    sampler_host = nullptr;
    environments.clear();
    descriptor_set_cache.clear();
    stats = {};
    next_environment_index = 1U;
    initialized = false;
}

scene::SkyEnvironmentGpuHandle SkyEnvironmentGpuHost::RegisterOrUpdate(
    const scene::SkyEnvironmentRenderState& state_,
    const SkyEnvironmentGpuPrepareView&) {
    if (!initialized) {
        throw std::runtime_error("SkyEnvironmentGpuHost::RegisterOrUpdate called before Initialize");
    }

    const std::size_t equivalent_index = FindEquivalentEnvironmentIndex(state_);
    if (equivalent_index < environments.size()) {
        ++stats.cache_hit_count;
        return environments[equivalent_index].handle;
    }

    EnvironmentRecord record{};
    record.handle.index = next_environment_index++;
    record.handle.generation = 1U;
    record.state = state_;
    record.params = BuildParams(state_);
    environments.push_back(record);

    ++stats.register_count;
    stats.environment_count = static_cast<std::uint32_t>(environments.size());
    stats.revision += 1U;
    return record.handle;
}

void SkyEnvironmentGpuHost::PrepareFrame(const SkyEnvironmentGpuPrepareView&) {
    if (!initialized) {
        throw std::runtime_error("SkyEnvironmentGpuHost::PrepareFrame called before Initialize");
    }
    ++stats.prepared_frame_count;
}

VkDescriptorSet SkyEnvironmentGpuHost::DescriptorSet(scene::SkyEnvironmentGpuHandle,
                                                     std::uint32_t frame_index_) const {
    if (frame_index_ >= descriptor_set_cache.size()) {
        return VK_NULL_HANDLE;
    }
    return descriptor_set_cache[frame_index_];
}

const SkyEnvironmentGpuParams& SkyEnvironmentGpuHost::Params(
    scene::SkyEnvironmentGpuHandle handle_) const {
    static const SkyEnvironmentGpuParams empty_params{};
    const std::size_t index = FindEnvironmentIndex(handle_);
    if (index >= environments.size()) {
        return empty_params;
    }
    return environments[index].params;
}

const SkyEnvironmentGpuHostStats& SkyEnvironmentGpuHost::Stats() const noexcept {
    return stats;
}

bool SkyEnvironmentGpuHost::IsInitialized() const noexcept {
    return initialized;
}

std::size_t SkyEnvironmentGpuHost::FindEnvironmentIndex(
    const scene::SkyEnvironmentGpuHandle& handle_) const noexcept {
    for (std::size_t index = 0; index < environments.size(); ++index) {
        if (environments[index].handle.index == handle_.index &&
            environments[index].handle.generation == handle_.generation) {
            return index;
        }
    }
    return environments.size();
}

std::size_t SkyEnvironmentGpuHost::FindEquivalentEnvironmentIndex(
    const scene::SkyEnvironmentRenderState& state_) const noexcept {
    for (std::size_t index = 0; index < environments.size(); ++index) {
        if (EquivalentState(environments[index].state, state_)) {
            return index;
        }
    }
    return environments.size();
}

bool SkyEnvironmentGpuHost::EquivalentState(const scene::SkyEnvironmentRenderState& lhs_,
                                            const scene::SkyEnvironmentRenderState& rhs_) noexcept {
    return lhs_.mode == rhs_.mode &&
           lhs_.sky_texture_id == rhs_.sky_texture_id &&
           lhs_.irradiance_texture_id == rhs_.irradiance_texture_id &&
           lhs_.prefiltered_texture_id == rhs_.prefiltered_texture_id &&
           lhs_.brdf_lut_texture_id == rhs_.brdf_lut_texture_id &&
           lhs_.zenith_color.x == rhs_.zenith_color.x &&
           lhs_.zenith_color.y == rhs_.zenith_color.y &&
           lhs_.zenith_color.z == rhs_.zenith_color.z &&
           lhs_.zenith_color.w == rhs_.zenith_color.w &&
           lhs_.horizon_color.x == rhs_.horizon_color.x &&
           lhs_.horizon_color.y == rhs_.horizon_color.y &&
           lhs_.horizon_color.z == rhs_.horizon_color.z &&
           lhs_.horizon_color.w == rhs_.horizon_color.w &&
           lhs_.ground_color.x == rhs_.ground_color.x &&
           lhs_.ground_color.y == rhs_.ground_color.y &&
           lhs_.ground_color.z == rhs_.ground_color.z &&
           lhs_.ground_color.w == rhs_.ground_color.w &&
           lhs_.tint.x == rhs_.tint.x &&
           lhs_.tint.y == rhs_.tint.y &&
           lhs_.tint.z == rhs_.tint.z &&
           lhs_.tint.w == rhs_.tint.w &&
           lhs_.exposure == rhs_.exposure &&
           lhs_.sky_intensity == rhs_.sky_intensity &&
           lhs_.diffuse_ibl_intensity == rhs_.diffuse_ibl_intensity &&
           lhs_.specular_ibl_intensity == rhs_.specular_ibl_intensity &&
           lhs_.rotation_y == rhs_.rotation_y &&
           lhs_.max_specular_lod == rhs_.max_specular_lod &&
           lhs_.flags == rhs_.flags &&
           lhs_.revision == rhs_.revision;
}

SkyEnvironmentGpuParams SkyEnvironmentGpuHost::BuildParams(
    const scene::SkyEnvironmentRenderState& state_) noexcept {
    SkyEnvironmentGpuParams params{};
    params.tint_exposure = state_.tint;
    params.tint_exposure.w = state_.exposure;
    params.rotation_lod_flags = ecs::Float4{
        .x = state_.rotation_y,
        .y = state_.max_specular_lod,
        .z = static_cast<float>(state_.flags),
        .w = 0.0F,
    };
    params.sky_intensity_ibl_intensity = ecs::Float4{
        .x = state_.sky_intensity,
        .y = state_.diffuse_ibl_intensity,
        .z = state_.specular_ibl_intensity,
        .w = 0.0F,
    };
    params.horizon_color = state_.horizon_color;
    params.zenith_color = state_.zenith_color;
    params.ground_color = state_.ground_color;
    for (ecs::Float4& sh : params.sh9) {
        sh = ecs::Float4{};
    }
    return params;
}

} // namespace vr::render

#include "vr/render/environment/sky_environment_gpu_host.hpp"
#include "vr/render/ibl_bake_host.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace vr::render {

namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 6.28318530717958647692F;
constexpr std::uint32_t kShThetaSampleCount = 8U;
constexpr std::uint32_t kShPhiSampleCount = 16U;

struct Float3Value final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

[[nodiscard]] float Saturate(float value_) noexcept {
    return std::clamp(value_, 0.0F, 1.0F);
}

[[nodiscard]] Float3Value FromFloat4(const ecs::Float4& value_) noexcept {
    return Float3Value{
        .x = value_.x,
        .y = value_.y,
        .z = value_.z,
    };
}

[[nodiscard]] ecs::Float4 ToFloat4(const Float3Value& value_) noexcept {
    return ecs::Float4{
        .x = value_.x,
        .y = value_.y,
        .z = value_.z,
        .w = 0.0F,
    };
}

[[nodiscard]] Float3Value Add(const Float3Value& lhs_,
                              const Float3Value& rhs_) noexcept {
    return Float3Value{
        .x = lhs_.x + rhs_.x,
        .y = lhs_.y + rhs_.y,
        .z = lhs_.z + rhs_.z,
    };
}

[[nodiscard]] Float3Value Multiply(const Float3Value& value_,
                                   float scalar_) noexcept {
    return Float3Value{
        .x = value_.x * scalar_,
        .y = value_.y * scalar_,
        .z = value_.z * scalar_,
    };
}

[[nodiscard]] Float3Value Multiply(const Float3Value& lhs_,
                                   const Float3Value& rhs_) noexcept {
    return Float3Value{
        .x = lhs_.x * rhs_.x,
        .y = lhs_.y * rhs_.y,
        .z = lhs_.z * rhs_.z,
    };
}

[[nodiscard]] Float3Value Lerp(const Float3Value& lhs_,
                               const Float3Value& rhs_,
                               float t_) noexcept {
    return Float3Value{
        .x = lhs_.x + (rhs_.x - lhs_.x) * t_,
        .y = lhs_.y + (rhs_.y - lhs_.y) * t_,
        .z = lhs_.z + (rhs_.z - lhs_.z) * t_,
    };
}

[[nodiscard]] float Dot(const Float3Value& lhs_,
                        const Float3Value& rhs_) noexcept {
    return lhs_.x * rhs_.x + lhs_.y * rhs_.y + lhs_.z * rhs_.z;
}

[[nodiscard]] float LengthSquared(const Float3Value& value_) noexcept {
    return Dot(value_, value_);
}

[[nodiscard]] Float3Value Normalize(const Float3Value& value_) noexcept {
    const float length_sq = LengthSquared(value_);
    if (length_sq <= 1e-8F) {
        return Float3Value{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    }
    const float inverse_length = 1.0F / std::sqrt(length_sq);
    return Multiply(value_, inverse_length);
}

[[nodiscard]] Float3Value EvaluateGradientRadiance(
    const scene::SkyEnvironmentRenderState& state_,
    const Float3Value& direction_) noexcept {
    const Float3Value zenith = FromFloat4(state_.zenith_color);
    const Float3Value horizon = FromFloat4(state_.horizon_color);
    const Float3Value ground = FromFloat4(state_.ground_color);

    const float view_up = Saturate(direction_.y);
    const float view_down = Saturate(-direction_.y);
    const float upper_curve = std::pow(view_up, 0.65F);
    const float lower_curve = std::pow(view_down, 0.55F);

    const Float3Value sky = Lerp(horizon, zenith, upper_curve);
    const Float3Value ground_curve = Lerp(horizon, ground, lower_curve);
    return direction_.y >= 0.0F ? sky : ground_curve;
}

[[nodiscard]] Float3Value EvaluateProceduralAtmosphereRadiance(
    const scene::SkyEnvironmentRenderState& state_,
    const Float3Value& direction_) noexcept {
    const Float3Value zenith = FromFloat4(state_.zenith_color);
    const Float3Value horizon = FromFloat4(state_.horizon_color);
    const Float3Value ground = FromFloat4(state_.ground_color);
    const Float3Value tint = FromFloat4(state_.tint);

    const float density = std::max(state_.atmosphere_density, 0.05F);
    const float mie = std::max(state_.mie_scattering, 0.05F);
    const float rayleigh = std::max(state_.rayleigh_scattering, 0.05F);

    const float view_up = Saturate(direction_.y);
    const float view_down = Saturate(-direction_.y);
    const float upper_curve =
        std::pow(view_up, 0.45F + (2.2F - 0.45F) * Saturate(rayleigh * 0.2F));
    const float lower_curve =
        std::pow(view_down, 0.55F + (1.7F - 0.55F) * Saturate(mie * 0.2F));

    const Float3Value sky = Lerp(horizon, zenith, upper_curve);
    const Float3Value ground_curve = Lerp(horizon, ground, lower_curve);
    Float3Value base_color = direction_.y >= 0.0F ? sky : ground_curve;

    const float horizon_glow =
        std::exp2(-std::abs(direction_.y) * (10.0F + (2.5F - 10.0F) * Saturate(density * 0.15F)));
    base_color = Lerp(base_color,
                      horizon,
                      Saturate(horizon_glow * 0.10F * density));

    const float sun_cos_elevation = std::cos(state_.sun_elevation);
    const Float3Value sun_direction = Normalize(Float3Value{
        .x = std::cos(state_.sun_azimuth) * sun_cos_elevation,
        .y = std::sin(state_.sun_elevation),
        .z = std::sin(state_.sun_azimuth) * sun_cos_elevation,
    });
    const float sun_amount = Saturate(Dot(Normalize(direction_), sun_direction));
    const float sun_core =
        std::pow(sun_amount, 384.0F + (2048.0F - 384.0F) * Saturate(mie * 0.1F));
    const float sun_haze =
        std::pow(sun_amount, 6.0F + (40.0F - 6.0F) * Saturate(density * 0.1F));
    Float3Value sun_color = Lerp(horizon,
                                 Float3Value{.x = 1.0F, .y = 0.97F, .z = 0.92F},
                                 0.65F);
    sun_color = Multiply(sun_color, 0.5F + 0.5F * Saturate(rayleigh * 0.2F));

    Float3Value color = base_color;
    color = Add(color,
                Multiply(sun_color,
                         sun_haze * density * 0.20F + sun_core * 5.0F));
    color = Multiply(color, tint);
    color = Multiply(color, std::max(state_.sky_intensity, 0.0F));
    return color;
}

[[nodiscard]] Float3Value EvaluateEnvironmentRadiance(
    const scene::SkyEnvironmentRenderState& state_,
    const Float3Value& direction_) noexcept {
    switch (state_.mode) {
    case scene::SkyEnvironmentMode::solid_color:
        return FromFloat4(state_.zenith_color);
    case scene::SkyEnvironmentMode::gradient:
        return EvaluateGradientRadiance(state_, direction_);
    case scene::SkyEnvironmentMode::procedural_atmosphere:
        return EvaluateProceduralAtmosphereRadiance(state_, direction_);
    case scene::SkyEnvironmentMode::none:
    case scene::SkyEnvironmentMode::cubemap:
    case scene::SkyEnvironmentMode::equirectangular_hdr:
    default:
        return {};
    }
}

void EvaluateSh9Basis(const Float3Value& direction_,
                      float out_basis_[9U]) noexcept {
    const Float3Value direction = Normalize(direction_);
    const float x = direction.x;
    const float y = direction.y;
    const float z = direction.z;

    out_basis_[0U] = 0.282095F;
    out_basis_[1U] = 0.488603F * y;
    out_basis_[2U] = 0.488603F * z;
    out_basis_[3U] = 0.488603F * x;
    out_basis_[4U] = 1.092548F * x * y;
    out_basis_[5U] = 1.092548F * y * z;
    out_basis_[6U] = 0.315392F * (3.0F * z * z - 1.0F);
    out_basis_[7U] = 1.092548F * x * z;
    out_basis_[8U] = 0.546274F * (x * x - y * y);
}

[[nodiscard]] bool HasNonZeroSh9(const std::array<ecs::Float4, 9U>& sh9_) noexcept {
    for (const ecs::Float4& coefficient : sh9_) {
        if (std::abs(coefficient.x) > 1e-6F ||
            std::abs(coefficient.y) > 1e-6F ||
            std::abs(coefficient.z) > 1e-6F) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::array<ecs::Float4, 9U> BuildAnalyticSh9(
    const scene::SkyEnvironmentRenderState& state_) noexcept {
    std::array<Float3Value, 9U> accum{};
    const float d_theta = kPi / static_cast<float>(kShThetaSampleCount);
    const float d_phi = kTwoPi / static_cast<float>(kShPhiSampleCount);

    for (std::uint32_t theta_index = 0U; theta_index < kShThetaSampleCount; ++theta_index) {
        const float theta =
            (static_cast<float>(theta_index) + 0.5F) * d_theta;
        const float sin_theta = std::sin(theta);
        const float cos_theta = std::cos(theta);

        for (std::uint32_t phi_index = 0U; phi_index < kShPhiSampleCount; ++phi_index) {
            const float phi =
                (static_cast<float>(phi_index) + 0.5F) * d_phi;
            const Float3Value direction{
                .x = std::cos(phi) * sin_theta,
                .y = cos_theta,
                .z = std::sin(phi) * sin_theta,
            };
            const Float3Value radiance = EvaluateEnvironmentRadiance(state_, direction);
            const float sample_weight = sin_theta * d_theta * d_phi;

            float basis[9U]{};
            EvaluateSh9Basis(direction, basis);
            for (std::size_t sh_index = 0U; sh_index < accum.size(); ++sh_index) {
                accum[sh_index] = Add(accum[sh_index],
                                      Multiply(radiance, basis[sh_index] * sample_weight));
            }
        }
    }

    std::array<ecs::Float4, 9U> sh9{};
    for (std::size_t sh_index = 0U; sh_index < sh9.size(); ++sh_index) {
        sh9[sh_index] = ToFloat4(accum[sh_index]);
    }
    return sh9;
}

[[nodiscard]] std::array<ecs::Float4, 9U> ResolveSh9(
    const scene::SkyEnvironmentRenderState& state_) noexcept {
    if (HasNonZeroSh9(state_.sh9)) {
        return state_.sh9;
    }

    switch (state_.mode) {
    case scene::SkyEnvironmentMode::solid_color:
    case scene::SkyEnvironmentMode::gradient:
    case scene::SkyEnvironmentMode::procedural_atmosphere:
        return BuildAnalyticSh9(state_);
    case scene::SkyEnvironmentMode::none:
    case scene::SkyEnvironmentMode::cubemap:
    case scene::SkyEnvironmentMode::equirectangular_hdr:
    default:
        return state_.sh9;
    }
}

[[nodiscard]] bool HasMeaningfulIblSh9(const SkyEnvironmentGpuParams& params_) noexcept {
    for (const ecs::Float4& coefficient : params_.sh9) {
        if (std::abs(coefficient.x) > 1e-6F ||
            std::abs(coefficient.y) > 1e-6F ||
            std::abs(coefficient.z) > 1e-6F) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool Sh9ArraysEqual(const std::array<ecs::Float4, 9U>& lhs_,
                                  const std::array<ecs::Float4, 9U>& rhs_) noexcept {
    for (std::size_t sh_index = 0U; sh_index < lhs_.size(); ++sh_index) {
        if (lhs_[sh_index].x != rhs_[sh_index].x ||
            lhs_[sh_index].y != rhs_[sh_index].y ||
            lhs_[sh_index].z != rhs_[sh_index].z ||
            lhs_[sh_index].w != rhs_[sh_index].w) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool TryBuildBakeSourceFromSnapshot(
    const asset::TextureHost::CpuFloatBaseLevelSnapshot& snapshot_,
    scene::SkyEnvironmentMode source_mode_,
    IblBakeSourceDesc* out_source_) {
    if (out_source_ == nullptr || !snapshot_.valid) {
        return false;
    }

    switch (source_mode_) {
    case scene::SkyEnvironmentMode::equirectangular_hdr:
        if (snapshot_.layers.size() != 1U) {
            return false;
        }
        out_source_->kind = IblBakeSourceKind::equirectangular;
        out_source_->equirect = IblCpuImageView{
            .pixels = snapshot_.layers[0U].pixels.data(),
            .width = snapshot_.layers[0U].width,
            .height = snapshot_.layers[0U].height,
            .row_pitch_pixels = snapshot_.layers[0U].row_pitch_pixels,
        };
        return out_source_->equirect.pixels != nullptr &&
               out_source_->equirect.width > 0U &&
               out_source_->equirect.height > 0U;
    case scene::SkyEnvironmentMode::cubemap:
        if (snapshot_.layers.size() < 6U) {
            return false;
        }
        out_source_->kind = IblBakeSourceKind::cubemap;
        for (std::size_t face_index = 0U; face_index < 6U; ++face_index) {
            const auto* layer = [&]() -> const asset::TextureHost::CpuFloatLayerSnapshot* {
                for (const auto& candidate : snapshot_.layers) {
                    if (candidate.array_layer == face_index) {
                        return &candidate;
                    }
                }
                return nullptr;
            }();
            if (layer == nullptr) {
                return false;
            }
            if (layer->pixels.empty() || layer->width == 0U || layer->height == 0U) {
                return false;
            }
            out_source_->cubemap.faces[face_index] = IblCpuImageView{
                .pixels = layer->pixels.data(),
                .width = layer->width,
                .height = layer->height,
                .row_pitch_pixels = layer->row_pitch_pixels,
            };
        }
        return true;
    case scene::SkyEnvironmentMode::none:
    case scene::SkyEnvironmentMode::solid_color:
    case scene::SkyEnvironmentMode::gradient:
    case scene::SkyEnvironmentMode::procedural_atmosphere:
    default:
        break;
    }
    return false;
}

[[nodiscard]] std::array<ecs::Float4, 9U> ConvertBakedSh9(
    const std::array<std::array<float, 4U>, 9U>& sh9_) noexcept {
    std::array<ecs::Float4, 9U> converted{};
    for (std::size_t sh_index = 0U; sh_index < converted.size(); ++sh_index) {
        converted[sh_index] = ecs::Float4{
            .x = sh9_[sh_index][0U],
            .y = sh9_[sh_index][1U],
            .z = sh9_[sh_index][2U],
            .w = sh9_[sh_index][3U],
        };
    }
    return converted;
}

} // namespace

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
    record.ibl = BuildIblData(state_, &record.bake_desc, &record.has_bake_desc);
    environments.push_back(record);

    ++stats.register_count;
    if (record.has_bake_desc) {
        ++stats.bake_request_count;
    }
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

const SkyEnvironmentIblData& SkyEnvironmentGpuHost::IblData(
    scene::SkyEnvironmentGpuHandle handle_) const {
    static const SkyEnvironmentIblData empty{};
    const std::size_t index = FindEnvironmentIndex(handle_);
    if (index >= environments.size()) {
        return empty;
    }
    return environments[index].ibl;
}

const SkyEnvironmentBakeDesc* SkyEnvironmentGpuHost::PendingBakeDesc(
    scene::SkyEnvironmentGpuHandle handle_) const {
    const std::size_t index = FindEnvironmentIndex(handle_);
    if (index >= environments.size()) {
        return nullptr;
    }
    const EnvironmentRecord& record = environments[index];
    return record.has_bake_desc ? &record.bake_desc : nullptr;
}

bool SkyEnvironmentGpuHost::HasPendingBake(scene::SkyEnvironmentGpuHandle handle_) const noexcept {
    const std::size_t index = FindEnvironmentIndex(handle_);
    return index < environments.size() && environments[index].has_bake_desc;
}

bool SkyEnvironmentGpuHost::ResolveSharedBrdfLut(scene::SkyEnvironmentGpuHandle handle_,
                                                 asset::TextureId brdf_lut_texture_id_) noexcept {
    const std::size_t index = FindEnvironmentIndex(handle_);
    if (index >= environments.size() || !brdf_lut_texture_id_.IsValid()) {
        return false;
    }

    EnvironmentRecord& record = environments[index];
    if (record.ibl.brdf_lut_texture.value == brdf_lut_texture_id_.value &&
        record.ibl.uses_shared_brdf_lut != 0U) {
        return false;
    }

    record.ibl.brdf_lut_texture = brdf_lut_texture_id_;
    record.ibl.uses_shared_brdf_lut = 1U;
    record.ibl.revision += 1U;
    ++stats.shared_brdf_lut_resolve_count;
    ++stats.revision;
    return true;
}

bool SkyEnvironmentGpuHost::ApplyBakeResult(scene::SkyEnvironmentGpuHandle handle_,
                                            const SkyEnvironmentBakeResult& result_) noexcept {
    const std::size_t index = FindEnvironmentIndex(handle_);
    if (index >= environments.size()) {
        return false;
    }

    EnvironmentRecord& record = environments[index];
    bool changed = false;

    if (result_.irradiance_texture_id != 0U &&
        record.ibl.irradiance_texture.value != result_.irradiance_texture_id) {
        record.ibl.irradiance_texture.value = result_.irradiance_texture_id;
        changed = true;
    }
    if (result_.prefiltered_texture_id != 0U &&
        record.ibl.prefiltered_texture.value != result_.prefiltered_texture_id) {
        record.ibl.prefiltered_texture.value = result_.prefiltered_texture_id;
        changed = true;
    }
    if (result_.brdf_lut_texture_id != 0U &&
        record.ibl.brdf_lut_texture.value != result_.brdf_lut_texture_id) {
        record.ibl.brdf_lut_texture.value = result_.brdf_lut_texture_id;
        changed = true;
    }

    record.ibl.pending_bake = 0U;
    record.has_bake_desc = false;
    if (result_.revision != 0U) {
        record.ibl.revision = result_.revision;
    } else if (changed) {
        record.ibl.revision += 1U;
    }

    if (changed) {
        ++stats.bake_apply_count;
        ++stats.revision;
    }
    return changed;
}

bool SkyEnvironmentGpuHost::TryBakePendingEnvironment(scene::SkyEnvironmentGpuHandle handle_,
                                                      IblBakeHost& ibl_bake_host_,
                                                      const IblBakeHostPrepareView& prepare_view_) {
    const std::size_t index = FindEnvironmentIndex(handle_);
    if (index >= environments.size() || texture_host == nullptr) {
        return false;
    }

    EnvironmentRecord& record = environments[index];
    if (!record.has_bake_desc || !record.ibl.pending_bake) {
        return false;
    }

    const asset::TextureId source_texture_id{record.bake_desc.source_texture_id};
    const auto* snapshot = texture_host->FindCpuFloatBaseLevelSnapshot(source_texture_id);
    if (snapshot == nullptr) {
        return false;
    }

    IblBakeSourceDesc source{};
    if (!TryBuildBakeSourceFromSnapshot(*snapshot, record.bake_desc.source_mode, &source)) {
        return false;
    }

    IblBakeRequest request{};
    request.source = source;
    request.environment_id = record.ibl_environment;
    request.brdf_lut_texture_id = record.ibl.brdf_lut_texture;
    request.specular_texture_id = record.ibl.prefiltered_texture;
    request.specular_cube_size = std::max(record.bake_desc.prefilter_size, 1U);
    request.brdf_lut_size = std::max(record.bake_desc.brdf_lut_size, 1U);
    request.intensity = record.state.specular_ibl_intensity;
    request.rotation_y_radians = record.state.rotation_y;
    request.tint_color = {
        record.state.tint.x,
        record.state.tint.y,
        record.state.tint.z,
    };
    request.bake_brdf_lut = !record.ibl.brdf_lut_texture.IsValid();
    request.bake_skybox = false;
    request.bake_specular = true;
    request.bake_sh9 = true;

    IblBakeHostPrepareView local_prepare_view = prepare_view_;
    local_prepare_view.ibl = nullptr;
    const IblBakeResult bake_result = ibl_bake_host_.BakeEnvironment(local_prepare_view, request);

    bool changed = false;
    if (bake_result.specular_cube.IsValid() &&
        record.ibl.prefiltered_texture.value != bake_result.specular_cube.value) {
        record.ibl.prefiltered_texture = bake_result.specular_cube;
        changed = true;
    }
    if (bake_result.brdf_lut.IsValid() &&
        record.ibl.brdf_lut_texture.value != bake_result.brdf_lut.value) {
        record.ibl.brdf_lut_texture = bake_result.brdf_lut;
        changed = true;
    }

    const std::array<ecs::Float4, 9U> baked_sh9 = ConvertBakedSh9(bake_result.environment.sh9);
    if (!Sh9ArraysEqual(record.params.sh9, baked_sh9)) {
        record.params.sh9 = baked_sh9;
        changed = true;
    }

    if (record.ibl_max_specular_lod != bake_result.environment.max_specular_lod) {
        record.ibl_max_specular_lod = bake_result.environment.max_specular_lod;
        record.params.rotation_lod_flags.y = bake_result.environment.max_specular_lod;
        changed = true;
    }

    record.ibl.pending_bake = 0U;
    record.has_bake_desc = false;
    if (changed) {
        record.ibl.revision += 1U;
        ++stats.bake_apply_count;
        ++stats.revision;
    }
    return changed;
}

IblEnvironmentId SkyEnvironmentGpuHost::EnsureIblEnvironment(
    VulkanContext& context_,
    IblHost& ibl_host_,
    scene::SkyEnvironmentGpuHandle handle_) {
    const std::size_t index = FindEnvironmentIndex(handle_);
    if (index >= environments.size()) {
        return {};
    }

    EnvironmentRecord& record = environments[index];
    asset::TextureId specular_texture = record.ibl.prefiltered_texture;
    if (!specular_texture.IsValid() && record.state.mode == scene::SkyEnvironmentMode::cubemap) {
        specular_texture.value = record.state.sky_texture_id;
    }
    const bool has_meaningful_sh9 = HasMeaningfulIblSh9(record.params);
    if (!specular_texture.IsValid() && !has_meaningful_sh9) {
        return {};
    }

    if (record.ibl_environment.IsValid() &&
        record.ibl_revision == record.ibl.revision) {
        return record.ibl_environment;
    }

    IblEnvironmentAssetDesc desc{};
    desc.environment_id = record.ibl_environment;
    desc.specular_cube = specular_texture;
    if (record.state.mode == scene::SkyEnvironmentMode::cubemap && record.state.sky_texture_id != 0U) {
        desc.skybox_cube.value = record.state.sky_texture_id;
    }
    desc.intensity = specular_texture.IsValid()
        ? record.state.specular_ibl_intensity
        : record.state.diffuse_ibl_intensity;
    desc.rotation_y_radians = record.state.rotation_y;
    desc.max_specular_lod =
        record.ibl_max_specular_lod >= 0.0F ? record.ibl_max_specular_lod
                                            : record.state.max_specular_lod;
    desc.tint_color = {
        record.state.tint.x,
        record.state.tint.y,
        record.state.tint.z,
    };
    for (std::size_t sh_index = 0; sh_index < desc.sh9.size() && sh_index < record.params.sh9.size();
         ++sh_index) {
        desc.sh9[sh_index] = {
            record.params.sh9[sh_index].x,
            record.params.sh9[sh_index].y,
            record.params.sh9[sh_index].z,
            record.params.sh9[sh_index].w,
        };
    }

    record.ibl_environment = ibl_host_.RegisterEnvironment(context_, desc);
    if (record.ibl.brdf_lut_texture.IsValid()) {
        ibl_host_.SetBrdfLut(record.ibl.brdf_lut_texture);
    }
    record.ibl_revision = record.ibl.revision;
    ++stats.ibl_register_count;
    return record.ibl_environment;
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
    const bool sh9_equal = [&]() noexcept {
        for (std::size_t sh_index = 0U; sh_index < lhs_.sh9.size(); ++sh_index) {
            if (lhs_.sh9[sh_index].x != rhs_.sh9[sh_index].x ||
                lhs_.sh9[sh_index].y != rhs_.sh9[sh_index].y ||
                lhs_.sh9[sh_index].z != rhs_.sh9[sh_index].z ||
                lhs_.sh9[sh_index].w != rhs_.sh9[sh_index].w) {
                return false;
            }
        }
        return true;
    }();

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
           lhs_.sun_elevation == rhs_.sun_elevation &&
           lhs_.sun_azimuth == rhs_.sun_azimuth &&
           lhs_.atmosphere_density == rhs_.atmosphere_density &&
           lhs_.mie_scattering == rhs_.mie_scattering &&
           lhs_.rayleigh_scattering == rhs_.rayleigh_scattering &&
           lhs_.flags == rhs_.flags &&
           sh9_equal &&
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
    const std::array<ecs::Float4, 9U> resolved_sh9 = ResolveSh9(state_);
    for (std::size_t sh_index = 0U; sh_index < params.sh9.size(); ++sh_index) {
        params.sh9[sh_index] = resolved_sh9[sh_index];
    }
    return params;
}

SkyEnvironmentIblData SkyEnvironmentGpuHost::BuildIblData(
    const scene::SkyEnvironmentRenderState& state_,
    SkyEnvironmentBakeDesc* bake_desc_out_,
    bool* has_bake_desc_out_) noexcept {
    SkyEnvironmentIblData ibl{};
    bool has_bake_desc = false;
    SkyEnvironmentBakeDesc bake_desc{};

    ibl.irradiance_texture.value = state_.irradiance_texture_id;
    ibl.prefiltered_texture.value = state_.prefiltered_texture_id;
    ibl.brdf_lut_texture.value = state_.brdf_lut_texture_id;
    ibl.has_explicit_ibl = (state_.irradiance_texture_id != 0U ||
                            state_.prefiltered_texture_id != 0U ||
                            state_.brdf_lut_texture_id != 0U)
        ? 1U
        : 0U;

    const bool needs_prefilter = !ibl.prefiltered_texture.IsValid();
    const bool needs_irradiance = !ibl.irradiance_texture.IsValid();
    const bool needs_brdf_lut = !ibl.brdf_lut_texture.IsValid();

    switch (state_.mode) {
    case scene::SkyEnvironmentMode::cubemap:
    case scene::SkyEnvironmentMode::equirectangular_hdr:
        if (needs_prefilter || needs_irradiance) {
            bake_desc.source_texture_id = state_.sky_texture_id;
            bake_desc.source_mode = state_.mode;
            bake_desc.generate_irradiance = needs_irradiance;
            bake_desc.generate_prefilter = needs_prefilter;
            bake_desc.generate_brdf_lut = needs_brdf_lut;
            has_bake_desc = true;
            ibl.pending_bake = 1U;
        }
        if (needs_brdf_lut) {
            ibl.uses_shared_brdf_lut = 1U;
        }
        break;
    case scene::SkyEnvironmentMode::procedural_atmosphere:
        break;
    case scene::SkyEnvironmentMode::none:
    case scene::SkyEnvironmentMode::solid_color:
    case scene::SkyEnvironmentMode::gradient:
    default:
        break;
    }

    if (bake_desc_out_ != nullptr) {
        *bake_desc_out_ = bake_desc;
    }
    if (has_bake_desc_out_ != nullptr) {
        *has_bake_desc_out_ = has_bake_desc;
    }
    return ibl;
}

} // namespace vr::render

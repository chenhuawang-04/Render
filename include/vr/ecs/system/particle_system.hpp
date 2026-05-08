#pragma once

#include "vr/ecs/component/particle_component.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"

#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <limits>

namespace vr::ecs {

template<DimensionTag DimensionT>
class ParticleSystem final {
public:
    using ParticleType = Particle<DimensionT>;
    using StyleType = typename ParticleType::StyleType;
    using RuntimeType = typename ParticleType::RuntimeType;

    [[nodiscard]] static std::uint64_t AppearanceHandleMutationSerial() noexcept {
        return appearance_handle_mutation_serial.load(std::memory_order_relaxed);
    }

    // [pass:2][material:16][texture:16][minor:16][batch:14]
    static constexpr std::uint32_t sort_key_batch_bits = 14U;
    static constexpr std::uint32_t sort_key_minor_bits = 16U;
    static constexpr std::uint32_t sort_key_texture_bits = 16U;
    static constexpr std::uint32_t sort_key_material_bits = 16U;
    static constexpr std::uint32_t sort_key_pass_bits = 2U;

    static constexpr std::uint32_t sort_key_batch_shift = 0U;
    static constexpr std::uint32_t sort_key_minor_shift = sort_key_batch_shift + sort_key_batch_bits;
    static constexpr std::uint32_t sort_key_texture_shift = sort_key_minor_shift + sort_key_minor_bits;
    static constexpr std::uint32_t sort_key_material_shift = sort_key_texture_shift + sort_key_texture_bits;
    static constexpr std::uint32_t sort_key_pass_shift = sort_key_material_shift + sort_key_material_bits;
    static constexpr std::uint32_t sort_key_binding_shift = sort_key_texture_shift;

    static constexpr std::uint64_t sort_key_batch_mask = (std::uint64_t{1U} << sort_key_batch_bits) - 1U;
    static constexpr std::uint64_t sort_key_minor_mask = (std::uint64_t{1U} << sort_key_minor_bits) - 1U;
    static constexpr std::uint64_t sort_key_texture_mask = (std::uint64_t{1U} << sort_key_texture_bits) - 1U;
    static constexpr std::uint64_t sort_key_material_mask = (std::uint64_t{1U} << sort_key_material_bits) - 1U;
    static constexpr std::uint64_t sort_key_pass_mask = (std::uint64_t{1U} << sort_key_pass_bits) - 1U;

    static_assert(sort_key_pass_bits + sort_key_material_bits + sort_key_texture_bits +
                      sort_key_minor_bits + sort_key_batch_bits == 64U,
                  "ParticleSystem sort-key bit layout must be exactly 64 bits");

    static void Initialize(ParticleType& component_) noexcept {
        SetDefaultStyle(component_);
        SetDefaultRuntime(component_);
        RebuildSortKey(component_);
    }

    static void SetDefaultStyle(ParticleType& component_) noexcept {
        component_.style.max_particles = 256U;
        component_.style.max_alive_per_frame = 256U;
        component_.style.simulation_mode = ParticleSimulationMode::cpu;
        component_.style.render_mode = std::same_as<DimensionT, Dim2>
            ? ParticleRenderMode::axis_aligned
            : ParticleRenderMode::billboard;
        component_.style.sort_mode = std::same_as<DimensionT, Dim2>
            ? ParticleSortMode::bucket
            : ParticleSortMode::by_view_depth;
        component_.style.lighting_mode = ParticleLightingMode::unlit;
        component_.style.receive_shadow = 0U;
        component_.style.premultiplied_alpha = 0U;
        component_.style.facing_mode = std::same_as<DimensionT, Dim2>
            ? ParticleFacingMode::local
            : ParticleFacingMode::screen;
        component_.style.blend_mode = ParticleBlendMode::alpha;
        component_.style.start_color = Rgba8{255U, 255U, 255U, 255U};
        component_.style.end_color = Rgba8{255U, 255U, 255U, 0U};
        component_.style.stretch_velocity_scale = 0.0F;
        component_.style.soft_particle_distance = 0.0F;
        component_.style.lod_bias = 0.0F;
        component_.style.screen_coverage_budget = 1.0F;
        component_.style.motion_blur_scale = 0.0F;

        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.style.reserved0 = 0U;
            component_.style.layer = 0;
        } else {
            component_.style.depth_test = 1U;
            component_.style.depth_write = 0U;
            component_.style.double_sided = 1U;
            component_.style.reserved0 = 0U;
        }

        MarkDirty(component_, particle_dirty_style_flag | particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetDefaultRuntime(ParticleType& component_) noexcept {
        component_.runtime.route.sort_key = 0U;
        component_.runtime.route.material_id = 0U;
        component_.runtime.route.texture_id = 0U;
        component_.runtime.route.batch_tag = 0U;
        component_.runtime.route.user_data = 0U;
        component_.runtime.route.appearance_handle = invalid_appearance_handle;
        component_.runtime.route.appearance_pipeline_bucket = 0U;
        component_.runtime.route.appearance_resource_bucket = 0U;
        component_.runtime.route.depth_bin = 0U;
        component_.runtime.route.visible = 1U;
        component_.runtime.route.cast_shadow = 0U;
        component_.runtime.route.pass_hint = ParticleRenderPassHint::transparent;
        component_.runtime.route.dirty_flags = particle_dirty_style_flag |
                                               particle_dirty_emitter_flag |
                                               particle_dirty_runtime_flag |
                                               particle_dirty_simulation_flag;
        component_.runtime.emitter_handle = invalid_particle_emitter_handle;
        component_.runtime.pool_handle = invalid_particle_pool_handle;
        component_.runtime.active_count = 0U;
        component_.runtime.visible_count = 0U;
        component_.runtime.revision_authoring = 1U;
        component_.runtime.revision_simulation = 0U;
        component_.runtime.last_visible_set_revision = 0U;
        component_.runtime.reserved0 = 0U;
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const ParticleType& component_) noexcept {
        return component_.runtime.route.dirty_flags;
    }

    [[nodiscard]] static bool HasDirtyFlags(const ParticleType& component_,
                                            std::uint32_t dirty_mask_) noexcept {
        return (component_.runtime.route.dirty_flags & dirty_mask_) != 0U;
    }

    static void MarkDirty(ParticleType& component_, std::uint32_t dirty_mask_) noexcept {
        component_.runtime.route.dirty_flags |= dirty_mask_;
    }

    static void ClearDirtyFlags(ParticleType& component_, std::uint32_t clear_mask_) noexcept {
        component_.runtime.route.dirty_flags &= ~clear_mask_;
    }

    static void SetVisible(ParticleType& component_, bool visible_) noexcept {
        const std::uint8_t visible_value = visible_ ? 1U : 0U;
        if (component_.runtime.route.visible == visible_value) {
            return;
        }
        component_.runtime.route.visible = visible_value;
        MarkDirty(component_, particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetRenderPassHint(ParticleType& component_,
                                  ParticleRenderPassHint pass_hint_) noexcept {
        if (component_.runtime.route.pass_hint == pass_hint_) {
            return;
        }
        component_.runtime.route.pass_hint = pass_hint_;
        MarkDirty(component_, particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
        RebuildSortKey(component_);
    }

    static void SetMaterialId(ParticleType& component_, std::uint32_t material_id_) noexcept {
        if (component_.runtime.route.material_id == material_id_) {
            return;
        }
        component_.runtime.route.material_id = material_id_;
        MarkDirty(component_, particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
        RebuildSortKey(component_);
    }

    static void SetTextureId(ParticleType& component_, std::uint32_t texture_id_) noexcept {
        if (component_.runtime.route.texture_id == texture_id_) {
            return;
        }
        component_.runtime.route.texture_id = texture_id_;
        MarkDirty(component_, particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
        RebuildSortKey(component_);
    }

    static void SetBatchTag(ParticleType& component_, std::uint32_t batch_tag_) noexcept {
        if (component_.runtime.route.batch_tag == batch_tag_) {
            return;
        }
        component_.runtime.route.batch_tag = batch_tag_;
        MarkDirty(component_, particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
        RebuildSortKey(component_);
    }

    static void SetUserData(ParticleType& component_, std::uint32_t user_data_) noexcept {
        if (component_.runtime.route.user_data == user_data_) {
            return;
        }
        component_.runtime.route.user_data = user_data_;
        MarkDirty(component_, particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetAppearanceHandle(ParticleType& component_,
                                    AppearanceHandle appearance_handle_) noexcept {
        if (component_.runtime.route.appearance_handle.index == appearance_handle_.index &&
            component_.runtime.route.appearance_handle.generation == appearance_handle_.generation) {
            return;
        }
        component_.runtime.route.appearance_handle = appearance_handle_;
        BumpAppearanceHandleMutationSerial();
        MarkDirty(component_, particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
    }

    static void ClearAppearanceHandle(ParticleType& component_) noexcept {
        if (component_.runtime.route.appearance_handle.index == invalid_appearance_handle.index &&
            component_.runtime.route.appearance_handle.generation == invalid_appearance_handle.generation &&
            component_.runtime.route.appearance_pipeline_bucket == 0U &&
            component_.runtime.route.appearance_resource_bucket == 0U) {
            return;
        }
        component_.runtime.route.appearance_handle = invalid_appearance_handle;
        component_.runtime.route.appearance_pipeline_bucket = 0U;
        component_.runtime.route.appearance_resource_bucket = 0U;
        BumpAppearanceHandleMutationSerial();
        MarkDirty(component_, particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
        RebuildSortKey(component_);
    }

    [[nodiscard]] static bool SetAppearanceRuntimeLink(ParticleType& component_,
                                                       AppearanceHandle appearance_handle_,
                                                       std::uint64_t appearance_sort_key_,
                                                       std::uint64_t appearance_pipeline_key_,
                                                       std::uint64_t appearance_resource_key_) noexcept {
        (void)appearance_sort_key_;
        const std::uint32_t pipeline_bucket = static_cast<std::uint32_t>(appearance_pipeline_key_);
        const std::uint32_t resource_bucket = static_cast<std::uint32_t>(appearance_resource_key_);
        const bool changed =
            component_.runtime.route.appearance_handle.index != appearance_handle_.index ||
            component_.runtime.route.appearance_handle.generation != appearance_handle_.generation ||
            component_.runtime.route.appearance_pipeline_bucket != pipeline_bucket ||
            component_.runtime.route.appearance_resource_bucket != resource_bucket;
        if (!changed) {
            return false;
        }

        const bool handle_changed =
            component_.runtime.route.appearance_handle.index != appearance_handle_.index ||
            component_.runtime.route.appearance_handle.generation != appearance_handle_.generation;
        component_.runtime.route.appearance_handle = appearance_handle_;
        component_.runtime.route.appearance_pipeline_bucket = pipeline_bucket;
        component_.runtime.route.appearance_resource_bucket = resource_bucket;
        if (handle_changed) {
            BumpAppearanceHandleMutationSerial();
        }
        MarkDirty(component_, particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
        RebuildSortKey(component_);
        return true;
    }

    static void SetDepthBin(ParticleType& component_, std::uint16_t depth_bin_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.runtime.route.depth_bin == depth_bin_) {
            return;
        }
        component_.runtime.route.depth_bin = depth_bin_;
        MarkDirty(component_, particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
        RebuildSortKey(component_);
    }

    static void SetCapacity(ParticleType& component_,
                            std::uint32_t max_particles_,
                            std::uint16_t max_alive_per_frame_) noexcept {
        const std::uint32_t max_particles = std::max<std::uint32_t>(1U, max_particles_);
        const std::uint16_t max_alive_per_frame = std::max<std::uint16_t>(1U, max_alive_per_frame_);
        if (component_.style.max_particles == max_particles &&
            component_.style.max_alive_per_frame == max_alive_per_frame) {
            return;
        }
        component_.style.max_particles = max_particles;
        component_.style.max_alive_per_frame = max_alive_per_frame;
        MarkDirty(component_, particle_dirty_style_flag | particle_dirty_runtime_flag |
                              particle_dirty_simulation_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetFacingMode(ParticleType& component_,
                              ParticleFacingMode facing_mode_) noexcept {
        if (component_.style.facing_mode == facing_mode_) {
            return;
        }
        component_.style.facing_mode = facing_mode_;
        MarkDirty(component_, particle_dirty_style_flag | particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetStartEndColor(ParticleType& component_,
                                 Rgba8 start_color_,
                                 Rgba8 end_color_) noexcept {
        if (IsSameColor(component_.style.start_color, start_color_) &&
            IsSameColor(component_.style.end_color, end_color_)) {
            return;
        }
        component_.style.start_color = start_color_;
        component_.style.end_color = end_color_;
        MarkDirty(component_, particle_dirty_style_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetLayer(ParticleType& component_, std::int16_t layer_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.style.layer == layer_) {
            return;
        }
        component_.style.layer = layer_;
        MarkDirty(component_, particle_dirty_style_flag | particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
        RebuildSortKey(component_);
    }

    static void SetDepthState(ParticleType& component_,
                              bool depth_test_,
                              bool depth_write_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const std::uint8_t depth_test = depth_test_ ? 1U : 0U;
        const std::uint8_t depth_write = depth_write_ ? 1U : 0U;
        if (component_.style.depth_test == depth_test &&
            component_.style.depth_write == depth_write) {
            return;
        }
        component_.style.depth_test = depth_test;
        component_.style.depth_write = depth_write;
        MarkDirty(component_, particle_dirty_style_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetDoubleSided(ParticleType& component_, bool enabled_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const std::uint8_t enabled_value = enabled_ ? 1U : 0U;
        if (component_.style.double_sided == enabled_value) {
            return;
        }
        component_.style.double_sided = enabled_value;
        MarkDirty(component_, particle_dirty_style_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetBlendMode(ParticleType& component_,
                             ParticleBlendMode blend_mode_) noexcept {
        if (component_.style.blend_mode == blend_mode_) {
            return;
        }
        component_.style.blend_mode = blend_mode_;
        MarkDirty(component_, particle_dirty_style_flag | particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetSimulationMode(ParticleType& component_,
                                  ParticleSimulationMode simulation_mode_) noexcept {
        if (component_.style.simulation_mode == simulation_mode_) {
            return;
        }
        component_.style.simulation_mode = simulation_mode_;
        MarkDirty(component_, particle_dirty_style_flag | particle_dirty_runtime_flag |
                              particle_dirty_simulation_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetRenderMode(ParticleType& component_,
                              ParticleRenderMode render_mode_) noexcept {
        if (component_.style.render_mode == render_mode_) {
            return;
        }
        component_.style.render_mode = render_mode_;
        MarkDirty(component_, particle_dirty_style_flag | particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetSortMode(ParticleType& component_,
                            ParticleSortMode sort_mode_) noexcept {
        if (component_.style.sort_mode == sort_mode_) {
            return;
        }
        component_.style.sort_mode = sort_mode_;
        MarkDirty(component_, particle_dirty_style_flag | particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetLightingMode(ParticleType& component_,
                                ParticleLightingMode lighting_mode_) noexcept {
        if (component_.style.lighting_mode == lighting_mode_) {
            return;
        }
        component_.style.lighting_mode = lighting_mode_;
        MarkDirty(component_, particle_dirty_style_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetPremultipliedAlpha(ParticleType& component_, bool enabled_) noexcept {
        const std::uint8_t enabled_value = enabled_ ? 1U : 0U;
        if (component_.style.premultiplied_alpha == enabled_value) {
            return;
        }
        component_.style.premultiplied_alpha = enabled_value;
        MarkDirty(component_, particle_dirty_style_flag | particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetShadowFlags(ParticleType& component_,
                               bool receive_shadow_,
                               bool cast_shadow_) noexcept {
        const std::uint8_t receive_shadow = receive_shadow_ ? 1U : 0U;
        const std::uint8_t cast_shadow = cast_shadow_ ? 1U : 0U;
        if (component_.style.receive_shadow == receive_shadow &&
            component_.runtime.route.cast_shadow == cast_shadow) {
            return;
        }
        component_.style.receive_shadow = receive_shadow;
        component_.runtime.route.cast_shadow = cast_shadow;
        MarkDirty(component_, particle_dirty_style_flag | particle_dirty_runtime_flag);
        BumpAuthoringRevision(component_);
    }

    static void SetScalarStyle(ParticleType& component_,
                               float stretch_velocity_scale_,
                               float lod_bias_,
                               float screen_coverage_budget_,
                               float motion_blur_scale_,
                               float soft_particle_distance_) noexcept {
        const float stretch_velocity_scale = std::max(0.0F, stretch_velocity_scale_);
        const float screen_coverage_budget = std::max(0.0F, screen_coverage_budget_);
        const float motion_blur_scale = std::max(0.0F, motion_blur_scale_);
        const float soft_particle_distance = std::max(0.0F, soft_particle_distance_);
        if (component_.style.stretch_velocity_scale == stretch_velocity_scale &&
            component_.style.lod_bias == lod_bias_ &&
            component_.style.screen_coverage_budget == screen_coverage_budget &&
            component_.style.motion_blur_scale == motion_blur_scale &&
            component_.style.soft_particle_distance == soft_particle_distance) {
            return;
        }
        component_.style.stretch_velocity_scale = stretch_velocity_scale;
        component_.style.lod_bias = lod_bias_;
        component_.style.screen_coverage_budget = screen_coverage_budget;
        component_.style.motion_blur_scale = motion_blur_scale;
        component_.style.soft_particle_distance = soft_particle_distance;
        MarkDirty(component_, particle_dirty_style_flag);
        BumpAuthoringRevision(component_);
    }

    [[nodiscard]] static std::uint64_t ComposeSortKey(const ParticleType& component_) noexcept {
        const bool has_linked_appearance =
            component_.runtime.route.appearance_handle.index != invalid_appearance_handle.index &&
            component_.runtime.route.appearance_handle.generation != 0U;
        const ParticleRenderPassHint effective_pass_hint = has_linked_appearance
            ? ResolveLinkedPassHint(component_.runtime.route.pass_hint,
                                    component_.runtime.route.appearance_pipeline_bucket)
            : component_.runtime.route.pass_hint;
        const std::uint64_t pass_bits =
            static_cast<std::uint64_t>(SortPassBucket(effective_pass_hint)) & sort_key_pass_mask;
        const std::uint64_t material_bits =
            static_cast<std::uint64_t>(ResolveEffectiveMaterialId(component_.runtime.route)) &
            sort_key_material_mask;
        const std::uint64_t texture_bits =
            static_cast<std::uint64_t>(component_.runtime.route.texture_id) & sort_key_texture_mask;
        const std::uint64_t batch_bits =
            static_cast<std::uint64_t>(component_.runtime.route.batch_tag) & sort_key_batch_mask;

        std::uint64_t minor_bits = 0U;
        if constexpr (std::same_as<DimensionT, Dim2>) {
            const std::int32_t shifted_layer = static_cast<std::int32_t>(component_.style.layer) -
                                               static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min());
            minor_bits = static_cast<std::uint64_t>(static_cast<std::uint32_t>(shifted_layer)) &
                         sort_key_minor_mask;
        } else {
            minor_bits = static_cast<std::uint64_t>(
                EncodeDepthMinorBucket(component_.runtime.route.depth_bin, effective_pass_hint)) &
                sort_key_minor_mask;
        }

        std::uint64_t key = 0U;
        key |= (pass_bits << sort_key_pass_shift);
        key |= (material_bits << sort_key_material_shift);
        key |= (texture_bits << sort_key_texture_shift);
        key |= (minor_bits << sort_key_minor_shift);
        key |= (batch_bits << sort_key_batch_shift);
        return key;
    }

    static void RebuildSortKey(ParticleType& component_) noexcept {
        component_.runtime.route.sort_key = ComposeSortKey(component_);
    }

    [[nodiscard]] static std::uint64_t SortKey(const ParticleType& component_) noexcept {
        return component_.runtime.route.sort_key;
    }

    [[nodiscard]] static std::uint64_t BindingSortKey(const ParticleType& component_) noexcept {
        return BindingSortKey(component_.runtime.route.sort_key);
    }

    [[nodiscard]] static std::uint64_t BindingSortKey(std::uint64_t sort_key_) noexcept {
        return sort_key_ >> sort_key_binding_shift;
    }

    [[nodiscard]] static std::uint32_t ExtractPassBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>(PassHintFromSortBucket<ParticleRenderPassHint>(
            static_cast<std::uint32_t>((sort_key_ >> sort_key_pass_shift) & sort_key_pass_mask)));
    }

    [[nodiscard]] static std::uint32_t ExtractMaterialBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_material_shift) & sort_key_material_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractTextureBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_texture_shift) & sort_key_texture_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractMinorBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_minor_shift) & sort_key_minor_mask);
    }

    [[nodiscard]] static std::uint32_t ExtractBatchBucket(std::uint64_t sort_key_) noexcept {
        return static_cast<std::uint32_t>((sort_key_ >> sort_key_batch_shift) & sort_key_batch_mask);
    }

    [[nodiscard]] static bool IsVisibleForBuild(const ParticleType& component_) noexcept {
        return component_.runtime.route.visible != 0U &&
               component_.style.max_particles > 0U;
    }

    [[nodiscard]] static bool UsesTransparentPass(const ParticleType& component_) noexcept {
        const RuntimeBlendPreset preset = ResolveRuntimeBlendPreset(
            component_.style.blend_mode,
            component_.style.premultiplied_alpha != 0U);
        if (IsTransparentBlendPreset(preset)) {
            return true;
        }
        return component_.runtime.route.pass_hint == ParticleRenderPassHint::transparent;
    }

private:
    static void BumpAppearanceHandleMutationSerial() noexcept {
        (void)appearance_handle_mutation_serial.fetch_add(1U, std::memory_order_relaxed);
    }

    static void BumpAuthoringRevision(ParticleType& component_) noexcept {
        component_.runtime.revision_authoring =
            component_.runtime.revision_authoring == (std::numeric_limits<std::uint32_t>::max)()
                ? 1U
                : (component_.runtime.revision_authoring + 1U);
    }

    [[nodiscard]] static bool IsSameColor(const Rgba8& lhs_,
                                          const Rgba8& rhs_) noexcept {
        return lhs_.r == rhs_.r &&
               lhs_.g == rhs_.g &&
               lhs_.b == rhs_.b &&
               lhs_.a == rhs_.a;
    }

    inline static std::atomic<std::uint64_t> appearance_handle_mutation_serial{1U};
};

} // namespace vr::ecs

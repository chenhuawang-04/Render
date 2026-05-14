#pragma once

#include "vr/ecs/component/shadow_component.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <limits>

namespace vr::ecs {

[[nodiscard]] constexpr std::uint32_t NextShadowRevision(std::uint32_t current_revision_) noexcept {
    return (current_revision_ == (std::numeric_limits<std::uint32_t>::max)()) ? 1U : (current_revision_ + 1U);
}

template<DimensionTag DimensionT>
class ShadowSystem final {
public:
    using ShadowType = Shadow<DimensionT>;
    using StyleType = typename ShadowType::StyleType;
    using BindingType = typename ShadowType::BindingType;

    static void Initialize(ShadowType& component_) noexcept {
        SetDefaultStyle(component_);
        SetDefaultBinding(component_);
        SetDefaultRuntime(component_);
    }

    static void SetDefaultStyle(ShadowType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.style.map_width = 1024U;
            component_.style.map_height = 1024U;
            component_.style.max_distance = 512.0F;
            component_.style.depth_bias = 0.0015F;
            component_.style.normal_bias = 0.0F;
            component_.style.softness = 1.0F;
            component_.style.occluder_height = 64.0F;
            component_.style.blur_sigma = 1.0F;
            component_.style.layer = 0;
            component_.style.projection_kind = ShadowProjectionKind::directional;
            component_.style.filter_kernel = ShadowFilterKernel::pcf3x3;
            component_.style.fit_mode = ShadowFitMode::stable;
            component_.style.stabilize = 1U;
            component_.style.reverse_z = 0U;
            component_.style.reserved0 = 0U;
            component_.style.reserved1 = 0U;
        } else {
            component_.style.map_width = 2048U;
            component_.style.map_height = 2048U;
            component_.style.max_distance = 400.0F;
            component_.style.depth_bias = 0.0015F;
            component_.style.normal_bias = 0.0005F;
            component_.style.slope_scaled_bias = 1.25F;
            component_.style.cascade_lambda = 0.55F;
            component_.style.near_plane_offset = 0.0F;
            component_.style.far_plane_offset = 8.0F;
            component_.style.cascade_count = 4U;
            component_.style.face_count = 1U;
            component_.style.projection_kind = ShadowProjectionKind::directional;
            component_.style.filter_kernel = ShadowFilterKernel::pcf3x3;
            component_.style.fit_mode = ShadowFitMode::stable;
            component_.style.stabilize = 1U;
            component_.style.reverse_z = 0U;
            component_.style.reserved0 = 0U;
        }
    }

    static void SetDefaultBinding(ShadowType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.binding.light_component_index = invalid_shadow_index;
            component_.binding.transform_component_index = invalid_shadow_index;
            component_.binding.caster_mask = 0xFFFFFFFFU;
            component_.binding.receiver_mask = 0xFFFFFFFFU;
            component_.binding.atlas_namespace_id = 1U;
            component_.binding.atlas_policy = ShadowAtlasPolicy::packed;
            component_.binding.reserved0 = 0U;
            component_.binding.reserved1 = 0U;
        } else {
            component_.binding.light_component_index = invalid_shadow_index;
            component_.binding.transform_component_index = invalid_shadow_index;
            component_.binding.camera_component_index = 0U;
            component_.binding.caster_mask = 0xFFFFFFFFU;
            component_.binding.receiver_mask = 0xFFFFFFFFU;
            component_.binding.atlas_namespace_id = 1U;
            component_.binding.atlas_policy = ShadowAtlasPolicy::packed;
            component_.binding.reserved0 = 0U;
            component_.binding.reserved1 = 0U;
        }
    }

    static void SetDefaultRuntime(ShadowType& component_) noexcept {
        component_.state.revision_style = 1U;
        component_.state.revision_binding = 1U;
        component_.state.upload_revision = 0U;
        component_.state.dirty_flags = shadow_dirty_style_flag | shadow_dirty_binding_flag | shadow_dirty_runtime_flag;

        component_.gpu.pipeline_key = 0U;
        component_.gpu.resource_key = 0U;
        component_.gpu.sort_key = 0U;
        component_.gpu.gpu_record_index = invalid_shadow_index;
        component_.gpu.handle = invalid_shadow_handle;

        component_.atlas.atlas_namespace_id = component_.binding.atlas_namespace_id;
        component_.atlas.atlas_x = 0U;
        component_.atlas.atlas_y = 0U;
        component_.atlas.atlas_width = component_.style.map_width;
        component_.atlas.atlas_height = component_.style.map_height;
        component_.atlas.atlas_layer = 0U;
        component_.atlas.view_count = 1U;
        component_.atlas.reserved0 = 0U;

        component_.visibility.caster_mask = component_.binding.caster_mask;
        component_.visibility.receiver_mask = component_.binding.receiver_mask;
        component_.visibility.visible = 1U;
        component_.visibility.enabled = 1U;
        component_.visibility.reserved0 = 0U;
        component_.visibility.reserved1 = 0U;
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const ShadowType& component_) noexcept {
        return component_.state.dirty_flags;
    }

    [[nodiscard]] static bool HasDirtyFlags(const ShadowType& component_, std::uint32_t dirty_mask_) noexcept {
        return (component_.state.dirty_flags & dirty_mask_) != 0U;
    }

    static void MarkDirty(ShadowType& component_, std::uint32_t dirty_mask_) noexcept {
        component_.state.dirty_flags |= dirty_mask_;
    }

    static void ClearDirtyFlags(ShadowType& component_, std::uint32_t clear_mask_) noexcept {
        component_.state.dirty_flags &= ~clear_mask_;
    }

    [[nodiscard]] static bool IsEnabledForBuild(const ShadowType& component_) noexcept {
        return component_.visibility.enabled != 0U &&
               component_.visibility.visible != 0U &&
               component_.style.map_width > 0U &&
               component_.style.map_height > 0U;
    }

    static void SetEnabled(ShadowType& component_, bool enabled_) noexcept {
        const std::uint8_t value = enabled_ ? 1U : 0U;
        if (component_.visibility.enabled == value) {
            return;
        }
        component_.visibility.enabled = value;
        MarkRuntimeRevisionDirty(component_);
    }

    static void SetVisible(ShadowType& component_, bool visible_) noexcept {
        const std::uint8_t value = visible_ ? 1U : 0U;
        if (component_.visibility.visible == value) {
            return;
        }
        component_.visibility.visible = value;
        MarkRuntimeRevisionDirty(component_);
    }

    static void SetMapResolution(ShadowType& component_,
                                 std::uint16_t map_width_,
                                 std::uint16_t map_height_) noexcept {
        const std::uint16_t width = std::max<std::uint16_t>(128U, map_width_);
        const std::uint16_t height = std::max<std::uint16_t>(128U, map_height_);
        if (component_.style.map_width == width && component_.style.map_height == height) {
            return;
        }
        component_.style.map_width = width;
        component_.style.map_height = height;
        MarkStyleRevisionDirty(component_);
    }

    static void SetBias(ShadowType& component_,
                        float depth_bias_,
                        float normal_bias_) noexcept {
        const float depth_bias = std::max(0.0F, depth_bias_);
        const float normal_bias = std::max(0.0F, normal_bias_);
        if (component_.style.depth_bias == depth_bias && component_.style.normal_bias == normal_bias) {
            return;
        }
        component_.style.depth_bias = depth_bias;
        component_.style.normal_bias = normal_bias;
        MarkStyleRevisionDirty(component_);
    }

    static void SetFitMode(ShadowType& component_, ShadowFitMode fit_mode_) noexcept {
        if (component_.style.fit_mode == fit_mode_) {
            return;
        }
        component_.style.fit_mode = fit_mode_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetFilterKernel(ShadowType& component_, ShadowFilterKernel filter_kernel_) noexcept {
        if (component_.style.filter_kernel == filter_kernel_) {
            return;
        }
        component_.style.filter_kernel = filter_kernel_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetProjectionKind(ShadowType& component_, ShadowProjectionKind projection_kind_) noexcept {
        if (component_.style.projection_kind == projection_kind_) {
            return;
        }
        component_.style.projection_kind = projection_kind_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetStabilize(ShadowType& component_, bool stabilize_) noexcept {
        const std::uint8_t value = stabilize_ ? 1U : 0U;
        if (component_.style.stabilize == value) {
            return;
        }
        component_.style.stabilize = value;
        MarkStyleRevisionDirty(component_);
    }

    static void SetReverseZ(ShadowType& component_, bool reverse_z_) noexcept {
        const std::uint8_t value = reverse_z_ ? 1U : 0U;
        if (component_.style.reverse_z == value) {
            return;
        }
        component_.style.reverse_z = value;
        MarkStyleRevisionDirty(component_);
    }

    static void SetMaxDistance(ShadowType& component_, float max_distance_) noexcept {
        const float value = std::max(0.0F, max_distance_);
        if (component_.style.max_distance == value) {
            return;
        }
        component_.style.max_distance = value;
        MarkStyleRevisionDirty(component_);
    }

    static void SetBindingMasks(ShadowType& component_,
                                std::uint32_t caster_mask_,
                                std::uint32_t receiver_mask_) noexcept {
        if (component_.binding.caster_mask == caster_mask_ &&
            component_.binding.receiver_mask == receiver_mask_) {
            return;
        }
        component_.binding.caster_mask = caster_mask_;
        component_.binding.receiver_mask = receiver_mask_;
        component_.visibility.caster_mask = caster_mask_;
        component_.visibility.receiver_mask = receiver_mask_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetAtlasNamespace(ShadowType& component_,
                                  std::uint32_t atlas_namespace_id_) noexcept {
        if (component_.binding.atlas_namespace_id == atlas_namespace_id_) {
            return;
        }
        component_.binding.atlas_namespace_id = atlas_namespace_id_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetAtlasPolicy(ShadowType& component_,
                               ShadowAtlasPolicy atlas_policy_) noexcept {
        if (component_.binding.atlas_policy == atlas_policy_) {
            return;
        }
        component_.binding.atlas_policy = atlas_policy_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetLightComponentIndex(ShadowType& component_,
                                       std::uint32_t light_component_index_) noexcept {
        if (component_.binding.light_component_index == light_component_index_) {
            return;
        }
        component_.binding.light_component_index = light_component_index_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetTransformComponentIndex(ShadowType& component_,
                                           std::uint32_t transform_component_index_) noexcept {
        if (component_.binding.transform_component_index == transform_component_index_) {
            return;
        }
        component_.binding.transform_component_index = transform_component_index_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetCameraComponentIndex(ShadowType& component_,
                                        std::uint32_t camera_component_index_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        if (component_.binding.camera_component_index == camera_component_index_) {
            return;
        }
        component_.binding.camera_component_index = camera_component_index_;
        MarkBindingRevisionDirty(component_);
    }

    static void SetSoftness(ShadowType& component_,
                            float softness_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const float value = std::max(0.0F, softness_);
        if (component_.style.softness == value) {
            return;
        }
        component_.style.softness = value;
        MarkStyleRevisionDirty(component_);
    }

    static void SetOccluderHeight(ShadowType& component_,
                                  float occluder_height_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const float value = std::max(0.0F, occluder_height_);
        if (component_.style.occluder_height == value) {
            return;
        }
        component_.style.occluder_height = value;
        MarkStyleRevisionDirty(component_);
    }

    static void SetBlurSigma(ShadowType& component_,
                             float blur_sigma_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        const float value = std::max(0.0F, blur_sigma_);
        if (component_.style.blur_sigma == value) {
            return;
        }
        component_.style.blur_sigma = value;
        MarkStyleRevisionDirty(component_);
    }

    static void SetLayer(ShadowType& component_,
                         std::int16_t layer_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        if (component_.style.layer == layer_) {
            return;
        }
        component_.style.layer = layer_;
        MarkStyleRevisionDirty(component_);
    }

    static void SetCascadeConfig(ShadowType& component_,
                                 std::uint8_t cascade_count_,
                                 float cascade_lambda_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const std::uint8_t cascade_count = static_cast<std::uint8_t>(std::clamp<std::uint32_t>(cascade_count_, 1U, 4U));
        const float cascade_lambda = std::clamp(cascade_lambda_, 0.0F, 1.0F);
        if (component_.style.cascade_count == cascade_count && component_.style.cascade_lambda == cascade_lambda) {
            return;
        }
        component_.style.cascade_count = cascade_count;
        component_.style.cascade_lambda = cascade_lambda;
        MarkStyleRevisionDirty(component_);
    }

    static void SetDepthSlopeBias(ShadowType& component_,
                                  float slope_scaled_bias_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const float value = std::max(0.0F, slope_scaled_bias_);
        if (component_.style.slope_scaled_bias == value) {
            return;
        }
        component_.style.slope_scaled_bias = value;
        MarkStyleRevisionDirty(component_);
    }

    static void SetPlaneOffsets(ShadowType& component_,
                                float near_plane_offset_,
                                float far_plane_offset_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const float near_offset = near_plane_offset_;
        const float far_offset = std::max(0.0F, far_plane_offset_);
        if (component_.style.near_plane_offset == near_offset && component_.style.far_plane_offset == far_offset) {
            return;
        }
        component_.style.near_plane_offset = near_offset;
        component_.style.far_plane_offset = far_offset;
        MarkStyleRevisionDirty(component_);
    }

    static void SetFaceCount(ShadowType& component_,
                             std::uint8_t face_count_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        const std::uint8_t value = std::clamp<std::uint8_t>(face_count_, 1U, 6U);
        if (component_.style.face_count == value) {
            return;
        }
        component_.style.face_count = value;
        MarkStyleRevisionDirty(component_);
    }

    static void SetRuntimeKeys(ShadowType& component_,
                               std::uint64_t pipeline_key_,
                               std::uint64_t resource_key_,
                               std::uint64_t sort_key_) noexcept {
        component_.gpu.pipeline_key = pipeline_key_;
        component_.gpu.resource_key = resource_key_;
        component_.gpu.sort_key = sort_key_;
    }

    static void SetGpuRecordHandle(ShadowType& component_, ShadowHandle handle_) noexcept {
        component_.gpu.handle = handle_;
        component_.gpu.gpu_record_index = handle_.index;
    }

    static void SetAtlasRuntime(ShadowType& component_,
                                std::uint32_t atlas_namespace_id_,
                                std::uint16_t atlas_x_,
                                std::uint16_t atlas_y_,
                                std::uint16_t atlas_width_,
                                std::uint16_t atlas_height_,
                                std::uint16_t atlas_layer_,
                                std::uint8_t view_count_) noexcept {
        component_.atlas.atlas_namespace_id = atlas_namespace_id_;
        component_.atlas.atlas_x = atlas_x_;
        component_.atlas.atlas_y = atlas_y_;
        component_.atlas.atlas_width = atlas_width_;
        component_.atlas.atlas_height = atlas_height_;
        component_.atlas.atlas_layer = atlas_layer_;
        component_.atlas.view_count = view_count_;
        component_.atlas.reserved0 = 0U;
    }

    static void MarkUploaded(ShadowType& component_) noexcept {
        component_.state.upload_revision = NextShadowRevision(component_.state.upload_revision);
        ClearDirtyFlags(component_,
                        shadow_dirty_style_flag |
                            shadow_dirty_binding_flag |
                            shadow_dirty_runtime_flag);
    }

private:
    static void MarkStyleRevisionDirty(ShadowType& component_) noexcept {
        component_.state.revision_style = NextShadowRevision(component_.state.revision_style);
        MarkDirty(component_, shadow_dirty_style_flag);
    }

    static void MarkBindingRevisionDirty(ShadowType& component_) noexcept {
        component_.state.revision_binding = NextShadowRevision(component_.state.revision_binding);
        MarkDirty(component_, shadow_dirty_binding_flag);
    }

    static void MarkRuntimeRevisionDirty(ShadowType& component_) noexcept {
        component_.state.revision_style = NextShadowRevision(component_.state.revision_style);
        MarkDirty(component_, shadow_dirty_runtime_flag);
    }
};

} // namespace vr::ecs


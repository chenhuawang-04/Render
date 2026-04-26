#pragma once

#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/spatial_math.hpp"

#include <algorithm>
#include <concepts>
#include <cstdint>

namespace vr::ecs {

template<DimensionTag DimensionT>
class CameraSystem final {
public:
    using CameraType = Camera<DimensionT>;
    using TransformType = Transform<DimensionT>;

    static void Initialize(CameraType& component_) noexcept {
        SetDefaultStyle(component_);
        SetDefaultRuntime(component_);

        RebuildProjection(component_);
        component_.runtime.view_matrix = spatial_math::IdentityMatrix4x4();
        RebuildViewProjection(component_);
        component_.runtime.dirty_flags = 0U;
    }

    static void SetDefaultStyle(CameraType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.style.orthographic_height = 20.0F;
            component_.style.aspect_ratio = 16.0F / 9.0F;
            component_.style.near_plane = -100.0F;
            component_.style.far_plane = 100.0F;
            component_.style.zoom = 1.0F;
            component_.style.y_down = 0U;
            component_.style.reserved0 = 0U;
            component_.style.reserved1 = 0U;
            component_.style.viewport = CameraViewport{
                .origin_x = 0.0F,
                .origin_y = 0.0F,
                .width = 1.0F,
                .height = 1.0F,
            };
        } else {
            component_.style.projection_mode = CameraProjectionMode::perspective;
            component_.style.reverse_z = 0U;
            component_.style.reserved0 = 0U;
            component_.style.vertical_fov_radians = 60.0F * 0.01745329251994329577F;
            component_.style.orthographic_height = 20.0F;
            component_.style.aspect_ratio = 16.0F / 9.0F;
            component_.style.near_plane = 0.05F;
            component_.style.far_plane = 2000.0F;
            component_.style.viewport = CameraViewport{
                .origin_x = 0.0F,
                .origin_y = 0.0F,
                .width = 1.0F,
                .height = 1.0F,
            };
        }

        MarkDirty(component_, camera_dirty_projection_flag | camera_dirty_runtime_flag);
    }

    static void SetDefaultRuntime(CameraType& component_) noexcept {
        component_.runtime.view_matrix = spatial_math::IdentityMatrix4x4();
        component_.runtime.projection_matrix = spatial_math::IdentityMatrix4x4();
        component_.runtime.view_projection_matrix = spatial_math::IdentityMatrix4x4();
        component_.runtime.culling_mask = 0xFFFFFFFFU;
        component_.runtime.revision = 0U;
        component_.runtime.dirty_flags = camera_dirty_projection_flag |
                                         camera_dirty_view_flag |
                                         camera_dirty_runtime_flag;
        component_.runtime.reserved0 = 0U;
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const CameraType& component_) noexcept {
        return component_.runtime.dirty_flags;
    }

    [[nodiscard]] static bool HasDirtyFlags(const CameraType& component_,
                                            std::uint32_t dirty_mask_) noexcept {
        return (component_.runtime.dirty_flags & dirty_mask_) != 0U;
    }

    static void MarkDirty(CameraType& component_, std::uint32_t dirty_mask_) noexcept {
        component_.runtime.dirty_flags |= dirty_mask_;
    }

    static void ClearDirtyFlags(CameraType& component_, std::uint32_t clear_mask_) noexcept {
        component_.runtime.dirty_flags &= ~clear_mask_;
    }

    [[nodiscard]] static std::uint32_t Revision(const CameraType& component_) noexcept {
        return component_.runtime.revision;
    }

    static void SetCullingMask(CameraType& component_, std::uint32_t culling_mask_) noexcept {
        component_.runtime.culling_mask = culling_mask_;
        MarkDirty(component_, camera_dirty_runtime_flag);
    }

    static void SetAspectRatio(CameraType& component_, float aspect_ratio_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.style.aspect_ratio = std::max(1e-6F, aspect_ratio_);
        } else {
            component_.style.aspect_ratio = std::max(1e-6F, aspect_ratio_);
        }
        MarkDirty(component_, camera_dirty_projection_flag | camera_dirty_runtime_flag);
    }

    static void SetNearFar(CameraType& component_,
                           float near_plane_,
                           float far_plane_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            if (near_plane_ <= far_plane_) {
                component_.style.near_plane = near_plane_;
                component_.style.far_plane = far_plane_;
            } else {
                component_.style.near_plane = far_plane_;
                component_.style.far_plane = near_plane_;
            }
        } else {
            const float near_plane = std::max(1e-4F, near_plane_);
            const float far_plane = std::max(near_plane + 1e-3F, far_plane_);
            component_.style.near_plane = near_plane;
            component_.style.far_plane = far_plane;
        }

        MarkDirty(component_, camera_dirty_projection_flag | camera_dirty_runtime_flag);
    }

    static void SetViewport(CameraType& component_,
                            float origin_x_,
                            float origin_y_,
                            float width_,
                            float height_) noexcept {
        component_.style.viewport.origin_x = origin_x_;
        component_.style.viewport.origin_y = origin_y_;
        component_.style.viewport.width = std::max(1e-6F, width_);
        component_.style.viewport.height = std::max(1e-6F, height_);
        MarkDirty(component_, camera_dirty_runtime_flag);
    }

    static void SetOrthographicHeight(CameraType& component_,
                                      float orthographic_height_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            component_.style.orthographic_height = std::max(1e-3F, orthographic_height_);
        } else {
            component_.style.orthographic_height = std::max(1e-3F, orthographic_height_);
        }
        MarkDirty(component_, camera_dirty_projection_flag | camera_dirty_runtime_flag);
    }

    static void SetZoom(CameraType& component_, float zoom_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        component_.style.zoom = std::max(1e-3F, zoom_);
        MarkDirty(component_, camera_dirty_projection_flag | camera_dirty_runtime_flag);
    }

    static void SetYDown(CameraType& component_, bool y_down_) noexcept
    requires std::same_as<DimensionT, Dim2>
    {
        component_.style.y_down = y_down_ ? 1U : 0U;
        MarkDirty(component_, camera_dirty_projection_flag | camera_dirty_runtime_flag);
    }

    static void SetProjectionMode(CameraType& component_,
                                  CameraProjectionMode projection_mode_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.style.projection_mode = projection_mode_;
        MarkDirty(component_, camera_dirty_projection_flag | camera_dirty_runtime_flag);
    }

    static void SetVerticalFovRadians(CameraType& component_,
                                      float vertical_fov_radians_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.style.vertical_fov_radians = std::clamp(vertical_fov_radians_, 1e-3F, 3.13F);
        MarkDirty(component_, camera_dirty_projection_flag | camera_dirty_runtime_flag);
    }

    static void SetReverseZ(CameraType& component_, bool reverse_z_) noexcept
    requires std::same_as<DimensionT, Dim3>
    {
        component_.style.reverse_z = reverse_z_ ? 1U : 0U;
        MarkDirty(component_, camera_dirty_projection_flag | camera_dirty_runtime_flag);
    }

    static void RebuildProjection(CameraType& component_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            const float zoom = std::max(1e-3F, component_.style.zoom);
            const float half_height = std::max(1e-4F, 0.5F * component_.style.orthographic_height / zoom);
            const float half_width = half_height * std::max(1e-6F, component_.style.aspect_ratio);

            float bottom = -half_height;
            float top = half_height;
            if (component_.style.y_down != 0U) {
                std::swap(bottom, top);
            }

            component_.runtime.projection_matrix = spatial_math::BuildOrthographicProjection(-half_width,
                                                                                              half_width,
                                                                                              bottom,
                                                                                              top,
                                                                                              component_.style.near_plane,
                                                                                              component_.style.far_plane);
        } else {
            if (component_.style.projection_mode == CameraProjectionMode::orthographic) {
                const float half_height = std::max(1e-4F, 0.5F * component_.style.orthographic_height);
                const float half_width = half_height * std::max(1e-6F, component_.style.aspect_ratio);

                component_.runtime.projection_matrix = spatial_math::BuildOrthographicProjection(-half_width,
                                                                                                  half_width,
                                                                                                  -half_height,
                                                                                                  half_height,
                                                                                                  component_.style.near_plane,
                                                                                                  component_.style.far_plane);
            } else {
                component_.runtime.projection_matrix = spatial_math::BuildPerspectiveProjection(component_.style.vertical_fov_radians,
                                                                                                component_.style.aspect_ratio,
                                                                                                component_.style.near_plane,
                                                                                                component_.style.far_plane,
                                                                                                component_.style.reverse_z != 0U);
            }
        }

        ++component_.runtime.revision;
        component_.runtime.dirty_flags &= ~camera_dirty_projection_flag;
        component_.runtime.dirty_flags |= camera_dirty_runtime_flag;
    }

    static void RebuildViewFromTransform(CameraType& component_,
                                         const TransformType& transform_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            Affine2x3 inverse_affine{};
            (void)spatial_math::InvertAffine2x3(transform_.runtime.world_matrix, inverse_affine);
            component_.runtime.view_matrix = spatial_math::Affine2x3ToMatrix4x4(inverse_affine);
        } else {
            Matrix4x4 inverse_world{};
            (void)spatial_math::InvertAffineMatrix4x4(transform_.runtime.world_matrix, inverse_world);
            component_.runtime.view_matrix = inverse_world;
        }

        ++component_.runtime.revision;
        component_.runtime.dirty_flags &= ~camera_dirty_view_flag;
        component_.runtime.dirty_flags |= camera_dirty_runtime_flag;
    }

    static void RebuildViewProjection(CameraType& component_) noexcept {
        component_.runtime.view_projection_matrix =
            spatial_math::MultiplyMatrix4x4(component_.runtime.projection_matrix,
                                            component_.runtime.view_matrix);
        ++component_.runtime.revision;
        component_.runtime.dirty_flags &= ~camera_dirty_runtime_flag;
    }

    static void Update(CameraType& component_, const TransformType& transform_) noexcept {
        if (HasDirtyFlags(component_, camera_dirty_projection_flag)) {
            RebuildProjection(component_);
        }
        if (HasDirtyFlags(component_, camera_dirty_view_flag)) {
            RebuildViewFromTransform(component_, transform_);
        }
        if (HasDirtyFlags(component_, camera_dirty_runtime_flag)) {
            RebuildViewProjection(component_);
        }
    }

    static void UpdateAligned(CameraType* cameras_,
                              const TransformType* transforms_,
                              std::uint32_t component_count_) noexcept {
        if (cameras_ == nullptr || transforms_ == nullptr || component_count_ == 0U) {
            return;
        }

        for (std::uint32_t i = 0U; i < component_count_; ++i) {
            Update(cameras_[i], transforms_[i]);
        }
    }

    static void MarkViewDirty(CameraType& component_) noexcept {
        MarkDirty(component_, camera_dirty_view_flag | camera_dirty_runtime_flag);
    }
};

} // namespace vr::ecs

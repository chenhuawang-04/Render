#pragma once

#include "vr/ecs/component/spatial_types.hpp"
#include "vr/ecs/concept/dimension.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::ecs {

enum class CameraProjectionMode : std::uint8_t {
    orthographic = 0U,
    perspective = 1U,
};

enum CameraDirtyFlags : std::uint32_t {
    camera_dirty_projection_flag = 1U << 0U,
    camera_dirty_view_flag = 1U << 1U,
    camera_dirty_runtime_flag = 1U << 2U,
};

struct CameraViewport final {
    float origin_x;
    float origin_y;
    float width;
    float height;
};

struct CameraStyle2D final {
    float orthographic_height;
    float aspect_ratio;
    float near_plane;
    float far_plane;
    float zoom;
    std::uint8_t y_down;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
    CameraViewport viewport;
};

struct CameraStyle3D final {
    CameraProjectionMode projection_mode;
    std::uint8_t reverse_z;
    std::uint16_t reserved0;
    float vertical_fov_radians;
    float orthographic_height;
    float aspect_ratio;
    float near_plane;
    float far_plane;
    CameraViewport viewport;
};

struct CameraRuntimeData final {
    Matrix4x4 view_matrix;
    Matrix4x4 projection_matrix;
    Matrix4x4 view_projection_matrix;
    std::uint32_t culling_mask;
    std::uint32_t revision;
    std::uint32_t dirty_flags;
    std::uint32_t reserved0;
};

template<DimensionTag DimensionT>
struct CameraComponent;

template<>
struct CameraComponent<Dim2> final {
    using StyleType = CameraStyle2D;

    StyleType style;
    CameraRuntimeData runtime;
};

template<>
struct CameraComponent<Dim3> final {
    using StyleType = CameraStyle3D;

    StyleType style;
    CameraRuntimeData runtime;
};

template<DimensionTag DimensionT>
using Camera = CameraComponent<DimensionT>;

static_assert(std::is_standard_layout_v<CameraViewport> &&
              std::is_trivial_v<CameraViewport>);
static_assert(std::is_standard_layout_v<CameraStyle2D> &&
              std::is_trivial_v<CameraStyle2D>);
static_assert(std::is_standard_layout_v<CameraStyle3D> &&
              std::is_trivial_v<CameraStyle3D>);
static_assert(std::is_standard_layout_v<CameraRuntimeData> &&
              std::is_trivial_v<CameraRuntimeData>);
static_assert(std::is_standard_layout_v<Camera<Dim2>> &&
              std::is_trivial_v<Camera<Dim2>>);
static_assert(std::is_standard_layout_v<Camera<Dim3>> &&
              std::is_trivial_v<Camera<Dim3>>);

} // namespace vr::ecs


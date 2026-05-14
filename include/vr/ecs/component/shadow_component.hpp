#pragma once

#include "vr/ecs/component/spatial_types.hpp"
#include "vr/ecs/concept/dimension.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>

namespace vr::ecs {

enum ShadowDirtyFlags : std::uint32_t {
    shadow_dirty_style_flag = 1U << 0U,
    shadow_dirty_binding_flag = 1U << 1U,
    shadow_dirty_runtime_flag = 1U << 2U,
};

enum class ShadowProjectionKind : std::uint8_t {
    directional = 0U,
    spot = 1U,
    point = 2U,
};

enum class ShadowFilterKernel : std::uint8_t {
    hard = 0U,
    pcf3x3 = 1U,
    pcf5x5 = 2U,
};

enum class ShadowFitMode : std::uint8_t {
    stable = 0U,
    tight = 1U,
};

enum class ShadowAtlasPolicy : std::uint8_t {
    packed = 0U,
    dedicated = 1U,
};

struct ShadowHandle final {
    std::uint32_t index;
    std::uint32_t generation;
};

inline constexpr std::uint32_t invalid_shadow_index = (std::numeric_limits<std::uint32_t>::max)();
inline constexpr ShadowHandle invalid_shadow_handle{
    .index = invalid_shadow_index,
    .generation = 0U,
};

struct ShadowRuntimeState final {
    std::uint32_t revision_style;
    std::uint32_t revision_binding;
    std::uint32_t upload_revision;
    std::uint32_t dirty_flags;
};

struct ShadowRuntimeGpu final {
    std::uint64_t pipeline_key;
    std::uint64_t resource_key;
    std::uint64_t sort_key;
    std::uint32_t gpu_record_index;
    ShadowHandle handle;
};

struct ShadowRuntimeAtlas final {
    std::uint32_t atlas_namespace_id;
    std::uint16_t atlas_x;
    std::uint16_t atlas_y;
    std::uint16_t atlas_width;
    std::uint16_t atlas_height;
    std::uint16_t atlas_layer;
    std::uint8_t view_count;
    std::uint8_t reserved0;
};

struct ShadowRuntimeVisibility final {
    std::uint32_t caster_mask;
    std::uint32_t receiver_mask;
    std::uint8_t visible;
    std::uint8_t enabled;
    std::uint8_t reserved0;
    std::uint8_t reserved1;
};

struct ShadowStyle2D final {
    std::uint16_t map_width;
    std::uint16_t map_height;
    float max_distance;
    float depth_bias;
    float normal_bias;
    float softness;
    float occluder_height;
    float blur_sigma;
    std::int16_t layer;
    ShadowProjectionKind projection_kind;
    ShadowFilterKernel filter_kernel;
    ShadowFitMode fit_mode;
    std::uint8_t stabilize;
    std::uint8_t reverse_z;
    std::uint8_t reserved0;
    std::uint8_t reserved1;
};

struct ShadowStyle3D final {
    std::uint16_t map_width;
    std::uint16_t map_height;
    float max_distance;
    float depth_bias;
    float normal_bias;
    float slope_scaled_bias;
    float cascade_lambda;
    float near_plane_offset;
    float far_plane_offset;
    std::uint8_t cascade_count;
    std::uint8_t face_count;
    ShadowProjectionKind projection_kind;
    ShadowFilterKernel filter_kernel;
    ShadowFitMode fit_mode;
    std::uint8_t stabilize;
    std::uint8_t reverse_z;
    std::uint8_t reserved0;
};

struct ShadowBinding2D final {
    std::uint32_t light_component_index;
    std::uint32_t transform_component_index;
    std::uint32_t caster_mask;
    std::uint32_t receiver_mask;
    std::uint32_t atlas_namespace_id;
    ShadowAtlasPolicy atlas_policy;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
};

struct ShadowBinding3D final {
    std::uint32_t light_component_index;
    std::uint32_t transform_component_index;
    std::uint32_t camera_component_index;
    std::uint32_t caster_mask;
    std::uint32_t receiver_mask;
    std::uint32_t atlas_namespace_id;
    ShadowAtlasPolicy atlas_policy;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
};

template<DimensionTag DimensionT>
struct ShadowComponent;

template<>
struct ShadowComponent<Dim2> final {
    using StyleType = ShadowStyle2D;
    using BindingType = ShadowBinding2D;

    StyleType style;
    BindingType binding;
    ShadowRuntimeState state;
    ShadowRuntimeGpu gpu;
    ShadowRuntimeAtlas atlas;
    ShadowRuntimeVisibility visibility;
};

template<>
struct ShadowComponent<Dim3> final {
    using StyleType = ShadowStyle3D;
    using BindingType = ShadowBinding3D;

    StyleType style;
    BindingType binding;
    ShadowRuntimeState state;
    ShadowRuntimeGpu gpu;
    ShadowRuntimeAtlas atlas;
    ShadowRuntimeVisibility visibility;
};

template<DimensionTag DimensionT>
using Shadow = ShadowComponent<DimensionT>;

template<typename T>
concept PurePodShadowComponent = std::is_standard_layout_v<T> &&
                                 std::is_trivial_v<T>;

static_assert(PurePodShadowComponent<ShadowHandle>);
static_assert(PurePodShadowComponent<ShadowRuntimeState>);
static_assert(PurePodShadowComponent<ShadowRuntimeGpu>);
static_assert(PurePodShadowComponent<ShadowRuntimeAtlas>);
static_assert(PurePodShadowComponent<ShadowRuntimeVisibility>);
static_assert(PurePodShadowComponent<ShadowStyle2D>);
static_assert(PurePodShadowComponent<ShadowStyle3D>);
static_assert(PurePodShadowComponent<ShadowBinding2D>);
static_assert(PurePodShadowComponent<ShadowBinding3D>);
static_assert(PurePodShadowComponent<Shadow<Dim2>>);
static_assert(PurePodShadowComponent<Shadow<Dim3>>);

} // namespace vr::ecs


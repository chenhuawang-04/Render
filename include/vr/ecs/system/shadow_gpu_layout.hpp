#pragma once

#include "vr/ecs/component/shadow_component.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::ecs {

inline constexpr std::uint32_t max_shadow_cascade_count = 4U;
inline constexpr std::uint32_t max_shadow_view_count = 6U;

struct alignas(16) ShadowViewGpuRecord final {
    Matrix4x4 view_matrix;
    Matrix4x4 projection_matrix;
    Matrix4x4 view_projection_matrix;

    float split_near;
    float split_far;
    float depth_bias;
    float normal_bias;

    float slope_scaled_bias;
    float texel_world_size;
    std::uint32_t atlas_namespace_id;
    std::uint32_t shadow_component_index;

    std::uint32_t atlas_x;
    std::uint32_t atlas_y;
    std::uint32_t atlas_width;
    std::uint32_t atlas_height;

    std::uint32_t atlas_layer;
    std::uint32_t view_index;
    std::uint32_t cascade_index;
    std::uint32_t flags;
};

struct alignas(16) ShadowGpuRecord2D final {
    float max_distance;
    float softness;
    float occluder_height;
    float blur_sigma;

    std::uint32_t first_view_index;
    std::uint32_t view_count;
    std::uint32_t caster_mask;
    std::uint32_t receiver_mask;

    std::uint32_t projection_kind;
    std::uint32_t filter_kernel;
    std::uint32_t fit_mode;
    std::uint32_t flags;

    std::uint32_t atlas_namespace_id;
    std::uint32_t atlas_policy;
    std::int32_t layer;
    std::uint32_t reserved0;
};

struct alignas(16) ShadowGpuRecord3D final {
    float max_distance;
    float cascade_lambda;
    float near_plane_offset;
    float far_plane_offset;

    std::uint32_t first_view_index;
    std::uint32_t view_count;
    std::uint32_t caster_mask;
    std::uint32_t receiver_mask;

    std::uint32_t projection_kind;
    std::uint32_t filter_kernel;
    std::uint32_t fit_mode;
    std::uint32_t flags;

    std::uint32_t atlas_namespace_id;
    std::uint32_t atlas_policy;
    std::uint32_t reserved0;
    std::uint32_t reserved1;
};

template<DimensionTag DimensionT>
struct ShadowGpuRecordTypeTraits;

template<>
struct ShadowGpuRecordTypeTraits<Dim2> final {
    using RecordType = ShadowGpuRecord2D;
};

template<>
struct ShadowGpuRecordTypeTraits<Dim3> final {
    using RecordType = ShadowGpuRecord3D;
};

template<DimensionTag DimensionT>
using ShadowGpuRecord = typename ShadowGpuRecordTypeTraits<DimensionT>::RecordType;

static_assert(std::is_standard_layout_v<ShadowViewGpuRecord> && std::is_trivial_v<ShadowViewGpuRecord>);
static_assert(std::is_standard_layout_v<ShadowGpuRecord2D> && std::is_trivial_v<ShadowGpuRecord2D>);
static_assert(std::is_standard_layout_v<ShadowGpuRecord3D> && std::is_trivial_v<ShadowGpuRecord3D>);
static_assert(sizeof(ShadowViewGpuRecord) % 16U == 0U);
static_assert(sizeof(ShadowGpuRecord2D) % 16U == 0U);
static_assert(sizeof(ShadowGpuRecord3D) % 16U == 0U);

} // namespace vr::ecs

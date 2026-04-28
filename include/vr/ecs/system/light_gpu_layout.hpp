#pragma once

#include "vr/ecs/component/light_component.hpp"

#include <cstdint>
#include <type_traits>

namespace vr::ecs {

struct alignas(16) LightGpuRecord2D final {
    float position_x;
    float position_y;
    float radius;
    float intensity;

    float color_r;
    float color_g;
    float color_b;
    float falloff_exponent;

    float direction_x;
    float direction_y;
    float cone_cos_outer;
    float cone_cos_inner;

    float source_height;
    std::uint32_t light_type;
    std::uint32_t channel_mask;
    std::uint32_t flags;
};

struct alignas(16) LightGpuRecord3D final {
    float position_x;
    float position_y;
    float position_z;
    float radius;

    float color_r;
    float color_g;
    float color_b;
    float intensity;

    float direction_x;
    float direction_y;
    float direction_z;
    float cone_cos_outer;

    float cone_cos_inner;
    float source_radius;
    float source_length;
    float source_height;

    float falloff_exponent;
    float volumetric_strength;
    std::uint32_t light_type;
    std::uint32_t channel_mask;

    std::uint32_t flags;
    std::uint32_t reserved0;
    std::uint32_t reserved1;
    std::uint32_t reserved2;
};

template<DimensionTag DimensionT>
struct LightGpuRecordTypeTraits;

template<>
struct LightGpuRecordTypeTraits<Dim2> final {
    using RecordType = LightGpuRecord2D;
};

template<>
struct LightGpuRecordTypeTraits<Dim3> final {
    using RecordType = LightGpuRecord3D;
};

template<DimensionTag DimensionT>
using LightGpuRecord = typename LightGpuRecordTypeTraits<DimensionT>::RecordType;

static_assert(std::is_standard_layout_v<LightGpuRecord2D> && std::is_trivial_v<LightGpuRecord2D>);
static_assert(std::is_standard_layout_v<LightGpuRecord3D> && std::is_trivial_v<LightGpuRecord3D>);
static_assert(sizeof(LightGpuRecord2D) % 16U == 0U);
static_assert(sizeof(LightGpuRecord3D) % 16U == 0U);

} // namespace vr::ecs


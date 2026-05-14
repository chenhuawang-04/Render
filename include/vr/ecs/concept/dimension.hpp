#pragma once

#include <concepts>
#include <cstdint>

namespace vr::ecs {

enum class SceneDimension : std::uint8_t {
    dim2 = 0U,
    dim3 = 1U,
};

struct Dim2 final {
    static constexpr SceneDimension value = SceneDimension::dim2;
};

struct Dim3 final {
    static constexpr SceneDimension value = SceneDimension::dim3;
};

template<typename DimensionT>
concept DimensionTag = std::same_as<DimensionT, Dim2> ||
                       std::same_as<DimensionT, Dim3>;

template<DimensionTag DimensionT>
inline constexpr SceneDimension scene_dimension_v = DimensionT::value;

} // namespace vr::ecs


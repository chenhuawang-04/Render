#pragma once

#include "vr/ecs/concept/dimension.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace vr::geometry {

struct alignas(16) MaterialGpuRecord final {
    std::array<float, 4U> base_rgba{};
    std::array<float, 4U> emissive_rgba{};
    std::array<float, 4U> material_params{};
    std::array<float, 4U> extras{};
    std::array<std::uint32_t, 4U> flags_u32{};
    std::array<std::uint32_t, 4U> textures0_u32{};
    std::array<std::uint32_t, 4U> textures1_u32{};
};

inline constexpr std::uint32_t invalid_material_record_index =
    (std::numeric_limits<std::uint32_t>::max)();

enum MaterialTexturePresenceFlags : std::uint32_t {
    material_texture_presence_base_color = 1U << 0U,
    material_texture_presence_normal = 1U << 1U,
    material_texture_presence_metal_rough = 1U << 2U,
    material_texture_presence_occlusion = 1U << 3U,
    material_texture_presence_emissive = 1U << 4U,
};

} // namespace vr::geometry

static_assert(alignof(vr::geometry::MaterialGpuRecord) == 16U);
static_assert((sizeof(vr::geometry::MaterialGpuRecord) % 16U) == 0U);
static_assert(std::is_standard_layout_v<vr::geometry::MaterialGpuRecord>);
static_assert(std::is_trivially_copyable_v<vr::geometry::MaterialGpuRecord>);

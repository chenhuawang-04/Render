#pragma once

#include "vr/ecs/component/appearance_component.hpp"
#include "vr/ecs/component/spatial_types.hpp"
#include "vr/ecs/component/text_component.hpp"
#include "vr/ecs/concept/dimension.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>

namespace vr::ecs {

enum ParticleDirtyFlags : std::uint32_t {
    particle_dirty_style_flag = 1U << 0U,
    particle_dirty_emitter_flag = 1U << 1U,
    particle_dirty_runtime_flag = 1U << 2U,
    particle_dirty_simulation_flag = 1U << 3U,
};

enum class ParticleRenderPassHint : std::uint8_t {
    overlay = 0U,
    opaque = 1U,
    transparent = 2U,
};

enum class ParticleSimulationMode : std::uint8_t {
    cpu = 0U,
    gpu = 1U,
    hybrid_gpu = 2U,
};

enum class ParticleRenderMode : std::uint8_t {
    billboard = 0U,
    axis_aligned = 1U,
    mesh = 2U,
    trail = 3U,
};

enum class ParticleSortMode : std::uint8_t {
    none = 0U,
    by_view_depth = 1U,
    bucket = 2U,
    gpu_radix = 3U,
    weighted_blended_oit = 4U,
};

enum class ParticleLightingMode : std::uint8_t {
    unlit = 0U,
    approximate_lit = 1U,
    clustered_lit = 2U,
};

enum class ParticleSimulationSpace : std::uint8_t {
    local = 0U,
    world = 1U,
};

enum class ParticleEmitterShape : std::uint8_t {
    point = 0U,
    box = 1U,
    circle = 2U,
    sphere = 3U,
    cone = 4U,
};

enum class ParticleFacingMode : std::uint8_t {
    screen = 0U,
    velocity = 1U,
    local = 2U,
};

enum class ParticleBlendMode : std::uint8_t {
    alpha = 0U,
    additive = 1U,
    multiply = 2U,
    premultiplied_alpha = 3U,
    screen = 4U,
};

struct ParticleRuntimeRoute final {
    std::uint64_t sort_key;
    std::uint32_t material_id;
    std::uint32_t texture_id;
    std::uint32_t batch_tag;
    std::uint32_t user_data;
    AppearanceHandle appearance_handle;
    std::uint32_t appearance_pipeline_bucket;
    std::uint32_t appearance_resource_bucket;
    std::uint16_t depth_bin;
    std::uint8_t visible;
    std::uint8_t cast_shadow;
    ParticleRenderPassHint pass_hint;
    std::uint32_t dirty_flags;
};

struct ParticleStyle2D final {
    std::uint32_t max_particles;
    std::uint16_t max_alive_per_frame;
    ParticleSimulationMode simulation_mode;
    ParticleRenderMode render_mode;
    ParticleSortMode sort_mode;
    ParticleLightingMode lighting_mode;
    std::uint8_t receive_shadow;
    std::uint8_t premultiplied_alpha;
    ParticleFacingMode facing_mode;
    ParticleBlendMode blend_mode;
    std::uint16_t reserved0;
    std::int16_t layer;
    Rgba8 start_color;
    Rgba8 end_color;
    float stretch_velocity_scale;
    float soft_particle_distance;
    float lod_bias;
    float screen_coverage_budget;
    float motion_blur_scale;
};

struct ParticleStyle3D final {
    std::uint32_t max_particles;
    std::uint16_t max_alive_per_frame;
    ParticleSimulationMode simulation_mode;
    ParticleRenderMode render_mode;
    ParticleSortMode sort_mode;
    ParticleLightingMode lighting_mode;
    std::uint8_t receive_shadow;
    std::uint8_t premultiplied_alpha;
    ParticleFacingMode facing_mode;
    ParticleBlendMode blend_mode;
    std::uint8_t depth_test;
    std::uint8_t depth_write;
    std::uint8_t double_sided;
    std::uint8_t reserved0;
    Rgba8 start_color;
    Rgba8 end_color;
    float stretch_velocity_scale;
    float soft_particle_distance;
    float lod_bias;
    float screen_coverage_budget;
    float motion_blur_scale;
};

struct ParticleRuntimeCommon final {
    ParticleRuntimeRoute route;
    std::uint32_t emitter_handle;
    std::uint32_t pool_handle;
    std::uint32_t active_count;
    std::uint32_t visible_count;
    std::uint32_t revision_authoring;
    std::uint32_t revision_simulation;
    std::uint32_t last_visible_set_revision;
    std::uint32_t reserved0;
};

template<DimensionTag DimensionT>
struct ParticleComponent;

template<>
struct ParticleComponent<Dim2> final {
    using StyleType = ParticleStyle2D;
    using RuntimeType = ParticleRuntimeCommon;

    StyleType style;
    RuntimeType runtime;
};

template<>
struct ParticleComponent<Dim3> final {
    using StyleType = ParticleStyle3D;
    using RuntimeType = ParticleRuntimeCommon;

    StyleType style;
    RuntimeType runtime;
};

template<DimensionTag DimensionT>
using Particle = ParticleComponent<DimensionT>;

inline constexpr std::uint32_t invalid_particle_pool_handle =
    (std::numeric_limits<std::uint32_t>::max)();
inline constexpr std::uint32_t invalid_particle_emitter_handle =
    (std::numeric_limits<std::uint32_t>::max)();

inline constexpr std::uint32_t particle_pipeline_state_blend_shift = 0U;
inline constexpr std::uint32_t particle_pipeline_state_blend_mask = 0xFFU;
inline constexpr std::uint32_t particle_pipeline_state_depth_test_shift = 8U;
inline constexpr std::uint32_t particle_pipeline_state_depth_write_shift = 9U;
inline constexpr std::uint32_t particle_pipeline_state_double_sided_shift = 10U;
inline constexpr std::uint32_t particle_pipeline_state_facing_mode_shift = 11U;
inline constexpr std::uint32_t particle_pipeline_state_facing_mode_mask = 0x7U;
inline constexpr std::uint32_t particle_pipeline_state_render_mode_shift = 14U;
inline constexpr std::uint32_t particle_pipeline_state_render_mode_mask = 0x3U;
inline constexpr std::uint32_t particle_pipeline_state_lighting_mode_shift = 16U;
inline constexpr std::uint32_t particle_pipeline_state_lighting_mode_mask = 0x3U;

template<typename T>
concept PurePodParticleComponent = std::is_standard_layout_v<T> &&
                                   std::is_trivial_v<T>;

static_assert(PurePodParticleComponent<ParticleRuntimeRoute>);
static_assert(PurePodParticleComponent<ParticleStyle2D>);
static_assert(PurePodParticleComponent<ParticleStyle3D>);
static_assert(PurePodParticleComponent<ParticleRuntimeCommon>);
static_assert(PurePodParticleComponent<Particle<Dim2>>);
static_assert(PurePodParticleComponent<Particle<Dim3>>);
static_assert(sizeof(ParticleRuntimeRoute) <= 64U);

} // namespace vr::ecs

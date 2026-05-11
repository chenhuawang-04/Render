#include "vr/particle/particle_simulation_host.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/particle/generated/particle_build_2d_comp_spv.hpp"
#include "vr/particle/generated/particle_build_3d_comp_spv.hpp"
#include "vr/particle/generated/particle_sort_3d_comp_spv.hpp"
#include "vr/particle/generated/particle_update_2d_comp_spv.hpp"
#include "vr/particle/generated/particle_update_3d_comp_spv.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace vr::particle {

namespace {

constexpr VkBufferUsageFlags k_state_buffer_usage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

constexpr std::uint32_t k_particle_gpu_flag_alive = 1U << 0U;

constexpr VkBufferUsageFlags k_list_buffer_usage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

constexpr VkBufferUsageFlags k_spawn_buffer_usage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT;

[[nodiscard]] std::uint32_t ResolveBindlessTextureSlot(
    const asset::TextureHost* texture_host_,
    const render::BindlessResourceSystem& bindless_resources_,
    std::uint32_t texture_id_) noexcept {
    if (texture_id_ == 0U || texture_host_ == nullptr || !texture_host_->IsInitialized()) {
        return bindless_resources_.PlaceholderImageSlot().index;
    }
    return bindless_resources_.ResolveTextureImageSlot(*texture_host_,
                                                       asset::TextureId{texture_id_}).index;
}

[[nodiscard]] std::uint32_t ResolveBindlessSamplerSlot(
    const asset::TextureHost* texture_host_,
    const render::BindlessResourceSystem& bindless_resources_,
    std::uint32_t texture_id_) noexcept {
    if (texture_id_ == 0U || texture_host_ == nullptr || !texture_host_->IsInitialized()) {
        return bindless_resources_.DefaultSamplerSlot().index;
    }
    return bindless_resources_.ResolveTextureSamplerSlot(*texture_host_,
                                                         asset::TextureId{texture_id_}).index;
}

constexpr VkBufferUsageFlags k_draw_instance_buffer_usage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT;

constexpr VkBufferUsageFlags k_indirect_buffer_usage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT;

constexpr VkBufferUsageFlags k_sort_buffer_usage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
    VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

constexpr std::uint32_t k_particle_build_group_size = 64U;
constexpr std::uint32_t k_particle_build_flag_enable_sort = 0x80000000U;
constexpr std::uint32_t k_particle_build_flag_batch_count_mask = 0x7FFFFFFFU;

struct ParticleBuildPushConstants final {
    std::uint32_t record_count = 0U;
    std::uint32_t spawn_packet_count = 0U;
    float delta_time_s = 0.0F;
    std::uint32_t flags = 0U;
    float sort_origin_x = 0.0F;
    float sort_origin_y = 0.0F;
    float sort_origin_z = 0.0F;
    float sort_direction_x = 0.0F;
    float sort_direction_y = 0.0F;
    float sort_direction_z = -1.0F;
};

static_assert(sizeof(ParticleBuildPushConstants) == 40U);

struct ParticleSortPushConstants final {
    std::uint32_t segment_capacity = 0U;
    std::uint32_t batch_index = 0U;
    float reserved0 = 0.0F;
    std::uint32_t flags = 0U;
};

static_assert(sizeof(ParticleSortPushConstants) == 16U);

[[nodiscard]] constexpr bool Is2DStride(std::uint32_t stride_) noexcept {
    return stride_ == sizeof(ecs::Particle2DGpuInstance);
}

template<typename TransformT>
[[nodiscard]] ecs::Float3 TransformPointForParticle(const TransformT& transform_,
                                                    const ecs::Float3& local_point_) noexcept {
    if constexpr (std::same_as<TransformT, ecs::Transform<ecs::Dim2>>) {
        const auto& m = transform_.runtime.world_matrix;
        return ecs::Float3{
            .x = m.m00 * local_point_.x + m.m01 * local_point_.y + m.m02,
            .y = m.m10 * local_point_.x + m.m11 * local_point_.y + m.m12,
            .z = 0.0F,
        };
    } else {
        const auto& m = transform_.runtime.world_matrix.m;
        return ecs::Float3{
            .x = m[0] * local_point_.x + m[4] * local_point_.y + m[8] * local_point_.z + m[12],
            .y = m[1] * local_point_.x + m[5] * local_point_.y + m[9] * local_point_.z + m[13],
            .z = m[2] * local_point_.x + m[6] * local_point_.y + m[10] * local_point_.z + m[14],
        };
    }
}

template<typename TransformT>
[[nodiscard]] ecs::Float3 TransformVectorForParticle(const TransformT& transform_,
                                                     const ecs::Float3& local_vector_) noexcept {
    if constexpr (std::same_as<TransformT, ecs::Transform<ecs::Dim2>>) {
        const auto& m = transform_.runtime.world_matrix;
        return ecs::Float3{
            .x = m.m00 * local_vector_.x + m.m01 * local_vector_.y,
            .y = m.m10 * local_vector_.x + m.m11 * local_vector_.y,
            .z = 0.0F,
        };
    } else {
        const auto& m = transform_.runtime.world_matrix.m;
        return ecs::Float3{
            .x = m[0] * local_vector_.x + m[4] * local_vector_.y + m[8] * local_vector_.z,
            .y = m[1] * local_vector_.x + m[5] * local_vector_.y + m[9] * local_vector_.z,
            .z = m[2] * local_vector_.x + m[6] * local_vector_.y + m[10] * local_vector_.z,
        };
    }
}

template<typename ParticleT, typename EmitterT, typename TransformT, typename ScratchT>
void BuildGpuStateRecords(const ParticleT* particles_,
                          const EmitterT* emitters_,
                          const TransformT* transforms_,
                          const ScratchT& runtime_scratch_,
                          const ecs::ParticleRuntimeBuildStats& runtime_stats_,
                          ParticleSimulationMcVector<ParticleGpuStateRecord>& out_records_) {
    out_records_.clear();
    out_records_.reserve(runtime_stats_.visible_particle_count);
    const std::uint32_t target_count = runtime_stats_.visible_particle_count;
    if (particles_ == nullptr || emitters_ == nullptr || transforms_ == nullptr || target_count == 0U) {
        return;
    }

    auto encode_pipeline_state = [](const auto& component_) noexcept {
        using ComponentT = std::remove_cvref_t<decltype(component_)>;
        const ecs::RuntimeBlendPreset blend_preset = ecs::ResolveRuntimeBlendPreset(
            component_.style.blend_mode,
            component_.style.premultiplied_alpha != 0U);
        std::uint32_t state =
            (static_cast<std::uint32_t>(blend_preset) & ecs::particle_pipeline_state_blend_mask)
            << ecs::particle_pipeline_state_blend_shift;
        state |= (static_cast<std::uint32_t>(component_.style.facing_mode) &
                  ecs::particle_pipeline_state_facing_mode_mask)
                 << ecs::particle_pipeline_state_facing_mode_shift;
        state |= (static_cast<std::uint32_t>(component_.style.render_mode) &
                  ecs::particle_pipeline_state_render_mode_mask)
                 << ecs::particle_pipeline_state_render_mode_shift;
        state |= (static_cast<std::uint32_t>(component_.style.lighting_mode) &
                  ecs::particle_pipeline_state_lighting_mode_mask)
                 << ecs::particle_pipeline_state_lighting_mode_shift;
        if constexpr (std::same_as<ComponentT, ecs::Particle<ecs::Dim3>>) {
            state |= static_cast<std::uint32_t>(component_.style.depth_test != 0U)
                     << ecs::particle_pipeline_state_depth_test_shift;
            state |= static_cast<std::uint32_t>(component_.style.depth_write != 0U)
                     << ecs::particle_pipeline_state_depth_write_shift;
            state |= static_cast<std::uint32_t>(component_.style.double_sided != 0U)
                     << ecs::particle_pipeline_state_double_sided_shift;
        }
        return state;
    };

    std::uint64_t current_sort_key = 0U;
    std::uint32_t current_material_id = 0U;
    std::uint32_t current_texture_id = 0U;
    std::uint32_t current_batch_tag = 0U;
    std::uint32_t current_pipeline_state = 0U;
    bool batch_valid = false;
    std::uint32_t current_batch_index = 0U;

    for (std::uint32_t component_index : runtime_scratch_.build_component_indices) {
        if (out_records_.size() >= target_count) {
            break;
        }
        const auto& component = particles_[component_index];
        const auto& emitter = emitters_[component_index];
        const auto& transform = transforms_[component_index];
        const auto& emitter_state = runtime_scratch_.emitter_states[component_index];
        if (component.runtime.route.visible == 0U || emitter_state.active_count == 0U) {
            continue;
        }

        const std::uint64_t sort_key = ecs::ParticleSystem<typename ParticleT::StyleType::DimensionTag>::SortKey(component);
        (void)sort_key;

        for (std::uint32_t particle_index = 0U;
             particle_index < emitter_state.active_count && out_records_.size() < target_count;
             ++particle_index) {
            ecs::Float3 position{
                .x = emitter_state.particles.position_x[particle_index],
                .y = emitter_state.particles.position_y[particle_index],
                .z = emitter_state.particles.position_z[particle_index],
            };
            ecs::Float3 velocity{
                .x = emitter_state.particles.velocity_x[particle_index],
                .y = emitter_state.particles.velocity_y[particle_index],
                .z = emitter_state.particles.velocity_z[particle_index],
            };
            if (emitter.config.simulation_space == ecs::ParticleSimulationSpace::local) {
                position = TransformPointForParticle(transform, position);
                velocity = TransformVectorForParticle(transform, velocity);
            }

            ParticleGpuStateRecord record{};
            record.position_x = position.x;
            record.position_y = position.y;
            record.position_z = position.z;
            record.age_s = emitter_state.particles.age_s[particle_index];
            record.velocity_x = velocity.x;
            record.velocity_y = velocity.y;
            record.velocity_z = velocity.z;
            record.lifetime_s = emitter_state.particles.lifetime_s[particle_index];
            record.start_size = emitter_state.particles.start_size[particle_index];
            record.end_size = emitter_state.particles.end_size[particle_index];
            record.rotation_radians = emitter_state.particles.rotation_radians[particle_index];
            record.angular_velocity_radians = emitter_state.particles.angular_velocity_radians[particle_index];
            record.start_color_rgba8 = emitter_state.particles.start_color_rgba8[particle_index];
            record.end_color_rgba8 = emitter_state.particles.end_color_rgba8[particle_index];
            record.texture_id = component.runtime.route.texture_id;
            record.material_id = ecs::ResolveEffectiveMaterialId(component.runtime.route);
            record.component_index = component_index;
            record.user_data = component.runtime.route.user_data;
            record.packed_flags = k_particle_gpu_flag_alive;
            record.reserved0 = 0U;
            record.drag_coefficient = emitter.config.drag_coefficient;
            record.gravity_scale = emitter.config.gravity_scale;
            if constexpr (std::same_as<ParticleT, ecs::Particle<ecs::Dim3>>) {
                record.soft_particle_distance = component.style.soft_particle_distance;
                record.stretch_velocity_scale = component.style.stretch_velocity_scale;
            } else {
                record.soft_particle_distance = 0.0F;
                record.stretch_velocity_scale = 0.0F;
            }
            out_records_.push_back(record);
        }
    }
}

void BuildIndirectCommands(const ParticleSimulationMcVector<ecs::ParticleDrawBatch>& draw_batches_,
                           const ParticleSimulationMcVector<ParticleGpuComponentRange>& component_ranges_,
                           ParticleSimulationMcVector<ParticleGpuIndirectCommand>& out_commands_) {
    out_commands_.clear();
    out_commands_.reserve(draw_batches_.size());
    ParticleSimulationMcVector<std::uint32_t> batch_capacities{};
    batch_capacities.resize(draw_batches_.size(), 0U);
    for (const ParticleGpuComponentRange& range : component_ranges_) {
        if (range.state_range_count == 0U || range.batch_index >= batch_capacities.size()) {
            continue;
        }
        batch_capacities[range.batch_index] += range.state_range_count;
    }

    std::uint32_t first_instance = 0U;
    for (const ecs::ParticleDrawBatch& batch : draw_batches_) {
        ParticleGpuIndirectCommand command{};
        command.draw.vertexCount = 6U;
        command.draw.instanceCount = 0U;
        command.draw.firstVertex = 0U;
        command.draw.firstInstance = first_instance;
        command.live_particle_count = 0U;
        out_commands_.push_back(command);
        if (out_commands_.size() - 1U < batch_capacities.size()) {
            first_instance += batch_capacities[out_commands_.size() - 1U];
        }
    }
}

template<typename ParticleT, typename ScratchT>
void BuildSpawnPackets(const ParticleT* particles_,
                       const ScratchT& runtime_scratch_,
                       ParticleSimulationMcVector<ParticleGpuSpawnPacket>& out_packets_) {
    out_packets_.clear();
    out_packets_.reserve(runtime_scratch_.build_component_indices.size());
    if (particles_ == nullptr) {
        return;
    }

    for (std::uint32_t component_index : runtime_scratch_.build_component_indices) {
        const auto& state = runtime_scratch_.emitter_states[component_index];
        if (state.spawned_this_build == 0U && state.active_count == 0U) {
            continue;
        }

        ParticleGpuSpawnPacket packet{};
        packet.component_index = component_index;
        packet.spawn_count = state.spawned_this_build;
        packet.random_seed = state.random_state;
        packet.packed_flags = static_cast<std::uint32_t>(particles_[component_index].style.simulation_mode);
        packet.delta_time_s = 0.0F;
        packet.emitter_time_s = state.simulation_time_s;
        out_packets_.push_back(packet);
    }
}

template<typename TransformT>
void PackParticleTransformRows(const TransformT& transform_,
                               ParticleGpuSpawnPacket& packet_) noexcept {
    if constexpr (std::same_as<TransformT, ecs::Transform<ecs::Dim2>>) {
        const auto& m = transform_.runtime.world_matrix;
        packet_.transform_x0 = m.m00;
        packet_.transform_x1 = m.m01;
        packet_.transform_x2 = 0.0F;
        packet_.transform_x3 = m.m02;
        packet_.transform_y0 = m.m10;
        packet_.transform_y1 = m.m11;
        packet_.transform_y2 = 0.0F;
        packet_.transform_y3 = m.m12;
        packet_.transform_z0 = 0.0F;
        packet_.transform_z1 = 0.0F;
        packet_.transform_z2 = 1.0F;
        packet_.transform_z3 = 0.0F;
    } else {
        const auto& m = transform_.runtime.world_matrix.m;
        packet_.transform_x0 = m[0];
        packet_.transform_x1 = m[4];
        packet_.transform_x2 = m[8];
        packet_.transform_x3 = m[12];
        packet_.transform_y0 = m[1];
        packet_.transform_y1 = m[5];
        packet_.transform_y2 = m[9];
        packet_.transform_y3 = m[13];
        packet_.transform_z0 = m[2];
        packet_.transform_z1 = m[6];
        packet_.transform_z2 = m[10];
        packet_.transform_z3 = m[14];
    }
}

template<typename ParticleT>
[[nodiscard]] std::uint64_t ComputeParticleSortKey(const ParticleT& component_) noexcept {
    if constexpr (std::same_as<ParticleT, ecs::Particle<ecs::Dim2>>) {
        return ecs::ParticleSystem<ecs::Dim2>::SortKey(component_);
    } else {
        return ecs::ParticleSystem<ecs::Dim3>::SortKey(component_);
    }
}

template<typename ParticleT, typename ScratchT>
[[nodiscard]] bool RequiresGpuParticleSort(const ParticleT* particles_,
                                           const ScratchT& runtime_scratch_) noexcept {
    if (particles_ == nullptr) {
        return false;
    }
    for (std::uint32_t component_index : runtime_scratch_.build_component_indices) {
        const auto& component = particles_[component_index];
        if (component.runtime.route.visible == 0U) {
            continue;
        }
        if (component.style.sort_mode == ecs::ParticleSortMode::gpu_radix) {
            return true;
        }
    }
    return false;
}

template<typename ParticleT>
[[nodiscard]] std::uint32_t EncodeParticlePipelineState(const ParticleT& component_) noexcept {
    const ecs::RuntimeBlendPreset blend_preset = ecs::ResolveRuntimeBlendPreset(
        component_.style.blend_mode,
        component_.style.premultiplied_alpha != 0U);
    std::uint32_t state =
        (static_cast<std::uint32_t>(blend_preset) & ecs::particle_pipeline_state_blend_mask)
        << ecs::particle_pipeline_state_blend_shift;
    state |= (static_cast<std::uint32_t>(component_.style.facing_mode) &
              ecs::particle_pipeline_state_facing_mode_mask)
             << ecs::particle_pipeline_state_facing_mode_shift;
    state |= (static_cast<std::uint32_t>(component_.style.render_mode) &
              ecs::particle_pipeline_state_render_mode_mask)
             << ecs::particle_pipeline_state_render_mode_shift;
    state |= (static_cast<std::uint32_t>(component_.style.lighting_mode) &
              ecs::particle_pipeline_state_lighting_mode_mask)
             << ecs::particle_pipeline_state_lighting_mode_shift;
    if constexpr (std::same_as<ParticleT, ecs::Particle<ecs::Dim3>>) {
        state |= static_cast<std::uint32_t>(component_.style.depth_test != 0U)
                 << ecs::particle_pipeline_state_depth_test_shift;
        state |= static_cast<std::uint32_t>(component_.style.depth_write != 0U)
                 << ecs::particle_pipeline_state_depth_write_shift;
        state |= static_cast<std::uint32_t>(component_.style.double_sided != 0U)
                 << ecs::particle_pipeline_state_double_sided_shift;
    }
    return state;
}

template<typename ParticleT, typename EmitterT>
[[nodiscard]] ParticleGpuStateRecord MakeDeadGpuStateRecord(const ParticleT& component_,
                                                            const EmitterT& emitter_,
                                                            std::uint32_t component_index_,
                                                            std::uint32_t batch_index_) noexcept {
    auto pack_rgba8 = [](const ecs::Rgba8& color_) noexcept {
        return static_cast<std::uint32_t>(color_.r) |
               (static_cast<std::uint32_t>(color_.g) << 8U) |
               (static_cast<std::uint32_t>(color_.b) << 16U) |
               (static_cast<std::uint32_t>(color_.a) << 24U);
    };
    ParticleGpuStateRecord record{};
    record.start_size = emitter_.config.start_size_min;
    record.end_size = emitter_.config.end_size_min;
    record.rotation_radians = emitter_.config.rotation_min_radians;
    record.angular_velocity_radians = emitter_.config.angular_velocity_min;
    record.start_color_rgba8 = pack_rgba8(component_.style.start_color);
    record.end_color_rgba8 = pack_rgba8(component_.style.end_color);
    record.texture_id = component_.runtime.route.texture_id;
    record.material_id = ecs::ResolveEffectiveMaterialId(component_.runtime.route);
    record.component_index = component_index_;
    record.user_data = component_.runtime.route.user_data;
    record.packed_flags = 0U;
    record.reserved0 = batch_index_;
    record.drag_coefficient = emitter_.config.drag_coefficient;
    record.gravity_scale = emitter_.config.gravity_scale;
    if constexpr (std::same_as<ParticleT, ecs::Particle<ecs::Dim3>>) {
        record.soft_particle_distance = component_.style.soft_particle_distance;
        record.stretch_velocity_scale = component_.style.stretch_velocity_scale;
    } else {
        record.soft_particle_distance = 0.0F;
        record.stretch_velocity_scale = 0.0F;
    }
    return record;
}

template<typename ParticleT, typename EmitterT, typename TransformT, typename ScratchT>
void BuildGpuStateRecords(const ParticleT* particles_,
                          const EmitterT* emitters_,
                          const TransformT* transforms_,
                          std::uint32_t component_count_,
                          const ScratchT& runtime_scratch_,
                          bool expand_to_component_capacity_,
                          ParticleSimulationMcVector<ParticleGpuStateRecord>& out_records_,
                          ParticleSimulationMcVector<ParticleGpuComponentRange>& out_component_ranges_) {
    out_records_.clear();
    out_component_ranges_.clear();
    out_component_ranges_.resize(component_count_);
    if (particles_ == nullptr || emitters_ == nullptr || transforms_ == nullptr || component_count_ == 0U) {
        return;
    }

    std::uint32_t reserve_count = 0U;
    for (std::uint32_t component_index : runtime_scratch_.build_component_indices) {
        const auto& component = particles_[component_index];
        const auto& emitter_state = runtime_scratch_.emitter_states[component_index];
        if (component.runtime.route.visible == 0U) {
            continue;
        }
        reserve_count += expand_to_component_capacity_
            ? component.style.max_particles
            : emitter_state.active_count;
    }
    out_records_.reserve(reserve_count);

    std::uint32_t visible_instance_cursor = 0U;
    std::uint32_t current_batch_index = 0U;
    std::uint32_t current_batch_instance_end = 0U;
    std::uint64_t current_sort_key = 0U;
    std::uint32_t current_material_id = 0U;
    std::uint32_t current_texture_id = 0U;
    std::uint32_t current_batch_tag = 0U;
    std::uint32_t current_pipeline_state = 0U;
    bool batch_valid = false;

    for (std::uint32_t component_index : runtime_scratch_.build_component_indices) {
        const auto& component = particles_[component_index];
        const auto& emitter = emitters_[component_index];
        const auto& transform = transforms_[component_index];
        const auto& emitter_state = runtime_scratch_.emitter_states[component_index];
        if (component.runtime.route.visible == 0U) {
            continue;
        }

        const std::uint32_t active_count = emitter_state.active_count;
        const std::uint32_t state_range_count = expand_to_component_capacity_
            ? component.style.max_particles
            : active_count;
        if (state_range_count == 0U) {
            continue;
        }

        const std::uint64_t sort_key = ComputeParticleSortKey(component);
        const std::uint32_t material_id = ecs::ResolveEffectiveMaterialId(component.runtime.route);
        const std::uint32_t texture_id = component.runtime.route.texture_id;
        const std::uint32_t batch_tag = component.runtime.route.batch_tag;
        const std::uint32_t pipeline_state = EncodeParticlePipelineState(component);
        const std::uint32_t instance_begin = visible_instance_cursor;

        if (!batch_valid ||
            current_sort_key != sort_key ||
            current_material_id != material_id ||
            current_texture_id != texture_id ||
            current_batch_tag != batch_tag ||
            current_pipeline_state != pipeline_state ||
            current_batch_instance_end != instance_begin) {
            current_batch_index = batch_valid ? (current_batch_index + 1U) : 0U;
            current_sort_key = sort_key;
            current_material_id = material_id;
            current_texture_id = texture_id;
            current_batch_tag = batch_tag;
            current_pipeline_state = pipeline_state;
            batch_valid = true;
        }
        current_batch_instance_end = instance_begin + active_count;

        ParticleGpuComponentRange range{};
        range.state_range_begin = static_cast<std::uint32_t>(out_records_.size());
        range.state_range_count = state_range_count;
        range.batch_index = current_batch_index;
        out_component_ranges_[component_index] = range;

        for (std::uint32_t particle_index = 0U; particle_index < active_count; ++particle_index) {
            ecs::Float3 position{
                .x = emitter_state.particles.position_x[particle_index],
                .y = emitter_state.particles.position_y[particle_index],
                .z = emitter_state.particles.position_z[particle_index],
            };
            ecs::Float3 velocity{
                .x = emitter_state.particles.velocity_x[particle_index],
                .y = emitter_state.particles.velocity_y[particle_index],
                .z = emitter_state.particles.velocity_z[particle_index],
            };
            if (emitter.config.simulation_space == ecs::ParticleSimulationSpace::local) {
                position = TransformPointForParticle(transform, position);
                velocity = TransformVectorForParticle(transform, velocity);
            }

            ParticleGpuStateRecord record =
                MakeDeadGpuStateRecord(component, emitter, component_index, current_batch_index);
            record.position_x = position.x;
            record.position_y = position.y;
            record.position_z = position.z;
            record.age_s = emitter_state.particles.age_s[particle_index];
            record.velocity_x = velocity.x;
            record.velocity_y = velocity.y;
            record.velocity_z = velocity.z;
            record.lifetime_s = emitter_state.particles.lifetime_s[particle_index];
            record.start_size = emitter_state.particles.start_size[particle_index];
            record.end_size = emitter_state.particles.end_size[particle_index];
            record.rotation_radians = emitter_state.particles.rotation_radians[particle_index];
            record.angular_velocity_radians = emitter_state.particles.angular_velocity_radians[particle_index];
            record.start_color_rgba8 = emitter_state.particles.start_color_rgba8[particle_index];
            record.end_color_rgba8 = emitter_state.particles.end_color_rgba8[particle_index];
            record.packed_flags = k_particle_gpu_flag_alive;
            out_records_.push_back(record);
        }

        for (std::uint32_t dead_index = active_count; dead_index < state_range_count; ++dead_index) {
            out_records_.push_back(
                MakeDeadGpuStateRecord(component, emitter, component_index, current_batch_index));
        }

        visible_instance_cursor += active_count;
    }
}

[[nodiscard]] std::uint32_t AdvanceRandomState(std::uint32_t& state_) noexcept {
    state_ ^= (state_ << 13U);
    state_ ^= (state_ >> 17U);
    state_ ^= (state_ << 5U);
    state_ = state_ == 0U ? 1U : state_;
    return state_;
}

template<typename ParticleT, typename EmitterT>
[[nodiscard]] std::uint32_t BuildGpuSpawnCountForComponent(const ParticleT& component_,
                                                           const EmitterT& emitter_,
                                                           const ecs::ParticleRuntimeBuildConfig& build_config_,
                                                           std::uint32_t& random_state_,
                                                           float& spawn_accumulator_s_,
                                                           float& fixed_step_accumulator_s_,
                                                           float& burst_accumulator_s_,
                                                           float& simulation_time_s_,
                                                           std::uint8_t& burst_fired_once_) noexcept {
    if (component_.style.simulation_mode != ecs::ParticleSimulationMode::gpu ||
        emitter_.config.playing == 0U ||
        build_config_.delta_time_s <= 0.0F ||
        build_config_.fixed_step_s <= 0.0F) {
        return 0U;
    }

    fixed_step_accumulator_s_ += build_config_.delta_time_s;
    std::uint32_t step_count = static_cast<std::uint32_t>(fixed_step_accumulator_s_ / build_config_.fixed_step_s);
    step_count = std::min(step_count, std::max<std::uint32_t>(1U, build_config_.max_simulation_steps));
    if (step_count == 0U) {
        return 0U;
    }
    fixed_step_accumulator_s_ -= static_cast<float>(step_count) * build_config_.fixed_step_s;

    std::uint32_t total_spawn_count = 0U;
    for (std::uint32_t step_index = 0U; step_index < step_count; ++step_index) {
        const float step_dt = build_config_.fixed_step_s;
        spawn_accumulator_s_ += emitter_.config.spawn_rate * step_dt;
        std::uint32_t spawn_count = static_cast<std::uint32_t>(spawn_accumulator_s_);
        spawn_accumulator_s_ -= static_cast<float>(spawn_count);

        if (emitter_.config.burst_count > 0U) {
            burst_accumulator_s_ += step_dt;
            const bool should_fire_initial = burst_fired_once_ == 0U;
            const bool should_fire_interval =
                emitter_.config.burst_interval_s > 0.0F &&
                burst_accumulator_s_ >= emitter_.config.burst_interval_s &&
                emitter_.config.looping != 0U;
            if (should_fire_initial || should_fire_interval) {
                spawn_count += emitter_.config.burst_count;
                burst_fired_once_ = 1U;
                if (emitter_.config.burst_interval_s > 0.0F) {
                    burst_accumulator_s_ = std::fmod(burst_accumulator_s_,
                                                     emitter_.config.burst_interval_s);
                } else {
                    burst_accumulator_s_ = 0.0F;
                }
            }
        }

        total_spawn_count += std::min<std::uint32_t>(
            spawn_count,
            static_cast<std::uint32_t>(emitter_.config.max_spawn_per_step));
        simulation_time_s_ += step_dt;
    }

    if (total_spawn_count > 0U) {
        std::uint32_t advanced_random_state = random_state_ + total_spawn_count;
        random_state_ = AdvanceRandomState(advanced_random_state);
    }
    return total_spawn_count;
}

template<typename ParticleT, typename EmitterT, typename TransformT, typename ScratchT>
void BuildSpawnPackets(const ParticleT* particles_,
                       const EmitterT* emitters_,
                       const TransformT* transforms_,
                       std::uint32_t component_count_,
                       const ecs::ParticleRuntimeBuildConfig& build_config_,
                       bool cpu_seeded_this_frame_,
                       ScratchT& runtime_scratch_,
                       const ParticleSimulationMcVector<ParticleGpuComponentRange>& component_ranges_,
                       ParticleSimulationMcVector<ParticleGpuSpawnPacket>& out_packets_) {
    out_packets_.clear();
    out_packets_.resize(component_count_);
    if (particles_ == nullptr || emitters_ == nullptr || transforms_ == nullptr) {
        return;
    }

    for (std::uint32_t component_index = 0U; component_index < component_count_; ++component_index) {
        ParticleGpuSpawnPacket packet{};
        packet.component_index = component_index;
        packet.simulation_space = static_cast<std::uint32_t>(ecs::ParticleSimulationSpace::world);
        packet.emitter_shape = static_cast<std::uint32_t>(ecs::ParticleEmitterShape::point);
        if (component_index < component_ranges_.size()) {
            packet.state_range_begin = component_ranges_[component_index].state_range_begin;
            packet.state_range_count = component_ranges_[component_index].state_range_count;
        }
        out_packets_[component_index] = packet;
    }

    for (std::uint32_t component_index : runtime_scratch_.build_component_indices) {
        if (component_index >= component_count_) {
            continue;
        }
        const auto& component = particles_[component_index];
        const auto& emitter = emitters_[component_index];
        const auto& transform = transforms_[component_index];
        auto& emitter_state = runtime_scratch_.emitter_states[component_index];
        auto& packet = out_packets_[component_index];
        if (component.runtime.route.visible == 0U || packet.state_range_count == 0U) {
            continue;
        }

        packet.random_seed = emitter_state.random_state;
        packet.delta_time_s = build_config_.delta_time_s;
        packet.emitter_time_s = emitter_state.simulation_time_s;
        packet.lifetime_min_s = emitter.config.lifetime_min_s;
        packet.lifetime_max_s = emitter.config.lifetime_max_s;
        packet.speed_min = emitter.config.speed_min;
        packet.speed_max = emitter.config.speed_max;
        packet.start_size_min = emitter.config.start_size_min;
        packet.start_size_max = emitter.config.start_size_max;
        packet.end_size_min = emitter.config.end_size_min;
        packet.end_size_max = emitter.config.end_size_max;
        packet.rotation_min_radians = emitter.config.rotation_min_radians;
        packet.rotation_max_radians = emitter.config.rotation_max_radians;
        packet.angular_velocity_min = emitter.config.angular_velocity_min;
        packet.angular_velocity_max = emitter.config.angular_velocity_max;
        packet.drag_coefficient = emitter.config.drag_coefficient;
        packet.gravity_scale = emitter.config.gravity_scale;
        packet.emission_extent_x = emitter.config.emission_extent.x;
        packet.emission_extent_y = emitter.config.emission_extent.y;
        packet.emission_extent_z = emitter.config.emission_extent.z;
        packet.emission_radius = emitter.config.emission_radius;
        packet.emission_axis_x = emitter.config.emission_axis.x;
        packet.emission_axis_y = emitter.config.emission_axis.y;
        packet.emission_axis_z = emitter.config.emission_axis.z;
        packet.cone_half_angle_radians = emitter.config.cone_half_angle_radians;
        packet.spread_angle_radians = emitter.config.spread_angle_radians;
        packet.simulation_space = static_cast<std::uint32_t>(emitter.config.simulation_space);
        packet.emitter_shape = static_cast<std::uint32_t>(emitter.config.emitter_shape);
        packet.packed_flags = static_cast<std::uint32_t>(component.style.simulation_mode);
        PackParticleTransformRows(transform, packet);

        if (cpu_seeded_this_frame_ || component.style.simulation_mode != ecs::ParticleSimulationMode::gpu) {
            packet.spawn_count = 0U;
            packet.random_seed = 0U;
            packet.delta_time_s = 0.0F;
            packet.emitter_time_s = 0.0F;
            continue;
        }

        packet.spawn_count = BuildGpuSpawnCountForComponent(component,
                                                            emitter,
                                                            build_config_,
                                                            emitter_state.random_state,
                                                            emitter_state.spawn_accumulator_s,
                                                            emitter_state.fixed_step_accumulator_s,
                                                            emitter_state.burst_accumulator_s,
                                                            emitter_state.simulation_time_s,
                                                            emitter_state.burst_fired_once);
        if (packet.spawn_count > 0U) {
            packet.emitter_time_s = emitter_state.simulation_time_s;
            packet.random_seed = emitter_state.random_state;
        } else {
            packet.random_seed = 0U;
            packet.delta_time_s = 0.0F;
            packet.emitter_time_s = 0.0F;
        }
    }
}

[[nodiscard]] std::uint64_t HashCombine64(std::uint64_t seed_,
                                          std::uint64_t value_) noexcept {
    return seed_ ^ (value_ + 0x9e3779b97f4a7c15ULL + (seed_ << 6U) + (seed_ >> 2U));
}

[[nodiscard]] std::uint64_t ComposeGpuBuildRevision(const ecs::ParticleRuntimeBuildStats& runtime_stats_) noexcept {
    std::uint64_t revision = runtime_stats_.component_signature;
    revision = HashCombine64(revision, runtime_stats_.transform_signature);
    revision = HashCombine64(revision, runtime_stats_.visible_signature);
    revision = HashCombine64(revision, runtime_stats_.runtime_state_signature);
    return revision;
}

[[nodiscard]] std::uint64_t ComposeIndirectRevision(const ecs::ParticleRuntimeBuildStats& runtime_stats_,
                                                    std::uint32_t draw_batch_count_) noexcept {
    std::uint64_t hash = ComposeGpuBuildRevision(runtime_stats_);
    return HashCombine64(hash, static_cast<std::uint64_t>(draw_batch_count_));
}

template<typename T>
[[nodiscard]] std::uint64_t HashPodSpan(const T* data_, std::size_t count_) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data_);
    const std::size_t byte_count = count_ * sizeof(T);
    for (std::size_t index = 0U; index < byte_count; ++index) {
        hash ^= static_cast<std::uint64_t>(bytes[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

void ParticleSimulationHost::Initialize(VulkanContext& context_,
                                        resource::GpuMemoryHost& gpu_memory_host_,
                                        const ParticleSimulationHostCreateInfo& create_info_) {
    if (initialized) {
        Shutdown(context_);
    }
    if (create_info_.frames_in_flight == 0U) {
        throw std::invalid_argument("ParticleSimulationHost::Initialize frames_in_flight must be > 0");
    }

    gpu_memory_host = &gpu_memory_host_;
    create_info_cache = create_info_;
    create_info_cache.initial_particle_capacity =
        std::max<std::uint32_t>(1U, create_info_cache.initial_particle_capacity);
    create_info_cache.initial_visible_particle_capacity =
        std::max<std::uint32_t>(1U, create_info_cache.initial_visible_particle_capacity);
    create_info_cache.initial_spawn_packet_capacity =
        std::max<std::uint32_t>(1U, create_info_cache.initial_spawn_packet_capacity);
    create_info_cache.initial_indirect_command_capacity =
        std::max<std::uint32_t>(1U, create_info_cache.initial_indirect_command_capacity);
    create_info_cache.initial_sort_key_capacity =
        std::max<std::uint32_t>(1U, create_info_cache.initial_sort_key_capacity);

    capabilities = QueryCapabilities(context_);
    frames.clear();
    frames.resize(create_info_cache.frames_in_flight);
    retired_buffers.Clear();
    gpu_build_scratch_2d = {};
    gpu_build_scratch_3d = {};
    gpu_build_descriptor_layout_id = {};
    gpu_build_pipeline_layout_id = {};
    gpu_update_shader_2d_id = {};
    gpu_update_shader_3d_id = {};
    gpu_build_shader_2d_id = {};
    gpu_build_shader_3d_id = {};
    gpu_sort_shader_3d_id = {};
    gpu_update_pipeline_2d_id = {};
    gpu_update_pipeline_3d_id = {};
    gpu_build_pipeline_2d_id = {};
    gpu_build_pipeline_3d_id = {};
    gpu_sort_pipeline_3d_id = {};
    stats = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    simulation_epoch = 0U;
    last_begin_frame_cookie = ~std::uint64_t{0};
    initialized = true;
}

void ParticleSimulationHost::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(context_.Device());
    }

    for (auto& frame : frames) {
        DestroyFrameState(context_, frame);
    }
    frames.clear();

    DestroyRetiredBuffers(context_);

    gpu_memory_host = nullptr;
    create_info_cache = {};
    capabilities = {};
    retired_buffers.Clear();
    gpu_build_scratch_2d = {};
    gpu_build_scratch_3d = {};
    gpu_build_descriptor_layout_id = {};
    gpu_build_pipeline_layout_id = {};
    gpu_update_shader_2d_id = {};
    gpu_update_shader_3d_id = {};
    gpu_build_shader_2d_id = {};
    gpu_build_shader_3d_id = {};
    gpu_update_pipeline_2d_id = {};
    gpu_update_pipeline_3d_id = {};
    gpu_build_pipeline_2d_id = {};
    gpu_build_pipeline_3d_id = {};
    stats = {};
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    simulation_epoch = 0U;
    last_begin_frame_cookie = ~std::uint64_t{0};
    initialized = false;
}

void ParticleSimulationHost::BeginFrame(VulkanContext& context_,
                                        std::uint32_t frame_index_,
                                        std::uint64_t last_submitted_value_,
                                        std::uint64_t completed_submit_value_) {
    if (!initialized) {
        throw std::runtime_error("ParticleSimulationHost::BeginFrame called before Initialize");
    }

    const std::uint64_t frame_cookie =
        (last_submitted_value_ << 16U) ^ (completed_submit_value_ << 1U) ^ frame_index_;
    if (frame_cookie == last_begin_frame_cookie) {
        return;
    }

    last_submitted_value_seen = std::max(last_submitted_value_seen, last_submitted_value_);
    completed_submit_value_seen = std::max(completed_submit_value_seen, completed_submit_value_);
    CollectRetiredBuffers(context_, completed_submit_value_seen);
    FrameState& frame = FrameAt(frame_index_);
    frame.build_2d.update_descriptor_set = VK_NULL_HANDLE;
    frame.build_2d.build_descriptor_set = VK_NULL_HANDLE;
    frame.build_2d.state_record_count = 0U;
    frame.build_2d.spawn_packet_count = 0U;
    frame.build_2d.indirect_command_count = 0U;
    frame.build_2d.dispatch_group_count = 0U;
    frame.build_2d.gpu_sort_enabled = false;
    frame.build_2d.update_delta_time_s = 0.0F;
    frame.build_3d.update_descriptor_set = VK_NULL_HANDLE;
    frame.build_3d.build_descriptor_set = VK_NULL_HANDLE;
    frame.build_3d.state_record_count = 0U;
    frame.build_3d.spawn_packet_count = 0U;
    frame.build_3d.indirect_command_count = 0U;
    frame.build_3d.dispatch_group_count = 0U;
    frame.build_3d.gpu_sort_enabled = false;
    frame.build_3d.update_delta_time_s = 0.0F;
    ++stats.begin_frame_count;
    ++simulation_epoch;
    last_begin_frame_cookie = frame_cookie;
}

ParticleSimulationFrameResources ParticleSimulationHost::PrepareFrameResources(
    VulkanContext& context_,
    std::uint32_t frame_index_,
    const ParticleSimulationPrepareDesc& prepare_desc_) {
    if (!initialized) {
        throw std::runtime_error("ParticleSimulationHost::PrepareFrameResources called before Initialize");
    }

    ParticleSimulationFrameResources resources{};
    resources.frame_index = frame_index_;
    resources.state_read_index = static_cast<std::uint32_t>(simulation_epoch & 0x1ULL);
    resources.state_write_index = resources.state_read_index ^ 0x1U;
    resources.resolved_path = ResolveSimulationPath(prepare_desc_.requested_mode, capabilities);
    resources.fell_back_to_cpu =
        resources.resolved_path == ParticleSimulationResolvedPath::cpu &&
        prepare_desc_.requested_mode != ecs::ParticleSimulationMode::cpu;
    resources.using_gpu_buffers = resources.resolved_path != ParticleSimulationResolvedPath::cpu;

    if (resources.fell_back_to_cpu && !prepare_desc_.allow_cpu_fallback) {
        throw std::runtime_error(
            "ParticleSimulationHost::PrepareFrameResources cannot satisfy requested GPU simulation path");
    }

    ++stats.prepared_frame_count;
    switch (resources.resolved_path) {
    case ParticleSimulationResolvedPath::gpu:
        ++stats.gpu_prepare_count;
        break;
    case ParticleSimulationResolvedPath::hybrid_gpu:
        ++stats.hybrid_prepare_count;
        break;
    case ParticleSimulationResolvedPath::cpu:
    default:
        ++stats.cpu_prepare_count;
        if (resources.fell_back_to_cpu) {
            ++stats.fallback_to_cpu_count;
        }
        return resources;
    }

    const std::uint32_t particle_capacity = std::max<std::uint32_t>(
        prepare_desc_.particle_capacity,
        create_info_cache.initial_particle_capacity);
    const std::uint32_t visible_particle_capacity = std::max<std::uint32_t>(
        prepare_desc_.visible_particle_capacity,
        create_info_cache.initial_visible_particle_capacity);
    const std::uint32_t spawn_packet_capacity = std::max<std::uint32_t>(
        prepare_desc_.spawn_packet_capacity,
        create_info_cache.initial_spawn_packet_capacity);
    const std::uint32_t indirect_command_capacity = std::max<std::uint32_t>(
        prepare_desc_.indirect_command_capacity,
        create_info_cache.initial_indirect_command_capacity);
    const std::uint32_t sort_key_capacity = std::max<std::uint32_t>(
        prepare_desc_.sort_key_capacity,
        create_info_cache.initial_sort_key_capacity);
    const std::uint32_t draw_instance_stride_bytes = std::max<std::uint32_t>(
        1U,
        prepare_desc_.draw_instance_stride_bytes);

    FrameState& frame = FrameAt(frame_index_);
    FrameState::BuildPathState* build_path = nullptr;
    if (prepare_desc_.require_draw_instance_buffer) {
        build_path = Is2DStride(draw_instance_stride_bytes)
            ? &frame.build_2d
            : &frame.build_3d;
        build_path->draw_instance_stride_bytes = draw_instance_stride_bytes;
        EnsureBufferCapacity(context_,
                             build_path->alive_list,
                             visible_particle_capacity,
                             sizeof(ParticleGpuAliveEntry),
                             k_list_buffer_usage,
                             stats.scratch_buffer_resize_count);
        EnsureBufferCapacity(context_,
                             build_path->dead_list,
                             particle_capacity,
                             sizeof(ParticleGpuAliveEntry),
                             k_list_buffer_usage,
                             stats.scratch_buffer_resize_count);
        EnsureBufferCapacity(context_,
                             build_path->state_buffers[0],
                             particle_capacity,
                             sizeof(ParticleGpuStateRecord),
                             k_state_buffer_usage,
                             stats.state_buffer_resize_count);
        EnsureBufferCapacity(context_,
                             build_path->state_buffers[1],
                             particle_capacity,
                             sizeof(ParticleGpuStateRecord),
                             k_state_buffer_usage,
                             stats.state_buffer_resize_count);
        EnsureBufferCapacity(context_,
                             build_path->spawn_packets,
                             spawn_packet_capacity,
                             sizeof(ParticleGpuSpawnPacket),
                             k_spawn_buffer_usage,
                             stats.scratch_buffer_resize_count);
        EnsureBufferCapacity(context_,
                             build_path->draw_instances,
                             visible_particle_capacity,
                             static_cast<VkDeviceSize>(draw_instance_stride_bytes),
                             k_draw_instance_buffer_usage,
                             stats.scratch_buffer_resize_count);
        EnsureBufferCapacity(context_,
                             build_path->indirect_commands,
                             indirect_command_capacity,
                             sizeof(ParticleGpuIndirectCommand),
                             k_indirect_buffer_usage,
                             stats.scratch_buffer_resize_count);
        if (prepare_desc_.require_sort_buffers) {
            EnsureBufferCapacity(context_,
                                 build_path->sort_keys,
                                 sort_key_capacity,
                                 sizeof(ParticleGpuSortKeyRecord),
                                 k_sort_buffer_usage,
                                 stats.scratch_buffer_resize_count);
            EnsureBufferCapacity(context_,
                                 build_path->sort_indices,
                                 sort_key_capacity,
                                 sizeof(std::uint32_t),
                                 k_sort_buffer_usage,
                                 stats.scratch_buffer_resize_count);
        }

        resources.draw_instances = MakeBufferView(context_, build_path->draw_instances);
        resources.indirect_commands = MakeBufferView(context_, build_path->indirect_commands);
        resources.sort_keys = MakeBufferView(context_, build_path->sort_keys);
        resources.sort_indices = MakeBufferView(context_, build_path->sort_indices);
        resources.spawn_packets = MakeBufferView(context_, build_path->spawn_packets);
        resources.state_read = MakeBufferView(context_, build_path->state_buffers[resources.state_read_index]);
        resources.state_write = MakeBufferView(context_, build_path->state_buffers[resources.state_write_index]);
        resources.alive_list = MakeBufferView(context_, build_path->alive_list);
        resources.dead_list = MakeBufferView(context_, build_path->dead_list);
    }

    if (resources.spawn_packets.buffer == VK_NULL_HANDLE) {
        resources.spawn_packets = {};
    }
    if (resources.state_read.buffer == VK_NULL_HANDLE) {
        resources.state_read = {};
        resources.state_write = {};
    }

    stats.state_particle_capacity =
        build_path != nullptr
            ? std::max(build_path->state_buffers[0].element_capacity,
                       build_path->state_buffers[1].element_capacity)
            : 0U;
    stats.visible_particle_capacity = visible_particle_capacity;
    stats.spawn_packet_capacity = build_path != nullptr ? build_path->spawn_packets.element_capacity : 0U;
    stats.indirect_command_capacity = build_path != nullptr ? build_path->indirect_commands.element_capacity : 0U;
    stats.sort_key_capacity = build_path != nullptr ? build_path->sort_keys.element_capacity : 0U;

    auto accumulate_slot_bytes = [](std::uint64_t total_, const BufferSlot& slot_) noexcept {
        return total_ + static_cast<std::uint64_t>(slot_.capacity_bytes);
    };
    stats.allocated_bytes = 0U;
    for (const FrameState& frame_state : frames) {
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_2d.alive_list);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_2d.dead_list);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_2d.state_buffers[0]);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_2d.state_buffers[1]);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_2d.spawn_packets);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_2d.draw_instances);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_2d.indirect_commands);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_2d.sort_keys);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_2d.sort_indices);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_3d.alive_list);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_3d.dead_list);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_3d.state_buffers[0]);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_3d.state_buffers[1]);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_3d.spawn_packets);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_3d.draw_instances);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_3d.indirect_commands);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_3d.sort_keys);
        stats.allocated_bytes = accumulate_slot_bytes(stats.allocated_bytes, frame_state.build_3d.sort_indices);
    }
    return resources;
}

ParticleSimulationGpuBuildResult ParticleSimulationHost::PrepareGpuBuild2D(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    render::DescriptorHost& descriptor_host_,
    render::PipelineHost& pipeline_host_,
    std::uint32_t frame_index_,
    const ParticleSimulationFrameResources& frame_resources_,
    const ecs::Particle<ecs::Dim2>* particles_,
    const ecs::ParticleEmitter<ecs::Dim2>* emitters_,
    const ecs::Transform<ecs::Dim2>* transforms_,
    std::uint32_t component_count_,
    const ecs::ParticleRuntimeBuildConfig& build_config_,
    bool cpu_seeded_this_frame_,
    ecs::Particle2DRuntimeScratch& runtime_scratch_,
    const ecs::ParticleRuntimeBuildStats& runtime_stats_,
    const asset::TextureHost* texture_host_,
    const render::BindlessResourceSystem& bindless_resources_) {
    ParticleSimulationGpuBuildResult result{};
    result.resources = frame_resources_;
    if (frame_resources_.resolved_path == ParticleSimulationResolvedPath::cpu) {
        return result;
    }
    (void)component_count_;
    EnsureGpuBuildObjects(context_, descriptor_host_, pipeline_host_);

    FrameState& frame = FrameAt(frame_index_);
    FrameState::BuildPathState& build_path = frame.build_2d;
    BufferSlot& state_read_slot = build_path.state_buffers[frame_resources_.state_read_index];
    BufferSlot& state_write_slot = build_path.state_buffers[frame_resources_.state_write_index];
    build_path.state_read_index = frame_resources_.state_read_index;
    build_path.state_write_index = frame_resources_.state_write_index;
    BuildGpuStateRecords(particles_,
                         emitters_,
                         transforms_,
                         component_count_,
                         runtime_scratch_,
                         frame_resources_.resolved_path == ParticleSimulationResolvedPath::gpu,
                         gpu_build_scratch_2d.state_records,
                         gpu_build_scratch_2d.component_ranges);
    BuildSpawnPackets(particles_,
                      emitters_,
                      transforms_,
                      component_count_,
                      build_config_,
                      cpu_seeded_this_frame_,
                      runtime_scratch_,
                      gpu_build_scratch_2d.component_ranges,
                      gpu_build_scratch_2d.spawn_packets);
    BuildIndirectCommands(runtime_scratch_.draw_batches,
                          gpu_build_scratch_2d.component_ranges,
                          gpu_build_scratch_2d.indirect_commands);

    for (auto& record : gpu_build_scratch_2d.state_records) {
        const std::uint32_t raw_texture_id = record.texture_id;
        record.texture_id = ResolveBindlessTextureSlot(texture_host_,
                                                       bindless_resources_,
                                                       raw_texture_id);
        record.material_id = ResolveBindlessSamplerSlot(texture_host_,
                                                        bindless_resources_,
                                                        raw_texture_id);
    }

    result.state_record_count = static_cast<std::uint32_t>(gpu_build_scratch_2d.state_records.size());
    result.resources.spawn_packets = MakeBufferView(context_, build_path.spawn_packets);
    result.indirect_command_count = static_cast<std::uint32_t>(gpu_build_scratch_2d.indirect_commands.size());
    build_path.state_record_count = result.state_record_count;
    build_path.spawn_packet_count = static_cast<std::uint32_t>(gpu_build_scratch_2d.spawn_packets.size());
    build_path.indirect_command_count = result.indirect_command_count;
    build_path.dispatch_group_count =
        (result.state_record_count + k_particle_build_group_size - 1U) / k_particle_build_group_size;
    build_path.update_delta_time_s =
        (frame_resources_.resolved_path == ParticleSimulationResolvedPath::gpu && !cpu_seeded_this_frame_)
            ? (build_config_.delta_time_s > 0.0F ? build_config_.delta_time_s : build_config_.fixed_step_s)
            : 0.0F;

    const std::uint64_t build_revision =
        HashPodSpan(gpu_build_scratch_2d.state_records.data(), gpu_build_scratch_2d.state_records.size());
    const std::uint64_t spawn_revision =
        HashPodSpan(gpu_build_scratch_2d.spawn_packets.data(), gpu_build_scratch_2d.spawn_packets.size());
    const std::uint64_t indirect_revision = simulation_epoch;
    const VkDeviceSize state_bytes = static_cast<VkDeviceSize>(result.state_record_count) * sizeof(ParticleGpuStateRecord);
    const VkDeviceSize spawn_bytes = static_cast<VkDeviceSize>(build_path.spawn_packet_count) * sizeof(ParticleGpuSpawnPacket);
    const VkDeviceSize indirect_bytes = static_cast<VkDeviceSize>(result.indirect_command_count) * sizeof(ParticleGpuIndirectCommand);
    const VkDeviceSize list_bytes = static_cast<VkDeviceSize>(result.state_record_count) * sizeof(ParticleGpuAliveEntry);

    if (result.state_record_count > 0U &&
        !(build_path.state_uploaded_revision == build_revision &&
          build_path.state_uploaded_size_bytes == state_bytes)) {
        upload_host_.StageAndRecordCopyBuffer(frame_index_,
                                              state_read_slot.resource.buffer,
                                              0U,
                                              gpu_build_scratch_2d.state_records.data(),
                                              state_bytes,
                                              16U);
        if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
            VkBufferMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            barrier.buffer = state_read_slot.resource.buffer;
            barrier.offset = 0U;
            barrier.size = state_bytes;
            upload_host_.RecordBufferBarrier2(frame_index_, barrier);
        }
        build_path.state_uploaded_revision = build_revision;
        build_path.state_uploaded_size_bytes = state_bytes;
        result.state_upload.buffer = state_read_slot.resource.buffer;
        result.state_upload.size_bytes = state_bytes;
        result.state_upload.element_count = result.state_record_count;
        result.state_upload.uploaded_revision = build_revision;
        result.state_upload.uploaded = true;
        ++stats.state_upload_count;
        stats.gpu_build_uploaded_bytes += static_cast<std::uint64_t>(state_bytes);
    }

    if (build_path.spawn_packet_count > 0U &&
        !(build_path.spawn_uploaded_revision == spawn_revision &&
          build_path.spawn_uploaded_size_bytes == spawn_bytes)) {
        upload_host_.StageAndRecordCopyBuffer(frame_index_,
                                              build_path.spawn_packets.resource.buffer,
                                              0U,
                                              gpu_build_scratch_2d.spawn_packets.data(),
                                              spawn_bytes,
                                              16U);
        if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
            VkBufferMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            barrier.buffer = build_path.spawn_packets.resource.buffer;
            barrier.offset = 0U;
            barrier.size = spawn_bytes;
            upload_host_.RecordBufferBarrier2(frame_index_, barrier);
        }
        build_path.spawn_uploaded_revision = spawn_revision;
        build_path.spawn_uploaded_size_bytes = spawn_bytes;
        result.spawn_upload.buffer = build_path.spawn_packets.resource.buffer;
        result.spawn_upload.size_bytes = spawn_bytes;
        result.spawn_upload.element_count = build_path.spawn_packet_count;
        result.spawn_upload.uploaded_revision = spawn_revision;
        result.spawn_upload.uploaded = true;
        ++stats.spawn_packet_upload_count;
        stats.gpu_build_uploaded_bytes += static_cast<std::uint64_t>(spawn_bytes);
    }

    if (result.indirect_command_count > 0U) {
        upload_host_.StageAndRecordCopyBuffer(frame_index_,
                                              build_path.indirect_commands.resource.buffer,
                                              0U,
                                              gpu_build_scratch_2d.indirect_commands.data(),
                                              indirect_bytes,
                                              16U);
        if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
            VkBufferMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask =
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            barrier.dstAccessMask =
                VK_ACCESS_2_SHADER_READ_BIT |
                VK_ACCESS_2_SHADER_WRITE_BIT |
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
            barrier.buffer = build_path.indirect_commands.resource.buffer;
            barrier.offset = 0U;
            barrier.size = indirect_bytes;
            upload_host_.RecordBufferBarrier2(frame_index_, barrier);
        }
        build_path.indirect_uploaded_revision = indirect_revision;
        build_path.indirect_uploaded_size_bytes = indirect_bytes;
        result.indirect_upload.buffer = build_path.indirect_commands.resource.buffer;
        result.indirect_upload.size_bytes = indirect_bytes;
        result.indirect_upload.element_count = result.indirect_command_count;
        result.indirect_upload.uploaded_revision = indirect_revision;
        result.indirect_upload.uploaded = true;
        ++stats.indirect_upload_count;
        stats.gpu_build_uploaded_bytes += static_cast<std::uint64_t>(indirect_bytes);
    }

    build_path.prepared_revision = build_revision;
    build_path.update_descriptor_set =
        descriptor_host_.AllocateSet(context_, frame_index_, gpu_build_descriptor_layout_id);
    build_path.build_descriptor_set =
        descriptor_host_.AllocateSet(context_, frame_index_, gpu_build_descriptor_layout_id);
    render::DescriptorMcVector<render::DescriptorBufferWrite> buffer_writes{};
    buffer_writes.reserve(6U);
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = state_read_slot.resource.buffer,
        .offset = 0U,
        .range = state_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 1U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = state_write_slot.resource.buffer,
        .offset = 0U,
        .range = state_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 2U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.alive_list.resource.buffer,
        .offset = 0U,
        .range = list_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 3U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.dead_list.resource.buffer,
        .offset = 0U,
        .range = list_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 4U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.spawn_packets.resource.buffer,
        .offset = 0U,
        .range = spawn_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 5U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.indirect_commands.resource.buffer,
        .offset = 0U,
        .range = indirect_bytes,
    });
    render::DescriptorMcVector<render::DescriptorImageWrite> image_writes{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> texel_writes{};
    descriptor_host_.UpdateSet(context_,
                               build_path.update_descriptor_set,
                               buffer_writes,
                               image_writes,
                               texel_writes);

    buffer_writes.clear();
    buffer_writes.reserve(6U);
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = state_write_slot.resource.buffer,
        .offset = 0U,
        .range = state_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 1U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.draw_instances.resource.buffer,
        .offset = 0U,
        .range = static_cast<VkDeviceSize>(result.state_record_count) * sizeof(ecs::Particle2DGpuInstance),
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 2U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.alive_list.resource.buffer,
        .offset = 0U,
        .range = list_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 3U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.dead_list.resource.buffer,
        .offset = 0U,
        .range = list_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 4U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.spawn_packets.resource.buffer,
        .offset = 0U,
        .range = spawn_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 5U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.indirect_commands.resource.buffer,
        .offset = 0U,
        .range = indirect_bytes,
    });
    descriptor_host_.UpdateSet(context_,
                               build_path.build_descriptor_set,
                               buffer_writes,
                               image_writes,
                               texel_writes);
    ++stats.gpu_build_prepare_count;
    result.used_gpu_build = true;
    result.cache_reused = !result.state_upload.uploaded &&
                          !result.spawn_upload.uploaded;
    return result;
}

ParticleSimulationGpuBuildResult ParticleSimulationHost::PrepareGpuBuild3D(
    VulkanContext& context_,
    render::UploadHost& upload_host_,
    render::DescriptorHost& descriptor_host_,
    render::PipelineHost& pipeline_host_,
    std::uint32_t frame_index_,
    const ParticleSimulationFrameResources& frame_resources_,
    const ecs::Particle<ecs::Dim3>* particles_,
    const ecs::ParticleEmitter<ecs::Dim3>* emitters_,
    const ecs::Transform<ecs::Dim3>* transforms_,
    std::uint32_t component_count_,
    const ecs::ParticleRuntimeBuildConfig& build_config_,
    bool cpu_seeded_this_frame_,
    ecs::Particle3DRuntimeScratch& runtime_scratch_,
    const ecs::ParticleRuntimeBuildStats& runtime_stats_) {
    ParticleSimulationGpuBuildResult result{};
    result.resources = frame_resources_;
    if (frame_resources_.resolved_path == ParticleSimulationResolvedPath::cpu) {
        return result;
    }
    (void)component_count_;
    EnsureGpuBuildObjects(context_, descriptor_host_, pipeline_host_);

    FrameState& frame = FrameAt(frame_index_);
    FrameState::BuildPathState& build_path = frame.build_3d;
    BufferSlot& state_read_slot = build_path.state_buffers[frame_resources_.state_read_index];
    BufferSlot& state_write_slot = build_path.state_buffers[frame_resources_.state_write_index];
    build_path.state_read_index = frame_resources_.state_read_index;
    build_path.state_write_index = frame_resources_.state_write_index;
    BuildGpuStateRecords(particles_,
                         emitters_,
                         transforms_,
                         component_count_,
                         runtime_scratch_,
                         frame_resources_.resolved_path == ParticleSimulationResolvedPath::gpu,
                         gpu_build_scratch_3d.state_records,
                         gpu_build_scratch_3d.component_ranges);
    BuildSpawnPackets(particles_,
                      emitters_,
                      transforms_,
                      component_count_,
                      build_config_,
                      cpu_seeded_this_frame_,
                      runtime_scratch_,
                      gpu_build_scratch_3d.component_ranges,
                      gpu_build_scratch_3d.spawn_packets);
    BuildIndirectCommands(runtime_scratch_.draw_batches,
                          gpu_build_scratch_3d.component_ranges,
                          gpu_build_scratch_3d.indirect_commands);

    result.state_record_count = static_cast<std::uint32_t>(gpu_build_scratch_3d.state_records.size());
    result.resources.spawn_packets = MakeBufferView(context_, build_path.spawn_packets);
    result.indirect_command_count = static_cast<std::uint32_t>(gpu_build_scratch_3d.indirect_commands.size());
    build_path.state_record_count = result.state_record_count;
    build_path.spawn_packet_count = static_cast<std::uint32_t>(gpu_build_scratch_3d.spawn_packets.size());
    build_path.indirect_command_count = result.indirect_command_count;
    build_path.gpu_sort_enabled = RequiresGpuParticleSort(particles_, runtime_scratch_);
    build_path.dispatch_group_count =
        (result.state_record_count + k_particle_build_group_size - 1U) / k_particle_build_group_size;
    build_path.update_delta_time_s =
        (frame_resources_.resolved_path == ParticleSimulationResolvedPath::gpu && !cpu_seeded_this_frame_)
            ? (build_config_.delta_time_s > 0.0F ? build_config_.delta_time_s : build_config_.fixed_step_s)
            : 0.0F;

    const std::uint64_t build_revision =
        HashPodSpan(gpu_build_scratch_3d.state_records.data(), gpu_build_scratch_3d.state_records.size());
    const std::uint64_t spawn_revision =
        HashPodSpan(gpu_build_scratch_3d.spawn_packets.data(), gpu_build_scratch_3d.spawn_packets.size());
    const std::uint64_t indirect_revision = simulation_epoch;
    const VkDeviceSize state_bytes = static_cast<VkDeviceSize>(result.state_record_count) * sizeof(ParticleGpuStateRecord);
    const VkDeviceSize spawn_bytes = static_cast<VkDeviceSize>(build_path.spawn_packet_count) * sizeof(ParticleGpuSpawnPacket);
    const VkDeviceSize indirect_bytes = static_cast<VkDeviceSize>(result.indirect_command_count) * sizeof(ParticleGpuIndirectCommand);
    const VkDeviceSize list_bytes = static_cast<VkDeviceSize>(result.state_record_count) * sizeof(ParticleGpuAliveEntry);

    if (result.state_record_count > 0U &&
        !(build_path.state_uploaded_revision == build_revision &&
          build_path.state_uploaded_size_bytes == state_bytes)) {
        upload_host_.StageAndRecordCopyBuffer(frame_index_,
                                              state_read_slot.resource.buffer,
                                              0U,
                                              gpu_build_scratch_3d.state_records.data(),
                                              state_bytes,
                                              16U);
        if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
            VkBufferMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            barrier.buffer = state_read_slot.resource.buffer;
            barrier.offset = 0U;
            barrier.size = state_bytes;
            upload_host_.RecordBufferBarrier2(frame_index_, barrier);
        }
        build_path.state_uploaded_revision = build_revision;
        build_path.state_uploaded_size_bytes = state_bytes;
        result.state_upload.buffer = state_read_slot.resource.buffer;
        result.state_upload.size_bytes = state_bytes;
        result.state_upload.element_count = result.state_record_count;
        result.state_upload.uploaded_revision = build_revision;
        result.state_upload.uploaded = true;
        ++stats.state_upload_count;
        stats.gpu_build_uploaded_bytes += static_cast<std::uint64_t>(state_bytes);
    }

    if (build_path.spawn_packet_count > 0U &&
        !(build_path.spawn_uploaded_revision == spawn_revision &&
          build_path.spawn_uploaded_size_bytes == spawn_bytes)) {
        upload_host_.StageAndRecordCopyBuffer(frame_index_,
                                              build_path.spawn_packets.resource.buffer,
                                              0U,
                                              gpu_build_scratch_3d.spawn_packets.data(),
                                              spawn_bytes,
                                              16U);
        if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
            VkBufferMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            barrier.buffer = build_path.spawn_packets.resource.buffer;
            barrier.offset = 0U;
            barrier.size = spawn_bytes;
            upload_host_.RecordBufferBarrier2(frame_index_, barrier);
        }
        build_path.spawn_uploaded_revision = spawn_revision;
        build_path.spawn_uploaded_size_bytes = spawn_bytes;
        result.spawn_upload.buffer = build_path.spawn_packets.resource.buffer;
        result.spawn_upload.size_bytes = spawn_bytes;
        result.spawn_upload.element_count = build_path.spawn_packet_count;
        result.spawn_upload.uploaded_revision = spawn_revision;
        result.spawn_upload.uploaded = true;
        ++stats.spawn_packet_upload_count;
        stats.gpu_build_uploaded_bytes += static_cast<std::uint64_t>(spawn_bytes);
    }

    if (result.indirect_command_count > 0U) {
        upload_host_.StageAndRecordCopyBuffer(frame_index_,
                                              build_path.indirect_commands.resource.buffer,
                                              0U,
                                              gpu_build_scratch_3d.indirect_commands.data(),
                                              indirect_bytes,
                                              16U);
        if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
            VkBufferMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask =
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            barrier.dstAccessMask =
                VK_ACCESS_2_SHADER_READ_BIT |
                VK_ACCESS_2_SHADER_WRITE_BIT |
                VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
            barrier.buffer = build_path.indirect_commands.resource.buffer;
            barrier.offset = 0U;
            barrier.size = indirect_bytes;
            upload_host_.RecordBufferBarrier2(frame_index_, barrier);
        }
        build_path.indirect_uploaded_revision = indirect_revision;
        build_path.indirect_uploaded_size_bytes = indirect_bytes;
        result.indirect_upload.buffer = build_path.indirect_commands.resource.buffer;
        result.indirect_upload.size_bytes = indirect_bytes;
        result.indirect_upload.element_count = result.indirect_command_count;
        result.indirect_upload.uploaded_revision = indirect_revision;
        result.indirect_upload.uploaded = true;
        ++stats.indirect_upload_count;
        stats.gpu_build_uploaded_bytes += static_cast<std::uint64_t>(indirect_bytes);
    }

    build_path.prepared_revision = build_revision;
    build_path.update_descriptor_set =
        descriptor_host_.AllocateSet(context_, frame_index_, gpu_build_descriptor_layout_id);
    build_path.build_descriptor_set =
        descriptor_host_.AllocateSet(context_, frame_index_, gpu_build_descriptor_layout_id);
    render::DescriptorMcVector<render::DescriptorBufferWrite> buffer_writes{};
    buffer_writes.reserve(7U);
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = state_read_slot.resource.buffer,
        .offset = 0U,
        .range = state_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 1U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = state_write_slot.resource.buffer,
        .offset = 0U,
        .range = state_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 2U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.alive_list.resource.buffer,
        .offset = 0U,
        .range = list_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 3U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.dead_list.resource.buffer,
        .offset = 0U,
        .range = list_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 4U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.spawn_packets.resource.buffer,
        .offset = 0U,
        .range = spawn_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 5U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.indirect_commands.resource.buffer,
        .offset = 0U,
        .range = indirect_bytes,
    });
    if (build_path.sort_keys.resource.buffer != VK_NULL_HANDLE) {
        buffer_writes.push_back(render::DescriptorBufferWrite{
            .binding = 6U,
            .array_element = 0U,
            .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .buffer = build_path.sort_keys.resource.buffer,
            .offset = 0U,
            .range = static_cast<VkDeviceSize>(build_path.sort_keys.element_capacity) * sizeof(ParticleGpuSortKeyRecord),
        });
    }
    render::DescriptorMcVector<render::DescriptorImageWrite> image_writes{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> texel_writes{};
    descriptor_host_.UpdateSet(context_,
                               build_path.update_descriptor_set,
                               buffer_writes,
                               image_writes,
                               texel_writes);

    buffer_writes.clear();
    buffer_writes.reserve(7U);
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = state_write_slot.resource.buffer,
        .offset = 0U,
        .range = state_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 1U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.draw_instances.resource.buffer,
        .offset = 0U,
        .range = static_cast<VkDeviceSize>(result.state_record_count) * sizeof(ecs::Particle3DGpuInstance),
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 2U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.alive_list.resource.buffer,
        .offset = 0U,
        .range = list_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 3U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.dead_list.resource.buffer,
        .offset = 0U,
        .range = list_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 4U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.spawn_packets.resource.buffer,
        .offset = 0U,
        .range = spawn_bytes,
    });
    buffer_writes.push_back(render::DescriptorBufferWrite{
        .binding = 5U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = build_path.indirect_commands.resource.buffer,
        .offset = 0U,
        .range = indirect_bytes,
    });
    if (build_path.sort_keys.resource.buffer != VK_NULL_HANDLE) {
        buffer_writes.push_back(render::DescriptorBufferWrite{
            .binding = 6U,
            .array_element = 0U,
            .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .buffer = build_path.sort_keys.resource.buffer,
            .offset = 0U,
            .range = static_cast<VkDeviceSize>(build_path.sort_keys.element_capacity) * sizeof(ParticleGpuSortKeyRecord),
        });
    }
    descriptor_host_.UpdateSet(context_,
                               build_path.build_descriptor_set,
                               buffer_writes,
                               image_writes,
                               texel_writes);
    ++stats.gpu_build_prepare_count;
    result.used_gpu_build = true;
    result.cache_reused = !result.state_upload.uploaded &&
                          !result.spawn_upload.uploaded;
    return result;
}

void ParticleSimulationHost::RecordBuild2D(VulkanContext& context_,
                                           render::PipelineHost& pipeline_host_,
                                           std::uint32_t frame_index_,
                                           VkCommandBuffer command_buffer_) {
    if (command_buffer_ == VK_NULL_HANDLE) {
        throw std::runtime_error("ParticleSimulationHost::RecordBuild2D requires valid command buffer");
    }
    FrameState& frame = FrameAt(frame_index_);
    FrameState::BuildPathState& build_path = frame.build_2d;
    BufferSlot& state_write_slot = build_path.state_buffers[build_path.state_write_index];
    if (build_path.state_record_count == 0U ||
        build_path.update_descriptor_set == VK_NULL_HANDLE ||
        build_path.build_descriptor_set == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(command_buffer_,
                      VK_PIPELINE_BIND_POINT_COMPUTE,
                      pipeline_host_.GetComputePipeline(gpu_update_pipeline_2d_id));
    VkDescriptorSet descriptor_set = build_path.update_descriptor_set;
    vkCmdBindDescriptorSets(command_buffer_,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id),
                            0U,
                            1U,
                            &descriptor_set,
                            0U,
                            nullptr);
    ParticleBuildPushConstants push_constants{};
    push_constants.record_count = build_path.state_record_count;
    push_constants.spawn_packet_count = build_path.spawn_packet_count;
    push_constants.delta_time_s = build_path.update_delta_time_s;
    push_constants.flags = build_path.indirect_command_count;
    vkCmdPushConstants(command_buffer_,
                       pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id),
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0U,
                       sizeof(ParticleBuildPushConstants),
                       &push_constants);
    vkCmdDispatch(command_buffer_, build_path.dispatch_group_count, 1U, 1U);
    ++stats.update_dispatch_count;
    ++stats.gpu_build_dispatch_count;

    if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
        std::array<VkBufferMemoryBarrier2, 2U> barriers{};
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barriers[0].buffer = state_write_slot.resource.buffer;
        barriers[0].offset = 0U;
        barriers[0].size = static_cast<VkDeviceSize>(build_path.state_record_count) * sizeof(ParticleGpuStateRecord);
        barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barriers[1].buffer = build_path.alive_list.resource.buffer;
        barriers[1].offset = 0U;
        barriers[1].size = static_cast<VkDeviceSize>(build_path.state_record_count) * sizeof(ParticleGpuAliveEntry);
        VkDependencyInfo dependency_info{};
        dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency_info.bufferMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
        dependency_info.pBufferMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2(command_buffer_, &dependency_info);
    }

    vkCmdBindPipeline(command_buffer_,
                      VK_PIPELINE_BIND_POINT_COMPUTE,
                      pipeline_host_.GetComputePipeline(gpu_build_pipeline_2d_id));
    descriptor_set = build_path.build_descriptor_set;
    vkCmdBindDescriptorSets(command_buffer_,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id),
                            0U,
                            1U,
                            &descriptor_set,
                            0U,
                            nullptr);
    vkCmdPushConstants(command_buffer_,
                       pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id),
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0U,
                       sizeof(ParticleBuildPushConstants),
                       &push_constants);
    vkCmdDispatch(command_buffer_, build_path.dispatch_group_count, 1U, 1U);
    ++stats.gpu_build_dispatch_count;

    if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
        std::array<VkBufferMemoryBarrier2, 2U> barriers{};
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        barriers[0].buffer = build_path.draw_instances.resource.buffer;
        barriers[0].offset = 0U;
        barriers[0].size = static_cast<VkDeviceSize>(build_path.state_record_count) * sizeof(ecs::Particle2DGpuInstance);
        barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        barriers[1].buffer = build_path.indirect_commands.resource.buffer;
        barriers[1].offset = 0U;
        barriers[1].size = static_cast<VkDeviceSize>(build_path.indirect_command_count) * sizeof(ParticleGpuIndirectCommand);
        VkDependencyInfo dependency_info{};
        dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency_info.bufferMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
        dependency_info.pBufferMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2(command_buffer_, &dependency_info);
    }
}

void ParticleSimulationHost::RecordBuild3D(VulkanContext& context_,
                                           render::PipelineHost& pipeline_host_,
                                           std::uint32_t frame_index_,
                                           const ecs::Float3& sort_origin_,
                                           const ecs::Float3& sort_direction_,
                                           VkCommandBuffer command_buffer_) {
    if (command_buffer_ == VK_NULL_HANDLE) {
        throw std::runtime_error("ParticleSimulationHost::RecordBuild3D requires valid command buffer");
    }
    FrameState& frame = FrameAt(frame_index_);
    FrameState::BuildPathState& build_path = frame.build_3d;
    BufferSlot& state_write_slot = build_path.state_buffers[build_path.state_write_index];
    if (build_path.state_record_count == 0U ||
        build_path.update_descriptor_set == VK_NULL_HANDLE ||
        build_path.build_descriptor_set == VK_NULL_HANDLE) {
        return;
    }

    vkCmdBindPipeline(command_buffer_,
                      VK_PIPELINE_BIND_POINT_COMPUTE,
                      pipeline_host_.GetComputePipeline(gpu_update_pipeline_3d_id));
    VkDescriptorSet descriptor_set = build_path.update_descriptor_set;
    vkCmdBindDescriptorSets(command_buffer_,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id),
                            0U,
                            1U,
                            &descriptor_set,
                            0U,
                            nullptr);
    ParticleBuildPushConstants push_constants{};
    push_constants.record_count = build_path.state_record_count;
    push_constants.spawn_packet_count = build_path.spawn_packet_count;
    push_constants.delta_time_s = build_path.update_delta_time_s;
    push_constants.flags =
        (build_path.indirect_command_count & k_particle_build_flag_batch_count_mask) |
        (build_path.gpu_sort_enabled ? k_particle_build_flag_enable_sort : 0U);
    push_constants.sort_origin_x = sort_origin_.x;
    push_constants.sort_origin_y = sort_origin_.y;
    push_constants.sort_origin_z = sort_origin_.z;
    push_constants.sort_direction_x = sort_direction_.x;
    push_constants.sort_direction_y = sort_direction_.y;
    push_constants.sort_direction_z = sort_direction_.z;
    vkCmdPushConstants(command_buffer_,
                       pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id),
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0U,
                       sizeof(ParticleBuildPushConstants),
                       &push_constants);
    vkCmdDispatch(command_buffer_, build_path.dispatch_group_count, 1U, 1U);
    ++stats.update_dispatch_count;
    ++stats.gpu_build_dispatch_count;

    if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
        std::array<VkBufferMemoryBarrier2, 3U> barriers{};
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barriers[0].buffer = state_write_slot.resource.buffer;
        barriers[0].offset = 0U;
        barriers[0].size = static_cast<VkDeviceSize>(build_path.state_record_count) * sizeof(ParticleGpuStateRecord);
        barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barriers[1].buffer = build_path.alive_list.resource.buffer;
        barriers[1].offset = 0U;
        barriers[1].size = static_cast<VkDeviceSize>(build_path.state_record_count) * sizeof(ParticleGpuAliveEntry);
        barriers[2].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[2].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[2].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barriers[2].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[2].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        barriers[2].buffer = build_path.sort_keys.resource.buffer;
        barriers[2].offset = 0U;
        barriers[2].size = static_cast<VkDeviceSize>(build_path.sort_keys.element_capacity) * sizeof(ParticleGpuSortKeyRecord);
        VkDependencyInfo dependency_info{};
        dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency_info.bufferMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
        dependency_info.pBufferMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2(command_buffer_, &dependency_info);
    }

    if (build_path.gpu_sort_enabled &&
        build_path.indirect_command_count > 0U &&
        build_path.sort_keys.resource.buffer != VK_NULL_HANDLE) {
        vkCmdBindPipeline(command_buffer_,
                          VK_PIPELINE_BIND_POINT_COMPUTE,
                          pipeline_host_.GetComputePipeline(gpu_sort_pipeline_3d_id));
        descriptor_set = build_path.build_descriptor_set;
        vkCmdBindDescriptorSets(command_buffer_,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id),
                                0U,
                                1U,
                                &descriptor_set,
                                0U,
                                nullptr);

        const auto& indirect_commands = gpu_build_scratch_3d.indirect_commands;
        for (std::uint32_t batch_index = 0U;
             batch_index < build_path.indirect_command_count && batch_index < indirect_commands.size();
             ++batch_index) {
            const std::uint32_t first_instance = indirect_commands[batch_index].draw.firstInstance;
            const std::uint32_t next_first_instance =
                (batch_index + 1U) < indirect_commands.size()
                    ? indirect_commands[batch_index + 1U].draw.firstInstance
                    : build_path.state_record_count;
            const std::uint32_t segment_capacity = next_first_instance > first_instance
                ? (next_first_instance - first_instance)
                : 0U;
            if (segment_capacity <= 1U) {
                continue;
            }

            const std::uint32_t padded_capacity = static_cast<std::uint32_t>(NextPow2(segment_capacity));
            const std::uint32_t dispatch_group_count =
                (segment_capacity + k_particle_build_group_size - 1U) / k_particle_build_group_size;

            for (std::uint32_t stage = 2U; stage <= padded_capacity; stage <<= 1U) {
                for (std::uint32_t pass = stage >> 1U; pass > 0U; pass >>= 1U) {
                    ParticleSortPushConstants sort_push_constants{};
                    sort_push_constants.segment_capacity = segment_capacity;
                    sort_push_constants.batch_index = batch_index;
                    sort_push_constants.flags = ((stage & 0xFFFFU) << 16U) | (pass & 0xFFFFU);
                    vkCmdPushConstants(command_buffer_,
                                       pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id),
                                       VK_SHADER_STAGE_COMPUTE_BIT,
                                       0U,
                                       sizeof(ParticleSortPushConstants),
                                       &sort_push_constants);
                    vkCmdDispatch(command_buffer_, dispatch_group_count, 1U, 1U);
                    ++stats.sort_dispatch_count;
                    ++stats.gpu_build_dispatch_count;

                    if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
                        VkBufferMemoryBarrier2 barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                        barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
                        barrier.buffer = build_path.sort_keys.resource.buffer;
                        barrier.offset = static_cast<VkDeviceSize>(first_instance) * sizeof(ParticleGpuSortKeyRecord);
                        barrier.size = static_cast<VkDeviceSize>(segment_capacity) * sizeof(ParticleGpuSortKeyRecord);
                        VkDependencyInfo dependency_info{};
                        dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        dependency_info.bufferMemoryBarrierCount = 1U;
                        dependency_info.pBufferMemoryBarriers = &barrier;
                        vkCmdPipelineBarrier2(command_buffer_, &dependency_info);
                    }
                }
            }
        }
    }

    vkCmdBindPipeline(command_buffer_,
                      VK_PIPELINE_BIND_POINT_COMPUTE,
                      pipeline_host_.GetComputePipeline(gpu_build_pipeline_3d_id));
    descriptor_set = build_path.build_descriptor_set;
    vkCmdBindDescriptorSets(command_buffer_,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id),
                            0U,
                            1U,
                            &descriptor_set,
                            0U,
                            nullptr);
    vkCmdPushConstants(command_buffer_,
                       pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id),
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0U,
                       sizeof(ParticleBuildPushConstants),
                       &push_constants);
    vkCmdDispatch(command_buffer_, build_path.dispatch_group_count, 1U, 1U);
    ++stats.gpu_build_dispatch_count;

    if (context_.EnabledVulkan13Features().synchronization2 == VK_TRUE) {
        std::array<VkBufferMemoryBarrier2, 2U> barriers{};
        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        barriers[0].buffer = build_path.draw_instances.resource.buffer;
        barriers[0].offset = 0U;
        barriers[0].size = static_cast<VkDeviceSize>(build_path.state_record_count) * sizeof(ecs::Particle3DGpuInstance);
        barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        barriers[1].buffer = build_path.indirect_commands.resource.buffer;
        barriers[1].offset = 0U;
        barriers[1].size = static_cast<VkDeviceSize>(build_path.indirect_command_count) * sizeof(ParticleGpuIndirectCommand);
        VkDependencyInfo dependency_info{};
        dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependency_info.bufferMemoryBarrierCount = static_cast<std::uint32_t>(barriers.size());
        dependency_info.pBufferMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2(command_buffer_, &dependency_info);
    }
}

ParticleSimulationResolvedPath ParticleSimulationHost::ResolveSimulationPath(
    ecs::ParticleSimulationMode requested_mode_,
    const ParticleSimulationHostCapabilities& capabilities_) noexcept {
    switch (requested_mode_) {
    case ecs::ParticleSimulationMode::gpu:
        return capabilities_.SupportsGpuSimulation()
            ? ParticleSimulationResolvedPath::gpu
            : ParticleSimulationResolvedPath::cpu;
    case ecs::ParticleSimulationMode::hybrid_gpu:
        return capabilities_.SupportsHybridSimulation()
            ? ParticleSimulationResolvedPath::hybrid_gpu
            : ParticleSimulationResolvedPath::cpu;
    case ecs::ParticleSimulationMode::cpu:
    default:
        return ParticleSimulationResolvedPath::cpu;
    }
}

ParticleSimulationHostCapabilities ParticleSimulationHost::QueryCapabilities(
    const VulkanContext& context_) noexcept {
    ParticleSimulationHostCapabilities queried{};
    queried.compute_queue_available =
        context_.IsDeviceInitialized() &&
        context_.ComputeQueue() != VK_NULL_HANDLE &&
        context_.QueueFamilies().compute.has_value();
    queried.synchronization2 =
        context_.EnabledVulkan13Features().synchronization2 == VK_TRUE;
    queried.buffer_device_address =
        context_.EnabledVulkan12Features().bufferDeviceAddress == VK_TRUE;
    queried.descriptor_indexing =
        context_.EnabledVulkan12Features().runtimeDescriptorArray == VK_TRUE ||
        context_.EnabledVulkan12Features().descriptorBindingPartiallyBound == VK_TRUE ||
        context_.EnabledVulkan12Features().descriptorBindingVariableDescriptorCount == VK_TRUE ||
        context_.EnabledVulkan12Features().shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
    queried.draw_indirect_count = context_.IsDeviceInitialized();
    queried.shader_int64 = context_.EnabledFeatures().shaderInt64 == VK_TRUE;
    return queried;
}

bool ParticleSimulationHost::NeedsCpuSeed2D(std::uint32_t frame_index_) const noexcept {
    if (!initialized || frames.empty()) {
        return true;
    }
    const FrameState& frame = FrameAt(frame_index_);
    const FrameState::BuildPathState& build_path = frame.build_2d;
    return build_path.state_uploaded_revision == 0U || build_path.state_record_count == 0U;
}

bool ParticleSimulationHost::NeedsCpuSeed3D(std::uint32_t frame_index_) const noexcept {
    if (!initialized || frames.empty()) {
        return true;
    }
    const FrameState& frame = FrameAt(frame_index_);
    const FrameState::BuildPathState& build_path = frame.build_3d;
    return build_path.state_uploaded_revision == 0U || build_path.state_record_count == 0U;
}

bool ParticleSimulationHost::HasPersistentState2D() const noexcept {
    if (!initialized) {
        return false;
    }
    for (const FrameState& frame : frames) {
        const FrameState::BuildPathState& build_path = frame.build_2d;
        if (build_path.state_uploaded_revision != 0U && build_path.state_record_count > 0U) {
            return true;
        }
    }
    return false;
}

bool ParticleSimulationHost::HasPersistentState3D() const noexcept {
    if (!initialized) {
        return false;
    }
    for (const FrameState& frame : frames) {
        const FrameState::BuildPathState& build_path = frame.build_3d;
        if (build_path.state_uploaded_revision != 0U && build_path.state_record_count > 0U) {
            return true;
        }
    }
    return false;
}

bool ParticleSimulationHost::IsInitialized() const noexcept {
    return initialized;
}

std::uint32_t ParticleSimulationHost::FramesInFlight() const noexcept {
    return create_info_cache.frames_in_flight;
}

const ParticleSimulationHostCreateInfo& ParticleSimulationHost::CreateInfo() const noexcept {
    return create_info_cache;
}

const ParticleSimulationHostStats& ParticleSimulationHost::Stats() const noexcept {
    return stats;
}

const ParticleSimulationHostCapabilities& ParticleSimulationHost::Capabilities() const noexcept {
    return capabilities;
}

bool ParticleSimulationHost::HasComputeTimelineProgress() const noexcept {
    return false;
}

std::uint64_t ParticleSimulationHost::LastSubmittedValue() const noexcept {
    return 0U;
}

std::uint64_t ParticleSimulationHost::CompletedSubmitValue() const noexcept {
    return 0U;
}

std::uint64_t ParticleSimulationHost::NextSignalValue() const noexcept {
    return 1U;
}

VkDeviceSize ParticleSimulationHost::NextPow2(VkDeviceSize value_) noexcept {
    if (value_ <= 1U) {
        return 1U;
    }
    --value_;
    value_ |= (value_ >> 1U);
    value_ |= (value_ >> 2U);
    value_ |= (value_ >> 4U);
    value_ |= (value_ >> 8U);
    value_ |= (value_ >> 16U);
    value_ |= (value_ >> 32U);
    return value_ + 1U;
}

VkDeviceSize ParticleSimulationHost::BufferSizeForElements(std::uint32_t element_capacity_,
                                                           VkDeviceSize element_size_) noexcept {
    return static_cast<VkDeviceSize>(std::max<std::uint32_t>(1U, element_capacity_)) * element_size_;
}

VkBufferUsageFlags ParticleSimulationHost::BuildBufferUsageFlags(
    VkBufferUsageFlags base_usage_) const noexcept {
    VkBufferUsageFlags usage = base_usage_;
    if (capabilities.buffer_device_address && create_info_cache.prefer_buffer_device_address) {
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    return usage;
}

VkDeviceAddress ParticleSimulationHost::QueryBufferDeviceAddress(
    VulkanContext& context_,
    const resource::BufferResource& resource_) const noexcept {
    if (!capabilities.buffer_device_address ||
        !create_info_cache.prefer_buffer_device_address ||
        context_.Device() == VK_NULL_HANDLE ||
        resource_.buffer == VK_NULL_HANDLE) {
        return 0U;
    }

    VkBufferDeviceAddressInfo address_info{};
    address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    address_info.buffer = resource_.buffer;
    return vkGetBufferDeviceAddress(context_.Device(), &address_info);
}

ParticleSimulationBufferView ParticleSimulationHost::MakeBufferView(
    VulkanContext& context_,
    const BufferSlot& slot_) const noexcept {
    ParticleSimulationBufferView view{};
    view.buffer = slot_.resource.buffer;
    view.size_bytes = slot_.capacity_bytes;
    view.device_address = QueryBufferDeviceAddress(context_, slot_.resource);
    view.element_capacity = slot_.element_capacity;
    return view;
}

void ParticleSimulationHost::EnsureBufferCapacity(VulkanContext& context_,
                                                  BufferSlot& slot_,
                                                  std::uint32_t required_element_capacity_,
                                                  VkDeviceSize element_size_,
                                                  VkBufferUsageFlags usage_,
                                                  std::uint32_t& resize_counter_) {
    if (required_element_capacity_ == 0U) {
        return;
    }
    if (slot_.resource.buffer != VK_NULL_HANDLE &&
        slot_.element_capacity >= required_element_capacity_) {
        return;
    }
    if (slot_.resource.buffer != VK_NULL_HANDLE && !create_info_cache.allow_growth) {
        throw std::runtime_error("ParticleSimulationHost buffer growth requested but allow_growth is disabled");
    }

    const VkDeviceSize requested_bytes = BufferSizeForElements(required_element_capacity_, element_size_);
    const VkDeviceSize target_bytes = NextPow2(requested_bytes);
    const std::uint32_t target_elements = static_cast<std::uint32_t>(
        std::max<VkDeviceSize>(1U, target_bytes / std::max<VkDeviceSize>(1U, element_size_)));

    if (slot_.resource.buffer != VK_NULL_HANDLE) {
        RetireBuffer(slot_, ComputeRetireValue());
    }

    resource::BufferCreateInfo create_info{};
    create_info.size = target_bytes;
    create_info.usage = BuildBufferUsageFlags(usage_);
    create_info.memory_properties = create_info_cache.memory_properties;
    slot_.resource = resource::BufferHost::CreateBuffer(context_, create_info, *gpu_memory_host);
    slot_.capacity_bytes = target_bytes;
    slot_.element_capacity = target_elements;
    ++resize_counter_;
    ++stats.revision;
}

void ParticleSimulationHost::RetireBuffer(BufferSlot& slot_,
                                          std::uint64_t retire_value_) {
    if (slot_.resource.buffer == VK_NULL_HANDLE) {
        return;
    }
    retired_buffers.Retire(std::move(slot_.resource), retire_value_);
    slot_ = {};
}

void ParticleSimulationHost::CollectRetiredBuffers(VulkanContext& context_,
                                                   std::uint64_t completed_submit_value_) {
    stats.retired_buffer_count += retired_buffers.Collect(
        completed_submit_value_,
        [&](resource::BufferResource& resource_) {
            resource::BufferHost::DestroyBuffer(context_, resource_);
        });
}

void ParticleSimulationHost::DestroyRetiredBuffers(VulkanContext& context_) noexcept {
    stats.retired_buffer_count += retired_buffers.Flush(
        [&](resource::BufferResource& resource_) {
            resource::BufferHost::DestroyBuffer(context_, resource_);
        });
}

void ParticleSimulationHost::DestroyFrameState(VulkanContext& context_,
                                               FrameState& frame_) noexcept {
    auto destroy_slot = [&](BufferSlot& slot_) {
        if (slot_.resource.buffer != VK_NULL_HANDLE) {
            resource::BufferHost::DestroyBuffer(context_, slot_.resource);
        }
        slot_ = {};
    };
    destroy_slot(frame_.build_2d.alive_list);
    destroy_slot(frame_.build_2d.dead_list);
    destroy_slot(frame_.build_2d.state_buffers[0]);
    destroy_slot(frame_.build_2d.state_buffers[1]);
    destroy_slot(frame_.build_2d.spawn_packets);
    destroy_slot(frame_.build_2d.draw_instances);
    destroy_slot(frame_.build_2d.indirect_commands);
    destroy_slot(frame_.build_2d.sort_keys);
    destroy_slot(frame_.build_2d.sort_indices);
    destroy_slot(frame_.build_3d.alive_list);
    destroy_slot(frame_.build_3d.dead_list);
    destroy_slot(frame_.build_3d.state_buffers[0]);
    destroy_slot(frame_.build_3d.state_buffers[1]);
    destroy_slot(frame_.build_3d.spawn_packets);
    destroy_slot(frame_.build_3d.draw_instances);
    destroy_slot(frame_.build_3d.indirect_commands);
    destroy_slot(frame_.build_3d.sort_keys);
    destroy_slot(frame_.build_3d.sort_indices);
    frame_ = {};
}

void ParticleSimulationHost::EnsureGpuBuildObjects(VulkanContext& context_,
                                                   render::DescriptorHost& descriptor_host_,
                                                   render::PipelineHost& pipeline_host_) {
    if (gpu_build_descriptor_layout_id.IsValid() &&
        gpu_build_pipeline_layout_id.IsValid() &&
        gpu_update_shader_2d_id.IsValid() &&
        gpu_update_shader_3d_id.IsValid() &&
        gpu_build_shader_2d_id.IsValid() &&
        gpu_build_shader_3d_id.IsValid() &&
        gpu_sort_shader_3d_id.IsValid() &&
        gpu_update_pipeline_2d_id.IsValid() &&
        gpu_update_pipeline_3d_id.IsValid() &&
        gpu_build_pipeline_2d_id.IsValid() &&
        gpu_build_pipeline_3d_id.IsValid() &&
        gpu_sort_pipeline_3d_id.IsValid()) {
        return;
    }

    if (!gpu_build_descriptor_layout_id.IsValid()) {
        render::DescriptorSetLayoutDesc layout_desc{};
        layout_desc.bindings.reserve(7U);
        layout_desc.bindings.push_back(VkDescriptorSetLayoutBinding{
            .binding = 0U,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1U,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        });
        layout_desc.bindings.push_back(VkDescriptorSetLayoutBinding{
            .binding = 1U,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1U,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        });
        layout_desc.bindings.push_back(VkDescriptorSetLayoutBinding{
            .binding = 2U,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1U,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        });
        layout_desc.bindings.push_back(VkDescriptorSetLayoutBinding{
            .binding = 3U,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1U,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        });
        layout_desc.bindings.push_back(VkDescriptorSetLayoutBinding{
            .binding = 4U,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1U,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        });
        layout_desc.bindings.push_back(VkDescriptorSetLayoutBinding{
            .binding = 5U,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1U,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        });
        layout_desc.bindings.push_back(VkDescriptorSetLayoutBinding{
            .binding = 6U,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1U,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .pImmutableSamplers = nullptr,
        });
        gpu_build_descriptor_layout_id = descriptor_host_.RegisterLayout(context_, layout_desc);
    }

    if (!gpu_build_pipeline_layout_id.IsValid()) {
        render::PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(descriptor_host_.GetLayout(gpu_build_descriptor_layout_id));
        layout_desc.push_constant_ranges.push_back(render::PushConstantRangeDesc{
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0U,
            .size = sizeof(ParticleBuildPushConstants),
        });
        gpu_build_pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (!gpu_update_shader_2d_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_particle_update_2d_comp_spv;
        shader_create_info.word_count = static_cast<std::uint32_t>(
            sizeof(generated::k_particle_update_2d_comp_spv) / sizeof(std::uint32_t));
        gpu_update_shader_2d_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!gpu_update_shader_3d_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_particle_update_3d_comp_spv;
        shader_create_info.word_count = static_cast<std::uint32_t>(
            sizeof(generated::k_particle_update_3d_comp_spv) / sizeof(std::uint32_t));
        gpu_update_shader_3d_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!gpu_build_shader_2d_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_particle_build_2d_comp_spv;
        shader_create_info.word_count = static_cast<std::uint32_t>(
            sizeof(generated::k_particle_build_2d_comp_spv) / sizeof(std::uint32_t));
        gpu_build_shader_2d_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!gpu_build_shader_3d_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_particle_build_3d_comp_spv;
        shader_create_info.word_count = static_cast<std::uint32_t>(
            sizeof(generated::k_particle_build_3d_comp_spv) / sizeof(std::uint32_t));
        gpu_build_shader_3d_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!gpu_sort_shader_3d_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_particle_sort_3d_comp_spv;
        shader_create_info.word_count = static_cast<std::uint32_t>(
            sizeof(generated::k_particle_sort_3d_comp_spv) / sizeof(std::uint32_t));
        gpu_sort_shader_3d_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!gpu_update_pipeline_2d_id.IsValid()) {
        render::ComputePipelineDesc pipeline_desc{};
        pipeline_desc.layout = pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id);
        pipeline_desc.shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_desc.shader_stage.module = pipeline_host_.GetShaderModule(gpu_update_shader_2d_id);
        pipeline_desc.shader_stage.entry_name = "main";
        gpu_update_pipeline_2d_id = pipeline_host_.RegisterComputePipeline(context_, pipeline_desc);
    }
    if (!gpu_update_pipeline_3d_id.IsValid()) {
        render::ComputePipelineDesc pipeline_desc{};
        pipeline_desc.layout = pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id);
        pipeline_desc.shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_desc.shader_stage.module = pipeline_host_.GetShaderModule(gpu_update_shader_3d_id);
        pipeline_desc.shader_stage.entry_name = "main";
        gpu_update_pipeline_3d_id = pipeline_host_.RegisterComputePipeline(context_, pipeline_desc);
    }
    if (!gpu_build_pipeline_2d_id.IsValid()) {
        render::ComputePipelineDesc pipeline_desc{};
        pipeline_desc.layout = pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id);
        pipeline_desc.shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_desc.shader_stage.module = pipeline_host_.GetShaderModule(gpu_build_shader_2d_id);
        pipeline_desc.shader_stage.entry_name = "main";
        gpu_build_pipeline_2d_id = pipeline_host_.RegisterComputePipeline(context_, pipeline_desc);
    }
    if (!gpu_build_pipeline_3d_id.IsValid()) {
        render::ComputePipelineDesc pipeline_desc{};
        pipeline_desc.layout = pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id);
        pipeline_desc.shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_desc.shader_stage.module = pipeline_host_.GetShaderModule(gpu_build_shader_3d_id);
        pipeline_desc.shader_stage.entry_name = "main";
        gpu_build_pipeline_3d_id = pipeline_host_.RegisterComputePipeline(context_, pipeline_desc);
    }
    if (!gpu_sort_pipeline_3d_id.IsValid()) {
        render::ComputePipelineDesc pipeline_desc{};
        pipeline_desc.layout = pipeline_host_.GetPipelineLayout(gpu_build_pipeline_layout_id);
        pipeline_desc.shader_stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipeline_desc.shader_stage.module = pipeline_host_.GetShaderModule(gpu_sort_shader_3d_id);
        pipeline_desc.shader_stage.entry_name = "main";
        gpu_sort_pipeline_3d_id = pipeline_host_.RegisterComputePipeline(context_, pipeline_desc);
    }
}

std::uint64_t ParticleSimulationHost::ComputeRetireValue() const noexcept {
    return std::max(last_submitted_value_seen, completed_submit_value_seen + 1U);
}

ParticleSimulationHost::FrameState& ParticleSimulationHost::FrameAt(std::uint32_t frame_index_) {
    if (frames.empty()) {
        throw std::runtime_error("ParticleSimulationHost::FrameAt has no frames");
    }
    return frames[frame_index_ % frames.size()];
}

const ParticleSimulationHost::FrameState& ParticleSimulationHost::FrameAt(
    std::uint32_t frame_index_) const {
    if (frames.empty()) {
        throw std::runtime_error("ParticleSimulationHost::FrameAt has no frames");
    }
    return frames[frame_index_ % frames.size()];
}

} // namespace vr::particle

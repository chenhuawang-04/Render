#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/geometry_batch_system.hpp"
#include "vr/ecs/system/geometry_path_system.hpp"
#include "vr/ecs/system/spatial_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vr::ecs {

template<typename T>
using GeometryRuntimeMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct Geometry2DPathPrimitive final {
    float x0;
    float y0;
    float x1;
    float y1;
    std::uint32_t fill_color_rgba8;
    std::uint32_t stroke_color_rgba8;
    float stroke_width_px;
    std::uint32_t params;
    std::uint32_t component_index;
    std::uint32_t user_data;
};

struct Geometry2DDrawBatch final {
    std::uint64_t sort_key;
    std::uint32_t primitive_begin;
    std::uint32_t primitive_count;
    std::uint32_t geometry_id;
    std::uint32_t material_id;
    std::uint32_t first_component_index;
    std::uint32_t params;
};

struct Geometry2DRuntimeBuildConfig final {
    std::uint32_t quad_subdivision = 8U;
    std::uint32_t cubic_subdivision = 12U;
    std::uint32_t max_primitives_per_component = 0U;
    float zero_length_epsilon = 1e-6F;
    bool build_ordered_indices = true;
};

struct Geometry2DRuntimeBuildStats final {
    GeometryBatchBuildStats batch{};
    std::uint32_t emitted_primitive_count = 0U;
    std::uint32_t emitted_batch_count = 0U;
    std::uint32_t approximated_quad_count = 0U;
    std::uint32_t approximated_cubic_count = 0U;
    std::uint32_t truncated_component_count = 0U;
};

struct Geometry2DRuntimeScratch final {
    GeometryBatchScratch<Dim2> batch_scratch{};
    GeometryRuntimeMcVector<Geometry2DPathPrimitive> primitives{};
    GeometryRuntimeMcVector<Geometry2DDrawBatch> draw_batches{};
};

struct Geometry3DGpuInstance final {
    float world_m00;
    float world_m01;
    float world_m02;
    float world_m03;

    float world_m10;
    float world_m11;
    float world_m12;
    float world_m13;

    float world_m20;
    float world_m21;
    float world_m22;
    float world_m23;

    float world_m30;
    float world_m31;
    float world_m32;
    float world_m33;

    float bounds_min_x;
    float bounds_min_y;
    float bounds_min_z;
    float reserved0;

    float bounds_max_x;
    float bounds_max_y;
    float bounds_max_z;
    float reserved1;

    float metallic;
    float roughness;
    float normal_scale;
    float line_width;

    std::uint32_t albedo_rgba8;
    std::uint32_t params;
    std::uint32_t geometry_id;
    std::uint32_t material_id;

    std::uint32_t submesh_index;
    std::uint32_t component_index;
    std::uint32_t user_data;
    std::uint32_t reserved2;
};

struct Geometry3DDrawBatch final {
    std::uint64_t sort_key;
    std::uint32_t instance_begin;
    std::uint32_t instance_count;
    std::uint32_t geometry_id;
    std::uint32_t material_id;
    std::uint32_t submesh_index;
    std::uint32_t first_component_index;
    std::uint32_t params;
};

struct Geometry3DRuntimeBuildConfig final {
    bool build_ordered_indices = true;
};

struct Geometry3DRuntimeBuildStats final {
    GeometryBatchBuildStats batch{};
    std::uint32_t emitted_instance_count = 0U;
    std::uint32_t emitted_batch_count = 0U;
    std::uint32_t depth_test_batch_count = 0U;
    std::uint32_t depth_write_batch_count = 0U;
    std::uint32_t shadow_cast_batch_count = 0U;
};

struct Geometry3DRuntimeScratch final {
    GeometryBatchScratch<Dim3> batch_scratch{};
    GeometryRuntimeMcVector<Geometry3DGpuInstance> instances{};
    GeometryRuntimeMcVector<Geometry3DDrawBatch> draw_batches{};
};

static_assert(PurePodGeometryComponent<Geometry2DPathPrimitive>);
static_assert(PurePodGeometryComponent<Geometry2DDrawBatch>);
static_assert(PurePodGeometryComponent<Geometry3DGpuInstance>);
static_assert(PurePodGeometryComponent<Geometry3DDrawBatch>);
static_assert(alignof(Geometry3DGpuInstance) == 4U);

template<DimensionTag DimensionT>
class GeometryRuntimeSystem;

template<>
class GeometryRuntimeSystem<Dim2> final {
public:
    using GeometryType = Geometry<Dim2>;
    using BatchSystemType = GeometryBatchSystem<Dim2>;
    using ScratchType = Geometry2DRuntimeScratch;

    static void Reserve(ScratchType& scratch_,
                        std::uint32_t component_count_,
                        std::uint32_t primitive_capacity_hint_ = 0U) {
        BatchSystemType::Reserve(scratch_.batch_scratch, component_count_);

        const std::size_t primitive_reserve = static_cast<std::size_t>(
            primitive_capacity_hint_ > 0U ? primitive_capacity_hint_ : component_count_);
        if (scratch_.primitives.capacity() < primitive_reserve) {
            scratch_.primitives.reserve(primitive_reserve);
        }
        if (scratch_.draw_batches.capacity() < primitive_reserve) {
            scratch_.draw_batches.reserve(primitive_reserve);
        }
    }

    [[nodiscard]] static Geometry2DRuntimeBuildStats Build(const GeometryType* components_,
                                                           std::uint32_t component_count_,
                                                           ScratchType& scratch_,
                                                           const Geometry2DRuntimeBuildConfig& build_config_ = {}) {
        Geometry2DRuntimeBuildStats stats{};
        scratch_.primitives.clear();
        scratch_.draw_batches.clear();

        if (components_ == nullptr || component_count_ == 0U) {
            return stats;
        }

        stats.batch = BatchSystemType::BuildAndSort(components_,
                                                    component_count_,
                                                    scratch_.batch_scratch,
                                                    build_config_.build_ordered_indices);
        if (stats.batch.visible_count == 0U) {
            return stats;
        }

        const std::uint32_t quad_subdivision = std::max(1U, build_config_.quad_subdivision);
        const std::uint32_t cubic_subdivision = std::max(1U, build_config_.cubic_subdivision);
        const float zero_length_epsilon = std::max(0.0F, build_config_.zero_length_epsilon);

        const GeometryBatchItem* sorted_items = BatchSystemType::SortedItems(scratch_.batch_scratch);
        const std::uint32_t sorted_count = BatchSystemType::VisibleCount(scratch_.batch_scratch);

        for (std::uint32_t i = 0U; i < sorted_count; ++i) {
            const GeometryBatchItem& item = sorted_items[i];
            const GeometryType& component = components_[item.component_index];

            const std::uint32_t primitive_begin = static_cast<std::uint32_t>(scratch_.primitives.size());
            const bool truncated = EmitPathPrimitives(component,
                                                      item.component_index,
                                                      quad_subdivision,
                                                      cubic_subdivision,
                                                      build_config_.max_primitives_per_component,
                                                      zero_length_epsilon,
                                                      scratch_);
            if (truncated) {
                ++stats.truncated_component_count;
            }

            const std::uint32_t primitive_count =
                static_cast<std::uint32_t>(scratch_.primitives.size()) - primitive_begin;
            if (primitive_count == 0U) {
                continue;
            }

            AppendOrMergeBatch(component,
                               item.sort_key,
                               item.component_index,
                               primitive_begin,
                               primitive_count,
                               scratch_);
        }

        stats.emitted_primitive_count = static_cast<std::uint32_t>(scratch_.primitives.size());
        stats.emitted_batch_count = static_cast<std::uint32_t>(scratch_.draw_batches.size());
        stats.approximated_quad_count = quad_subdivision;
        stats.approximated_cubic_count = cubic_subdivision;
        return stats;
    }

private:
    struct PathPenState final {
        Float2 current_point{.x = 0.0F, .y = 0.0F};
        Float2 subpath_start{.x = 0.0F, .y = 0.0F};
        bool has_current = false;
    };

    [[nodiscard]] static std::uint32_t PackRgba8(const Rgba8& color_) noexcept {
        return static_cast<std::uint32_t>(color_.r) |
               (static_cast<std::uint32_t>(color_.g) << 8U) |
               (static_cast<std::uint32_t>(color_.b) << 16U) |
               (static_cast<std::uint32_t>(color_.a) << 24U);
    }

    [[nodiscard]] static std::uint32_t PackStyleParams(const GeometryType& component_) noexcept {
        std::uint32_t params = 0U;
        params |= (component_.style.antialiasing != 0U) ? 0x1U : 0U;
        params |= (static_cast<std::uint32_t>(component_.style.topology) & 0x3U) << 1U;
        params |= (static_cast<std::uint32_t>(component_.style.fill_rule) & 0x1U) << 3U;
        params |= (static_cast<std::uint32_t>(component_.style.line_join) & 0x3U) << 4U;
        params |= (static_cast<std::uint32_t>(component_.style.line_cap) & 0x3U) << 6U;
        return params;
    }

    [[nodiscard]] static float LengthSquared(const Float2& delta_) noexcept {
        return delta_.x * delta_.x + delta_.y * delta_.y;
    }

    [[nodiscard]] static bool TryEmitLineSegment(const GeometryType& component_,
                                                 std::uint32_t component_index_,
                                                 const Float2& p0_,
                                                 const Float2& p1_,
                                                 float zero_length_epsilon_,
                                                 ScratchType& scratch_) {
        const Float2 delta{
            .x = p1_.x - p0_.x,
            .y = p1_.y - p0_.y
        };
        if (LengthSquared(delta) <= zero_length_epsilon_ * zero_length_epsilon_) {
            return false;
        }

        Geometry2DPathPrimitive primitive{};
        primitive.x0 = p0_.x;
        primitive.y0 = p0_.y;
        primitive.x1 = p1_.x;
        primitive.y1 = p1_.y;
        primitive.fill_color_rgba8 = PackRgba8(component_.style.fill_color);
        primitive.stroke_color_rgba8 = PackRgba8(component_.style.stroke_color);
        primitive.stroke_width_px = component_.style.stroke_width_px;
        primitive.params = PackStyleParams(component_);
        primitive.component_index = component_index_;
        primitive.user_data = component_.runtime.route.user_data;
        scratch_.primitives.push_back(primitive);
        return true;
    }

    [[nodiscard]] static Float2 Lerp(const Float2& a_, const Float2& b_, float t_) noexcept {
        return Float2{
            .x = a_.x + (b_.x - a_.x) * t_,
            .y = a_.y + (b_.y - a_.y) * t_,
        };
    }

    [[nodiscard]] static Float2 EvaluateQuadratic(const Float2& p0_,
                                                  const Float2& c_,
                                                  const Float2& p1_,
                                                  float t_) noexcept {
        const Float2 a = Lerp(p0_, c_, t_);
        const Float2 b = Lerp(c_, p1_, t_);
        return Lerp(a, b, t_);
    }

    [[nodiscard]] static Float2 EvaluateCubic(const Float2& p0_,
                                              const Float2& c0_,
                                              const Float2& c1_,
                                              const Float2& p1_,
                                              float t_) noexcept {
        const Float2 a = Lerp(p0_, c0_, t_);
        const Float2 b = Lerp(c0_, c1_, t_);
        const Float2 c = Lerp(c1_, p1_, t_);
        const Float2 d = Lerp(a, b, t_);
        const Float2 e = Lerp(b, c, t_);
        return Lerp(d, e, t_);
    }

    [[nodiscard]] static bool EmitPathPrimitives(const GeometryType& component_,
                                                 std::uint32_t component_index_,
                                                 std::uint32_t quad_subdivision_,
                                                 std::uint32_t cubic_subdivision_,
                                                 std::uint32_t max_primitives_per_component_,
                                                 float zero_length_epsilon_,
                                                 ScratchType& scratch_) {
        PathPenState state{};
        const std::uint32_t primitive_begin = static_cast<std::uint32_t>(scratch_.primitives.size());
        bool truncated = false;

        GeometryPathSystem::ForEachCommandRaw(component_,
                                              [&](const GeometryPathCommandView& command_view_) {
                                                  const std::uint32_t emitted_count =
                                                      static_cast<std::uint32_t>(scratch_.primitives.size()) -
                                                      primitive_begin;
                                                  if (max_primitives_per_component_ > 0U &&
                                                      emitted_count >= max_primitives_per_component_) {
                                                      truncated = true;
                                                      return;
                                                  }

                                                  switch (command_view_.type) {
                                                  case GeometryPathCommandType::move_to:
                                                      if (command_view_.size_bytes == sizeof(GeometryPathMoveToCommand)) {
                                                          const auto* command =
                                                              reinterpret_cast<const GeometryPathMoveToCommand*>(
                                                                  command_view_.bytes);
                                                          state.current_point = command->to;
                                                          state.subpath_start = command->to;
                                                          state.has_current = true;
                                                      }
                                                      break;
                                                  case GeometryPathCommandType::line_to:
                                                      if (command_view_.size_bytes == sizeof(GeometryPathLineToCommand)) {
                                                          const auto* command =
                                                              reinterpret_cast<const GeometryPathLineToCommand*>(
                                                                  command_view_.bytes);
                                                          if (!state.has_current) {
                                                              state.current_point = command->to;
                                                              state.subpath_start = command->to;
                                                              state.has_current = true;
                                                              break;
                                                          }
                                                          if (TryEmitLineSegment(component_,
                                                                                 component_index_,
                                                                                 state.current_point,
                                                                                 command->to,
                                                                                 zero_length_epsilon_,
                                                                                 scratch_)) {
                                                              state.current_point = command->to;
                                                          }
                                                      }
                                                      break;
                                                  case GeometryPathCommandType::quad_to:
                                                      if (command_view_.size_bytes == sizeof(GeometryPathQuadToCommand)) {
                                                          const auto* command =
                                                              reinterpret_cast<const GeometryPathQuadToCommand*>(
                                                                  command_view_.bytes);
                                                          if (!state.has_current) {
                                                              state.current_point = command->to;
                                                              state.subpath_start = command->to;
                                                              state.has_current = true;
                                                              break;
                                                          }

                                                          Float2 previous = state.current_point;
                                                          for (std::uint32_t step = 1U; step <= quad_subdivision_; ++step) {
                                                              const float t = static_cast<float>(step) /
                                                                              static_cast<float>(quad_subdivision_);
                                                              const Float2 current =
                                                                  EvaluateQuadratic(state.current_point,
                                                                                    command->control,
                                                                                    command->to,
                                                                                    t);
                                                              (void)TryEmitLineSegment(component_,
                                                                                       component_index_,
                                                                                       previous,
                                                                                       current,
                                                                                       zero_length_epsilon_,
                                                                                       scratch_);
                                                              previous = current;
                                                          }
                                                          state.current_point = command->to;
                                                      }
                                                      break;
                                                  case GeometryPathCommandType::cubic_to:
                                                      if (command_view_.size_bytes == sizeof(GeometryPathCubicToCommand)) {
                                                          const auto* command =
                                                              reinterpret_cast<const GeometryPathCubicToCommand*>(
                                                                  command_view_.bytes);
                                                          if (!state.has_current) {
                                                              state.current_point = command->to;
                                                              state.subpath_start = command->to;
                                                              state.has_current = true;
                                                              break;
                                                          }

                                                          Float2 previous = state.current_point;
                                                          for (std::uint32_t step = 1U; step <= cubic_subdivision_; ++step) {
                                                              const float t = static_cast<float>(step) /
                                                                              static_cast<float>(cubic_subdivision_);
                                                              const Float2 current =
                                                                  EvaluateCubic(state.current_point,
                                                                                command->control0,
                                                                                command->control1,
                                                                                command->to,
                                                                                t);
                                                              (void)TryEmitLineSegment(component_,
                                                                                       component_index_,
                                                                                       previous,
                                                                                       current,
                                                                                       zero_length_epsilon_,
                                                                                       scratch_);
                                                              previous = current;
                                                          }
                                                          state.current_point = command->to;
                                                      }
                                                      break;
                                                  case GeometryPathCommandType::close:
                                                      if (command_view_.size_bytes == sizeof(GeometryPathCloseCommand) &&
                                                          state.has_current) {
                                                          (void)TryEmitLineSegment(component_,
                                                                                   component_index_,
                                                                                   state.current_point,
                                                                                   state.subpath_start,
                                                                                   zero_length_epsilon_,
                                                                                   scratch_);
                                                          state.current_point = state.subpath_start;
                                                      }
                                                      break;
                                                  default:
                                                      break;
                                                  }
                                              });
        return truncated;
    }

    static void AppendOrMergeBatch(const GeometryType& component_,
                                   std::uint64_t sort_key_,
                                   std::uint32_t component_index_,
                                   std::uint32_t primitive_begin_,
                                   std::uint32_t primitive_count_,
                                   ScratchType& scratch_) {
        if (primitive_count_ == 0U) {
            return;
        }

        const std::uint32_t params = PackStyleParams(component_);
        if (!scratch_.draw_batches.empty()) {
            Geometry2DDrawBatch& last = scratch_.draw_batches.back();
            if (last.sort_key == sort_key_ &&
                last.geometry_id == component_.runtime.route.geometry_id &&
                last.material_id == component_.runtime.route.material_id &&
                last.params == params &&
                last.primitive_begin + last.primitive_count == primitive_begin_) {
                last.primitive_count += primitive_count_;
                return;
            }
        }

        Geometry2DDrawBatch batch{};
        batch.sort_key = sort_key_;
        batch.primitive_begin = primitive_begin_;
        batch.primitive_count = primitive_count_;
        batch.geometry_id = component_.runtime.route.geometry_id;
        batch.material_id = component_.runtime.route.material_id;
        batch.first_component_index = component_index_;
        batch.params = params;
        scratch_.draw_batches.push_back(batch);
    }
};

template<>
class GeometryRuntimeSystem<Dim3> final {
public:
    using GeometryType = Geometry<Dim3>;
    using TransformType = Transform<Dim3>;
    using BatchSystemType = GeometryBatchSystem<Dim3>;
    using ScratchType = Geometry3DRuntimeScratch;

    static void Reserve(ScratchType& scratch_,
                        std::uint32_t component_count_,
                        std::uint32_t instance_capacity_hint_ = 0U) {
        BatchSystemType::Reserve(scratch_.batch_scratch, component_count_);

        const std::size_t instance_reserve = static_cast<std::size_t>(
            instance_capacity_hint_ > 0U ? instance_capacity_hint_ : component_count_);
        if (scratch_.instances.capacity() < instance_reserve) {
            scratch_.instances.reserve(instance_reserve);
        }
        if (scratch_.draw_batches.capacity() < instance_reserve) {
            scratch_.draw_batches.reserve(instance_reserve);
        }
    }

    [[nodiscard]] static Geometry3DRuntimeBuildStats Build(const GeometryType* components_,
                                                           const TransformType* transforms_,
                                                           std::uint32_t component_count_,
                                                           ScratchType& scratch_,
                                                           const Geometry3DRuntimeBuildConfig& build_config_ = {}) {
        Geometry3DRuntimeBuildStats stats{};
        scratch_.instances.clear();
        scratch_.draw_batches.clear();

        if (components_ == nullptr || component_count_ == 0U) {
            return stats;
        }

        stats.batch = BatchSystemType::BuildAndSort(components_,
                                                    component_count_,
                                                    scratch_.batch_scratch,
                                                    build_config_.build_ordered_indices);
        if (stats.batch.visible_count == 0U) {
            return stats;
        }

        const GeometryBatchItem* sorted_items = BatchSystemType::SortedItems(scratch_.batch_scratch);
        const std::uint32_t sorted_count = BatchSystemType::VisibleCount(scratch_.batch_scratch);
        scratch_.instances.resize(sorted_count);

        for (std::uint32_t i = 0U; i < sorted_count; ++i) {
            const GeometryBatchItem& item = sorted_items[i];
            const GeometryType& component = components_[item.component_index];

            Matrix4x4 world_matrix = spatial_math::IdentityMatrix4x4();
            if (transforms_ != nullptr && item.component_index < component_count_) {
                world_matrix = transforms_[item.component_index].runtime.world_matrix;
            }

            Geometry3DGpuInstance instance{};
            instance.world_m00 = world_matrix.m[0];
            instance.world_m01 = world_matrix.m[1];
            instance.world_m02 = world_matrix.m[2];
            instance.world_m03 = world_matrix.m[3];
            instance.world_m10 = world_matrix.m[4];
            instance.world_m11 = world_matrix.m[5];
            instance.world_m12 = world_matrix.m[6];
            instance.world_m13 = world_matrix.m[7];
            instance.world_m20 = world_matrix.m[8];
            instance.world_m21 = world_matrix.m[9];
            instance.world_m22 = world_matrix.m[10];
            instance.world_m23 = world_matrix.m[11];
            instance.world_m30 = world_matrix.m[12];
            instance.world_m31 = world_matrix.m[13];
            instance.world_m32 = world_matrix.m[14];
            instance.world_m33 = world_matrix.m[15];

            instance.bounds_min_x = component.runtime.bounds_min.x;
            instance.bounds_min_y = component.runtime.bounds_min.y;
            instance.bounds_min_z = component.runtime.bounds_min.z;
            instance.reserved0 = 0.0F;
            instance.bounds_max_x = component.runtime.bounds_max.x;
            instance.bounds_max_y = component.runtime.bounds_max.y;
            instance.bounds_max_z = component.runtime.bounds_max.z;
            instance.reserved1 = 0.0F;

            instance.metallic = component.style.metallic;
            instance.roughness = component.style.roughness;
            instance.normal_scale = component.style.normal_scale;
            instance.line_width = component.style.line_width;

            instance.albedo_rgba8 = PackRgba8(component.style.albedo_color);
            instance.params = PackParams(component);
            instance.geometry_id = component.runtime.route.geometry_id;
            instance.material_id = component.runtime.route.material_id;
            instance.submesh_index = component.mesh.submesh_index;
            instance.component_index = item.component_index;
            instance.user_data = component.runtime.route.user_data;
            instance.reserved2 = 0U;
            scratch_.instances[i] = instance;

            AppendOrMergeBatch(component,
                               item.sort_key,
                               item.component_index,
                               i,
                               scratch_);
        }

        for (const Geometry3DDrawBatch& batch : scratch_.draw_batches) {
            if ((batch.params & 0x1U) != 0U) {
                ++stats.depth_test_batch_count;
            }
            if ((batch.params & 0x2U) != 0U) {
                ++stats.depth_write_batch_count;
            }
            if ((batch.params & 0x8U) != 0U) {
                ++stats.shadow_cast_batch_count;
            }
        }

        stats.emitted_instance_count = static_cast<std::uint32_t>(scratch_.instances.size());
        stats.emitted_batch_count = static_cast<std::uint32_t>(scratch_.draw_batches.size());
        return stats;
    }

private:
    [[nodiscard]] static std::uint32_t PackRgba8(const Rgba8& color_) noexcept {
        return static_cast<std::uint32_t>(color_.r) |
               (static_cast<std::uint32_t>(color_.g) << 8U) |
               (static_cast<std::uint32_t>(color_.b) << 16U) |
               (static_cast<std::uint32_t>(color_.a) << 24U);
    }

    [[nodiscard]] static std::uint32_t PackParams(const GeometryType& component_) noexcept {
        std::uint32_t params = 0U;
        params |= (component_.style.depth_test != 0U) ? 0x1U : 0U;
        params |= (component_.style.depth_write != 0U) ? 0x2U : 0U;
        params |= (component_.style.double_sided != 0U) ? 0x4U : 0U;
        params |= (component_.style.cast_shadow != 0U) ? 0x8U : 0U;
        params |= (component_.style.receive_shadow != 0U) ? 0x10U : 0U;
        params |= (static_cast<std::uint32_t>(component_.style.topology) & 0x3U) << 5U;
        params |= (static_cast<std::uint32_t>(component_.style.shading_model) & 0x1U) << 7U;
        params |= (static_cast<std::uint32_t>(component_.mesh.lod_index) & 0xFFFFU) << 8U;
        return params;
    }

    static void AppendOrMergeBatch(const GeometryType& component_,
                                   std::uint64_t sort_key_,
                                   std::uint32_t component_index_,
                                   std::uint32_t instance_index_,
                                   ScratchType& scratch_) {
        const std::uint32_t params = PackParams(component_);
        if (!scratch_.draw_batches.empty()) {
            Geometry3DDrawBatch& last = scratch_.draw_batches.back();
            if (last.sort_key == sort_key_ &&
                last.geometry_id == component_.runtime.route.geometry_id &&
                last.material_id == component_.runtime.route.material_id &&
                last.submesh_index == component_.mesh.submesh_index &&
                last.params == params &&
                last.instance_begin + last.instance_count == instance_index_) {
                ++last.instance_count;
                return;
            }
        }

        Geometry3DDrawBatch batch{};
        batch.sort_key = sort_key_;
        batch.instance_begin = instance_index_;
        batch.instance_count = 1U;
        batch.geometry_id = component_.runtime.route.geometry_id;
        batch.material_id = component_.runtime.route.material_id;
        batch.submesh_index = component_.mesh.submesh_index;
        batch.first_component_index = component_index_;
        batch.params = params;
        scratch_.draw_batches.push_back(batch);
    }
};

} // namespace vr::ecs

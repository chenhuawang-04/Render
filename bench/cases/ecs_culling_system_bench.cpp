#include "Center/Memory/Container/Vector/McVector.hpp"
#include "support/bench_framework.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/culling_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
using CullingSystem3D = vr::ecs::CullingSystem<vr::ecs::Dim3>;
using CullingScratch3D = vr::ecs::CullingScratch<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

template<typename T>
using CullingBenchMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

constexpr std::uint32_t k_component_count = 16384U;
static_assert((k_component_count & (k_component_count - 1U)) == 0U);

void WriteWorldBounds(Bounds3D& bounds_,
                      const vr::ecs::Float3& center_,
                      const vr::ecs::Float3& extents_,
                      std::uint32_t visibility_mask_) {
    const float extent_x = std::max(0.0F, extents_.x);
    const float extent_y = std::max(0.0F, extents_.y);
    const float extent_z = std::max(0.0F, extents_.z);
    bounds_.runtime.world_center = center_;
    bounds_.runtime.world_extents = vr::ecs::Float3{.x = extent_x, .y = extent_y, .z = extent_z};
    bounds_.runtime.world_min = vr::ecs::Float3{
        .x = center_.x - extent_x,
        .y = center_.y - extent_y,
        .z = center_.z - extent_z
    };
    bounds_.runtime.world_max = vr::ecs::Float3{
        .x = center_.x + extent_x,
        .y = center_.y + extent_y,
        .z = center_.z + extent_z
    };
    bounds_.runtime.world_radius = std::sqrt(extent_x * extent_x + extent_y * extent_y + extent_z * extent_z);
    bounds_.runtime.visibility_mask = visibility_mask_;
    bounds_.runtime.dirty_flags = 0U;
}

void InitializeCullingScene(CullingBenchMcVector<Bounds3D>& bounds_,
                            CullingBenchMcVector<std::uint32_t>& candidate_indices_,
                            Camera3D& camera_) {
    bounds_.resize(k_component_count);
    candidate_indices_.clear();
    candidate_indices_.reserve(k_component_count / 4U);

    for (std::uint32_t i = 0U; i < k_component_count; ++i) {
        BoundsSystem3D::Initialize(bounds_[i]);

        const float x = (static_cast<float>(i % 128U) - 64.0F) * 2.2F;
        const float y = (static_cast<float>((i / 128U) % 64U) - 32.0F) * 1.8F;
        float z = -(4.0F + static_cast<float>(i % 96U) * 1.5F);
        if ((i % 5U) == 0U) {
            z += 320.0F;
        }

        const vr::ecs::Float3 center{
            .x = x,
            .y = y,
            .z = z
        };
        const vr::ecs::Float3 extents{
            .x = 0.35F + static_cast<float>(i % 3U) * 0.15F,
            .y = 0.35F + static_cast<float>(i % 5U) * 0.10F,
            .z = 0.35F + static_cast<float>(i % 7U) * 0.08F
        };
        const std::uint32_t visibility_mask = ((i & 1U) == 0U) ? 0x1U : 0x2U;
        WriteWorldBounds(bounds_[i], center, extents, visibility_mask);

        if ((i & 3U) == 0U) {
            candidate_indices_.push_back(i);
        }
    }

    Transform3D camera_transform{};
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform, vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 12.0F});
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);

    CameraSystem3D::Initialize(camera_);
    CameraSystem3D::SetAspectRatio(camera_, 16.0F / 9.0F);
    CameraSystem3D::SetVerticalFovRadians(camera_, 1.0471975512F);
    CameraSystem3D::SetNearFar(camera_, 0.1F, 512.0F);
    CameraSystem3D::SetCullingMask(camera_, 0x1U);
    CameraSystem3D::MarkViewDirty(camera_);
    CameraSystem3D::Update(camera_, camera_transform);
}

VR_BENCHMARK_CASE(EcsCullingSystem_dim3_full_scan_16k, "core;ecs;culling;cpu") {
    CullingBenchMcVector<Bounds3D> bounds{};
    CullingBenchMcVector<std::uint32_t> candidate_indices{};
    Camera3D camera{};
    InitializeCullingScene(bounds, candidate_indices, camera);

    CullingScratch3D scratch{};
    const vr::ecs::CullingBuildOptions options{
        .enable_culling_mask_filter = true,
        .enable_frustum_culling = true,
        .enable_aabb_refine = true,
        .write_visibility_bits = false
    };
    const CullingSystem3D::PreparedCamera prepared_camera =
        CullingSystem3D::PrepareCamera(&camera, options);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i) & (k_component_count - 1U);
        Bounds3D& hot_bounds = bounds[hot_index];
        hot_bounds.runtime.world_center.x += 0.00125F;
        hot_bounds.runtime.world_min.x += 0.00125F;
        hot_bounds.runtime.world_max.x += 0.00125F;

        const vr::ecs::CullingBuildStats stats = CullingSystem3D::BuildVisibleSet(bounds.data(),
                                                                                   k_component_count,
                                                                                   prepared_camera,
                                                                                   scratch,
                                                                                   options);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.visible_count);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.plane_test_count);
    }

    bench_context_.AddItems(iterations * k_component_count);
    bench_context_.AddBytes(iterations * k_component_count * sizeof(Bounds3D));
    vr::bench::BenchmarkContext::DoNotOptimize(CullingSystem3D::VisibleIndices(scratch).size());
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsCullingSystem_dim3_candidate_scan_4k_of_16k, "core;ecs;culling;cpu") {
    CullingBenchMcVector<Bounds3D> bounds{};
    CullingBenchMcVector<std::uint32_t> candidate_indices{};
    Camera3D camera{};
    InitializeCullingScene(bounds, candidate_indices, camera);

    CullingScratch3D scratch{};
    const vr::ecs::CullingBuildOptions options{
        .enable_culling_mask_filter = true,
        .enable_frustum_culling = true,
        .enable_aabb_refine = true,
        .write_visibility_bits = false
    };
    const CullingSystem3D::PreparedCamera prepared_camera =
        CullingSystem3D::PrepareCamera(&camera, options);

    const std::uint32_t candidate_count = static_cast<std::uint32_t>(candidate_indices.size());
    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_slot = static_cast<std::uint32_t>(i) & (candidate_count - 1U);
        const std::uint32_t hot_index = candidate_indices[hot_slot];
        Bounds3D& hot_bounds = bounds[hot_index];
        hot_bounds.runtime.world_center.y -= 0.00110F;
        hot_bounds.runtime.world_min.y -= 0.00110F;
        hot_bounds.runtime.world_max.y -= 0.00110F;

        const vr::ecs::CullingBuildStats stats = CullingSystem3D::BuildVisibleSetFromCandidates(
            bounds.data(),
            k_component_count,
            candidate_indices.data(),
            candidate_count,
            prepared_camera,
            scratch,
            options);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.visible_count);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.plane_test_count);
    }

    bench_context_.AddItems(iterations * candidate_count);
    bench_context_.AddBytes(iterations * candidate_count * sizeof(Bounds3D));
    vr::bench::BenchmarkContext::DoNotOptimize(CullingSystem3D::VisibleIndices(scratch).size());
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "support/bench_framework.hpp"
#include "vr/ecs/system/bounds_system.hpp"
#include "vr/ecs/system/transform_system.hpp"

#include <cstdint>

namespace {

using Bounds3D = vr::ecs::Bounds<vr::ecs::Dim3>;
using BoundsSystem3D = vr::ecs::BoundsSystem<vr::ecs::Dim3>;
using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;

template<typename T>
using BoundsBenchMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

constexpr std::uint32_t k_component_count = 4096U;
static_assert((k_component_count & (k_component_count - 1U)) == 0U);

void InitializeBoundsScene(BoundsBenchMcVector<Bounds3D>& bounds_,
                           BoundsBenchMcVector<Transform3D>& transforms_) {
    bounds_.resize(k_component_count);
    transforms_.resize(k_component_count);

    for (std::uint32_t i = 0U; i < k_component_count; ++i) {
        BoundsSystem3D::Initialize(bounds_[i]);
        BoundsSystem3D::SetLocalCenterExtents(bounds_[i],
                                              vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.0F},
                                              vr::ecs::Float3{
                                                  .x = 0.5F + static_cast<float>(i % 5U) * 0.2F,
                                                  .y = 0.5F + static_cast<float>(i % 7U) * 0.16F,
                                                  .z = 0.5F + static_cast<float>(i % 3U) * 0.25F});

        TransformSystem3D::Initialize(transforms_[i]);
        TransformSystem3D::SetLocalPosition(transforms_[i],
                                            vr::ecs::Float3{
                                                .x = static_cast<float>(i % 64U) * 0.28F - 8.5F,
                                                .y = static_cast<float>((i / 64U) % 64U) * 0.21F - 6.0F,
                                                .z = static_cast<float>(i % 13U) * 0.14F - 1.1F});
        TransformSystem3D::SetLocalRotationEulerXyz(transforms_[i],
                                                    0.0F,
                                                    static_cast<float>(i % 360U) * 0.01745329251994329577F,
                                                    0.0F);
        TransformSystem3D::SetLocalScale(transforms_[i],
                                         vr::ecs::Float3{
                                             .x = 0.8F + static_cast<float>(i % 4U) * 0.15F,
                                             .y = 0.9F + static_cast<float>(i % 5U) * 0.12F,
                                             .z = 0.7F + static_cast<float>(i % 3U) * 0.19F});
    }

    TransformSystem3D::UpdateHierarchy(transforms_.data(), k_component_count);
    (void)BoundsSystem3D::UpdateAligned(bounds_.data(), transforms_.data(), k_component_count);
}

VR_BENCHMARK_CASE(EcsBoundsSystem_dim3_update_aligned_4k_full_scan, "core;ecs;bounds;cpu") {
    BoundsBenchMcVector<Bounds3D> bounds{};
    BoundsBenchMcVector<Transform3D> transforms{};
    InitializeBoundsScene(bounds, transforms);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i) & (k_component_count - 1U);
        Transform3D& hot_transform = transforms[hot_index];
        hot_transform.runtime.world_matrix.m[12] += 0.005F;
        hot_transform.runtime.world_matrix.m[13] -= 0.003F;
        hot_transform.runtime.world_matrix.m[14] += 0.001F;
        ++hot_transform.runtime.world_revision;

        const std::uint32_t updated_count =
            BoundsSystem3D::UpdateAligned(bounds.data(), transforms.data(), k_component_count);
        vr::bench::BenchmarkContext::DoNotOptimize(updated_count);
    }

    bench_context_.AddItems(iterations * k_component_count);
    bench_context_.AddBytes(iterations * k_component_count * sizeof(Bounds3D));
    vr::bench::BenchmarkContext::DoNotOptimize(bounds[0U].runtime.world_revision);
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsBoundsSystem_dim3_update_aligned_4k_dirty_indices, "core;ecs;bounds;cpu") {
    BoundsBenchMcVector<Bounds3D> bounds{};
    BoundsBenchMcVector<Transform3D> transforms{};
    InitializeBoundsScene(bounds, transforms);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i) & (k_component_count - 1U);
        Transform3D& hot_transform = transforms[hot_index];
        hot_transform.runtime.world_matrix.m[12] += 0.005F;
        hot_transform.runtime.world_matrix.m[13] -= 0.003F;
        hot_transform.runtime.world_matrix.m[14] += 0.001F;
        ++hot_transform.runtime.world_revision;

        const std::uint32_t updated_count = BoundsSystem3D::UpdateAligned(bounds.data(),
                                                                          transforms.data(),
                                                                          k_component_count,
                                                                          &hot_index,
                                                                          1U);
        vr::bench::BenchmarkContext::DoNotOptimize(updated_count);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * sizeof(Bounds3D));
    vr::bench::BenchmarkContext::DoNotOptimize(bounds[0U].runtime.world_revision);
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace


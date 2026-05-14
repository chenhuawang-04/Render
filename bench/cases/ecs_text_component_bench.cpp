#include "support/bench_framework.hpp"
#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/text_batch_system.hpp"
#include "vr/ecs/system/text_system.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace {

template<typename T>
using BenchMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

VR_BENCHMARK_CASE(EcsTextSystem_dim2_set_text_hot_path, "core;ecs;text;cpu") {
    using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;

    constexpr std::array<std::string_view, 6U> samples{
        "FPS",
        "Frame Time",
        "Player",
        "Coordinates",
        "Health",
        "Objective"
    };

    vr::ecs::Text<vr::ecs::Dim2> text_component{};
    TextSystem2D::Initialize(text_component);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::string_view text = samples[static_cast<std::size_t>(i % samples.size())];
        (void)TextSystem2D::SetText(text_component, text);
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * 8U);
    vr::bench::BenchmarkContext::DoNotOptimize(TextSystem2D::Revision(text_component));
    vr::bench::BenchmarkContext::DoNotOptimize(TextSystem2D::TextSizeBytes(text_component));
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsTextSystem_dim2_append_text_hot_path, "core;ecs;text;cpu") {
    using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;

    vr::ecs::Text<vr::ecs::Dim2> text_component{};
    TextSystem2D::Initialize(text_component);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        TextSystem2D::ClearText(text_component);
        (void)TextSystem2D::AppendText(text_component, "Entity#");
        (void)TextSystem2D::AppendText(text_component, "42");
        (void)TextSystem2D::AppendText(text_component, "::Visible");
    }

    bench_context_.AddItems(iterations * 3U);
    bench_context_.AddBytes(iterations * 16U);
    vr::bench::BenchmarkContext::DoNotOptimize(TextSystem2D::Revision(text_component));
    vr::bench::BenchmarkContext::DoNotOptimize(TextSystem2D::TextSizeBytes(text_component));
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsTextSystem_dim3_set_append_route, "core;ecs;text;cpu") {
    using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;

    vr::ecs::Text<vr::ecs::Dim3> text_component{};
    TextSystem3D::Initialize(text_component);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        (void)TextSystem3D::SetText(text_component, "Waypoint");
        (void)TextSystem3D::AppendText(text_component, " ");
        (void)TextSystem3D::AppendText(text_component, "A");
    }

    bench_context_.AddItems(iterations);
    bench_context_.AddBytes(iterations * 9U);
    vr::bench::BenchmarkContext::DoNotOptimize(TextSystem3D::Revision(text_component));
    vr::bench::BenchmarkContext::DoNotOptimize(TextSystem3D::TextSizeBytes(text_component));
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsTextBatchSystem_dim2_build_visible_only_4k, "core;ecs;text;batch;cpu") {
    using Text2D = vr::ecs::Text<vr::ecs::Dim2>;
    using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;
    using BatchSystem2D = vr::ecs::TextBatchSystem<vr::ecs::Dim2>;

    constexpr std::uint32_t component_count = 4096U;

    BenchMcVector<Text2D> components{};
    components.resize(component_count);

    for (std::uint32_t i = 0U; i < component_count; ++i) {
        TextSystem2D::Initialize(components[i]);
        (void)TextSystem2D::SetText(components[i], "Label");
        TextSystem2D::SetRuntimeRoute(components[i],
                                      i & 63U,
                                      (i >> 3U) & 1023U,
                                      (i >> 2U) & 127U,
                                      i & 31U);
        TextSystem2D::SetLayer(components[i], static_cast<std::int16_t>((i % 256U) - 128));
    }

    vr::ecs::TextBatchScratch<vr::ecs::Dim2> scratch{};
    BatchSystem2D::Reserve(scratch, component_count);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i & (component_count - 1U));
        TextSystem2D::SetVisible(components[hot_index], (i & 1U) == 0U);

        const vr::ecs::TextBatchBuildStats stats = BatchSystem2D::BuildVisibleItems(components.data(),
                                                                                     component_count,
                                                                                     scratch,
                                                                                     false);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.visible_count);
    }

    bench_context_.AddItems(iterations * component_count);
    bench_context_.AddBytes(iterations * component_count * sizeof(vr::ecs::TextBatchItem));
    vr::bench::BenchmarkContext::DoNotOptimize(BatchSystem2D::VisibleCount(scratch));
    vr::bench::BenchmarkContext::ClobberMemory();
}

VR_BENCHMARK_CASE(EcsTextBatchSystem_dim2_build_and_sort_4k, "core;ecs;text;batch;cpu") {
    using Text2D = vr::ecs::Text<vr::ecs::Dim2>;
    using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;
    using BatchSystem2D = vr::ecs::TextBatchSystem<vr::ecs::Dim2>;

    constexpr std::uint32_t component_count = 4096U;

    BenchMcVector<Text2D> components{};
    components.resize(component_count);

    for (std::uint32_t i = 0U; i < component_count; ++i) {
        TextSystem2D::Initialize(components[i]);
        (void)TextSystem2D::SetText(components[i], "GlyphBatch");
        TextSystem2D::SetRuntimeRoute(components[i],
                                      i & 255U,
                                      (i >> 4U) & 4095U,
                                      (i >> 2U) & 511U,
                                      i & 63U);
        TextSystem2D::SetLayer(components[i], static_cast<std::int16_t>((i % 512U) - 256));
    }

    vr::ecs::TextBatchScratch<vr::ecs::Dim2> scratch{};
    BatchSystem2D::Reserve(scratch, component_count);

    const std::uint64_t iterations = bench_context_.Iterations();
    for (std::uint64_t i = 0U; i < iterations; ++i) {
        const std::uint32_t hot_index = static_cast<std::uint32_t>(i & (component_count - 1U));
        TextSystem2D::SetBatchTag(components[hot_index], static_cast<std::uint32_t>(i));

        const vr::ecs::TextBatchBuildStats stats = BatchSystem2D::BuildAndSort(components.data(),
                                                                                component_count,
                                                                                scratch,
                                                                                false);
        vr::bench::BenchmarkContext::DoNotOptimize(stats.visible_count);
        if (stats.visible_count > 0U) {
            vr::bench::BenchmarkContext::DoNotOptimize(BatchSystem2D::SortedItems(scratch)[0U].sort_key);
        }
    }

    bench_context_.AddItems(iterations * component_count);
    bench_context_.AddBytes(iterations * component_count * sizeof(vr::ecs::TextBatchItem));
    vr::bench::BenchmarkContext::DoNotOptimize(BatchSystem2D::VisibleCount(scratch));
    vr::bench::BenchmarkContext::ClobberMemory();
}

} // namespace


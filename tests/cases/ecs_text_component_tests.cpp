#include "support/test_framework.hpp"
#include "vr/ecs/component/text_component.hpp"
#include "vr/ecs/system/text_batch_system.hpp"
#include "vr/ecs/system/text_system.hpp"

#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

VR_TEST_CASE(EcsTextComponent_is_pure_pod, "unit;core;ecs;text") {
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Text<vr::ecs::Dim2>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Text<vr::ecs::Dim2>>);
    VR_CHECK(std::is_standard_layout_v<vr::ecs::Text<vr::ecs::Dim3>>);
    VR_CHECK(std::is_trivial_v<vr::ecs::Text<vr::ecs::Dim3>>);
}

VR_TEST_CASE(EcsTextSystem_dim2_initialize_defaults_and_edit_text, "unit;core;ecs;text") {
    using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;

    vr::ecs::Text<vr::ecs::Dim2> text_component{};
    TextSystem2D::Initialize(text_component);

    VR_CHECK(text_component.text.capacity_bytes == vr::ecs::TextBufferInlineUtf8::inline_capacity_bytes);
    VR_CHECK(text_component.text.size_bytes == 0U);
    VR_CHECK(text_component.text.revision == 0U);
    VR_CHECK(text_component.style.pixel_size == 18.0F);
    VR_CHECK(text_component.style.horizontal_align == vr::ecs::TextHorizontalAlign::left);
    VR_CHECK(text_component.style.vertical_align == vr::ecs::TextVerticalAlign::top);
    VR_CHECK(text_component.style.enable_sdf == 1U);
    VR_CHECK(text_component.style.layer == 0);
    VR_CHECK(text_component.runtime.visible == 1U);

    VR_CHECK(TextSystem2D::SetText(text_component, "HUD: FPS"));
    VR_CHECK(TextSystem2D::GetText(text_component) == std::string_view("HUD: FPS"));
    VR_CHECK(TextSystem2D::Revision(text_component) == 1U);

    VR_CHECK(TextSystem2D::SetText(text_component, "HUD: FPS"));
    VR_CHECK(TextSystem2D::Revision(text_component) == 1U);

    VR_CHECK(TextSystem2D::AppendText(text_component, " 240"));
    VR_CHECK(TextSystem2D::GetText(text_component) == std::string_view("HUD: FPS 240"));
    VR_CHECK(TextSystem2D::Revision(text_component) == 2U);

    TextSystem2D::ClearText(text_component);
    VR_CHECK(TextSystem2D::GetText(text_component).empty());
    VR_CHECK(TextSystem2D::Revision(text_component) == 3U);
}

VR_TEST_CASE(EcsTextSystem_dim3_initialize_defaults_and_style_update, "unit;core;ecs;text") {
    using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;

    vr::ecs::Text<vr::ecs::Dim3> text_component{};
    TextSystem3D::Initialize(text_component);

    VR_CHECK(text_component.style.world_size == 0.25F);
    VR_CHECK(text_component.style.max_screen_size_px == 144.0F);
    VR_CHECK(text_component.style.billboard == 1U);
    VR_CHECK(text_component.style.depth_test == 1U);
    VR_CHECK(text_component.style.depth_write == 0U);
    VR_CHECK(text_component.style.enable_sdf == 1U);
    VR_CHECK(text_component.runtime.pass_hint == vr::ecs::TextRenderPassHint::transparent);

    TextSystem3D::SetColor(text_component, vr::ecs::Rgba8{20U, 30U, 40U, 255U});
    TextSystem3D::SetHorizontalAlign(text_component, vr::ecs::TextHorizontalAlign::right);
    TextSystem3D::SetVerticalAlign(text_component, vr::ecs::TextVerticalAlign::bottom);
    TextSystem3D::SetWorldSize(text_component, 0.75F);
    TextSystem3D::SetMaxScreenSizePx(text_component, 220.0F);
    TextSystem3D::SetBillboard(text_component, false);
    TextSystem3D::SetDepthTest(text_component, false);
    TextSystem3D::SetDepthWrite(text_component, true);

    VR_CHECK(text_component.style.color.r == 20U);
    VR_CHECK(text_component.style.color.g == 30U);
    VR_CHECK(text_component.style.color.b == 40U);
    VR_CHECK(text_component.style.horizontal_align == vr::ecs::TextHorizontalAlign::right);
    VR_CHECK(text_component.style.vertical_align == vr::ecs::TextVerticalAlign::bottom);
    VR_CHECK(text_component.style.world_size == 0.75F);
    VR_CHECK(text_component.style.max_screen_size_px == 220.0F);
    VR_CHECK(text_component.style.billboard == 0U);
    VR_CHECK(text_component.style.depth_test == 0U);
    VR_CHECK(text_component.style.depth_write == 1U);
}

VR_TEST_CASE(EcsTextSystem_runtime_route_and_sort_key_layout, "unit;core;ecs;text") {
    using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;

    vr::ecs::Text<vr::ecs::Dim2> text_component{};
    TextSystem2D::Initialize(text_component);
    TextSystem2D::ClearDirtyFlags(text_component, 0xFFFFFFFFU);

    TextSystem2D::SetRuntimeRoute(text_component,
                                  123U,
                                  456U,
                                  7U,
                                  63U);
    TextSystem2D::SetLayer(text_component, -21);
    TextSystem2D::SetRenderPassHint(text_component, vr::ecs::TextRenderPassHint::transparent);

    const std::uint64_t sort_key = TextSystem2D::SortKey(text_component);
    VR_CHECK(TextSystem2D::ExtractPassBucket(sort_key) == static_cast<std::uint32_t>(vr::ecs::TextRenderPassHint::transparent));
    VR_CHECK(TextSystem2D::ExtractMaterialBucket(sort_key) == 456U);
    VR_CHECK(TextSystem2D::ExtractFontBucket(sort_key) == 123U);
    VR_CHECK(TextSystem2D::ExtractAtlasBucket(sort_key) == 7U);
    VR_CHECK(TextSystem2D::ExtractBatchBucket(sort_key) == 63U);

    const std::uint32_t expected_minor = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(-21) -
        static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()));
    VR_CHECK(TextSystem2D::ExtractMinorBucket(sort_key) == expected_minor);
    VR_CHECK(TextSystem2D::HasDirtyFlags(text_component, vr::ecs::runtime_dirty_flag));
}

VR_TEST_CASE(EcsTextSystem_rejects_overflow_and_preserves_content, "unit;core;ecs;text") {
    using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;

    vr::ecs::Text<vr::ecs::Dim2> text_component{};
    TextSystem2D::Initialize(text_component);

    VR_REQUIRE(TextSystem2D::SetText(text_component, "baseline"));
    VR_REQUIRE(TextSystem2D::Revision(text_component) == 1U);

    constexpr std::uint32_t overflow_size = vr::ecs::TextBufferInlineUtf8::inline_capacity_bytes + 1U;
    std::string overflow_text{};
    overflow_text.resize(overflow_size, 'x');

    VR_CHECK(!TextSystem2D::SetText(text_component, overflow_text));
    VR_CHECK(!TextSystem2D::AppendText(text_component, overflow_text));
    VR_CHECK(TextSystem2D::GetText(text_component) == std::string_view("baseline"));
    VR_CHECK(TextSystem2D::Revision(text_component) == 1U);
}

VR_TEST_CASE(EcsTextBatchSystem_dim2_build_sort_and_group, "unit;core;ecs;text;batch") {
    using Text2D = vr::ecs::Text<vr::ecs::Dim2>;
    using TextSystem2D = vr::ecs::TextSystem<vr::ecs::Dim2>;
    using BatchSystem2D = vr::ecs::TextBatchSystem<vr::ecs::Dim2>;

    std::array<Text2D, 6U> components{};
    for (auto& component : components) {
        TextSystem2D::Initialize(component);
    }

    VR_REQUIRE(TextSystem2D::SetText(components[0U], "A"));
    TextSystem2D::SetRuntimeRoute(components[0U], 1U, 5U, 1U, 1U);

    VR_REQUIRE(TextSystem2D::SetText(components[1U], "B"));
    TextSystem2D::SetRuntimeRoute(components[1U], 1U, 4U, 1U, 1U);

    // 2: keep empty

    VR_REQUIRE(TextSystem2D::SetText(components[3U], "Hidden"));
    TextSystem2D::SetVisible(components[3U], false);

    VR_REQUIRE(TextSystem2D::SetText(components[4U], "D"));
    TextSystem2D::SetRuntimeRoute(components[4U], 2U, 4U, 1U, 0U);
    TextSystem2D::SetLayer(components[4U], -1);

    VR_REQUIRE(TextSystem2D::SetText(components[5U], "E"));
    TextSystem2D::SetRuntimeRoute(components[5U], 2U, 4U, 1U, 0U);
    TextSystem2D::SetLayer(components[5U], 2);

    vr::ecs::TextBatchScratch<vr::ecs::Dim2> scratch{};
    const auto stats = BatchSystem2D::BuildAndSort(components.data(),
                                                    static_cast<std::uint32_t>(components.size()),
                                                    scratch,
                                                    true);

    VR_CHECK(stats.total_count == static_cast<std::uint32_t>(components.size()));
    VR_CHECK(stats.scanned_count == static_cast<std::uint32_t>(components.size()));
    VR_CHECK(stats.visible_count == 4U);
    VR_CHECK(stats.hidden_count == 1U);
    VR_CHECK(stats.empty_count == 1U);
    VR_CHECK(stats.out_of_range_candidate_count == 0U);
    VR_CHECK(stats.used_candidate_indices == 0U);

    VR_CHECK(BatchSystem2D::OrderedIndexCount(scratch) == 4U);
    const std::uint32_t* indices = BatchSystem2D::OrderedIndices(scratch);
    VR_REQUIRE(indices != nullptr);
    VR_CHECK(indices[0U] == 1U);
    VR_CHECK(indices[1U] == 4U);
    VR_CHECK(indices[2U] == 5U);
    VR_CHECK(indices[3U] == 0U);

    const vr::ecs::TextBatchItem* items = BatchSystem2D::SortedItems(scratch);
    VR_REQUIRE(items != nullptr);
    for (std::uint32_t i = 1U; i < stats.visible_count; ++i) {
        VR_CHECK(items[i - 1U].sort_key <= items[i].sort_key);
    }

    std::array<std::uint32_t, 4U> binding_group_counts{};
    std::uint32_t binding_group_total = 0U;
    std::uint32_t binding_group_used = 0U;
    BatchSystem2D::ForEachBindingGroup(scratch,
                                       [&](std::uint32_t begin_,
                                           std::uint32_t count_,
                                           std::uint64_t binding_key_) {
                                           (void)begin_;
                                           (void)binding_key_;
                                           if (binding_group_used < binding_group_counts.size()) {
                                               binding_group_counts[binding_group_used] = count_;
                                           }
                                           ++binding_group_used;
                                           binding_group_total += count_;
                                       });

    VR_CHECK(binding_group_used == 3U);
    VR_CHECK(binding_group_counts[0U] == 1U);
    VR_CHECK(binding_group_counts[1U] == 2U);
    VR_CHECK(binding_group_counts[2U] == 1U);
    VR_CHECK(binding_group_total == stats.visible_count);
}

VR_TEST_CASE(EcsTextBatchSystem_dim3_binding_key_ignores_depth_and_batch, "unit;core;ecs;text;batch") {
    using Text3D = vr::ecs::Text<vr::ecs::Dim3>;
    using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;
    using BatchSystem3D = vr::ecs::TextBatchSystem<vr::ecs::Dim3>;

    std::array<Text3D, 3U> components{};
    for (auto& component : components) {
        TextSystem3D::Initialize(component);
        VR_REQUIRE(TextSystem3D::SetText(component, "Marker"));
        TextSystem3D::SetRuntimeRoute(component, 9U, 33U, 2U, 0U);
    }

    TextSystem3D::SetDepthBin(components[0U], 2U);
    TextSystem3D::SetBatchTag(components[0U], 9U);

    TextSystem3D::SetDepthBin(components[1U], 0U);
    TextSystem3D::SetBatchTag(components[1U], 1U);

    TextSystem3D::SetDepthBin(components[2U], 1U);
    TextSystem3D::SetBatchTag(components[2U], 3U);

    vr::ecs::TextBatchScratch<vr::ecs::Dim3> scratch{};
    const auto stats = BatchSystem3D::BuildAndSort(components.data(),
                                                    static_cast<std::uint32_t>(components.size()),
                                                    scratch,
                                                    true);
    VR_CHECK(stats.visible_count == 3U);

    const std::uint32_t* indices = BatchSystem3D::OrderedIndices(scratch);
    VR_REQUIRE(indices != nullptr);
    VR_CHECK(indices[0U] == 0U);
    VR_CHECK(indices[1U] == 2U);
    VR_CHECK(indices[2U] == 1U);

    std::uint32_t binding_group_count = 0U;
    std::uint32_t grouped_items = 0U;
    BatchSystem3D::ForEachBindingGroup(scratch,
                                       [&](std::uint32_t begin_,
                                           std::uint32_t count_,
                                           std::uint64_t binding_key_) {
                                           (void)begin_;
                                           (void)binding_key_;
                                           ++binding_group_count;
                                           grouped_items += count_;
                                       });

    VR_CHECK(binding_group_count == 1U);
    VR_CHECK(grouped_items == 3U);
}

VR_TEST_CASE(EcsTextBatchSystem_dim3_candidate_indices_filter_and_oob, "unit;core;ecs;text;batch") {
    using Text3D = vr::ecs::Text<vr::ecs::Dim3>;
    using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;
    using BatchSystem3D = vr::ecs::TextBatchSystem<vr::ecs::Dim3>;

    std::array<Text3D, 5U> components{};
    for (auto& component : components) {
        TextSystem3D::Initialize(component);
        TextSystem3D::SetRuntimeRoute(component, 2U, 5U, 1U, 0U);
        VR_REQUIRE(TextSystem3D::SetText(component, "Text"));
    }
    TextSystem3D::SetVisible(components[1U], false);
    TextSystem3D::ClearText(components[3U]);

    const std::array<std::uint32_t, 6U> candidates{
        4U, 1U, 99U, 3U, 0U, 2U
    };

    vr::ecs::TextBatchScratch<vr::ecs::Dim3> scratch{};
    const auto stats = BatchSystem3D::BuildAndSortFromCandidates(components.data(),
                                                                  static_cast<std::uint32_t>(components.size()),
                                                                  candidates.data(),
                                                                  static_cast<std::uint32_t>(candidates.size()),
                                                                  scratch,
                                                                  true);
    VR_CHECK(stats.total_count == static_cast<std::uint32_t>(components.size()));
    VR_CHECK(stats.scanned_count == static_cast<std::uint32_t>(candidates.size()));
    VR_CHECK(stats.visible_count == 3U);
    VR_CHECK(stats.hidden_count == 1U);
    VR_CHECK(stats.empty_count == 1U);
    VR_CHECK(stats.out_of_range_candidate_count == 1U);
    VR_CHECK(stats.used_candidate_indices == 1U);
    VR_CHECK(BatchSystem3D::OrderedIndexCount(scratch) == 3U);
}

VR_TEST_CASE(EcsTextComponent_dimension_meta_matches_expectation, "unit;core;ecs;text") {
    VR_CHECK(vr::ecs::scene_dimension_v<vr::ecs::Dim2> == vr::ecs::SceneDimension::dim2);
    VR_CHECK(vr::ecs::scene_dimension_v<vr::ecs::Dim3> == vr::ecs::SceneDimension::dim3);
}

} // namespace

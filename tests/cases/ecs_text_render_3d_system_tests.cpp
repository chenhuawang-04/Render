#include "support/test_framework.hpp"
#include "vr/ecs/system/camera_system.hpp"
#include "vr/ecs/system/text_render_3d_system.hpp"
#include "vr/ecs/system/text_system.hpp"
#include "vr/ecs/system/transform_system.hpp"
#include "vr/text/freetype_host.hpp"
#include "vr/text/glyph_atlas_host.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>

namespace {

[[nodiscard]] bool NearlyEqual(float lhs_, float rhs_, float epsilon_ = 1e-4F) {
    return std::abs(lhs_ - rhs_) <= epsilon_;
}

[[nodiscard]] std::string FindTestFontPath() {
    namespace fs = std::filesystem;

    constexpr std::array<const char*, 6U> candidate_paths{
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/calibri.ttf",
        "C:/Windows/Fonts/msyh.ttc"
    };

    for (const char* path : candidate_paths) {
        const fs::path candidate(path);
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return candidate.string();
        }
    }
    return {};
}

[[nodiscard]] const vr::ecs::Text3DGpuInstance* FindFirstInstanceByComponent(
    const vr::ecs::TextRender3DScratch& scratch_,
    std::uint32_t component_index_) {
    for (const auto& instance : scratch_.instances) {
        if (instance.component_index == component_index_) {
            return &instance;
        }
    }
    return nullptr;
}

VR_TEST_CASE(EcsTextRender3DSystem_build_instances_billboard_and_basis,
             "unit;core;ecs;text;runtime") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for TextRender3DSystem test.");
    }

    vr::text::FreeTypeHost freetype_host{};
    freetype_host.Initialize();

    vr::text::FontFaceCreateInfo face_create_info{};
    face_create_info.file_path = font_path;
    face_create_info.pixel_height = 24U;
    const vr::text::FontFaceId base_face = freetype_host.RegisterFace(face_create_info);
    VR_REQUIRE(base_face.IsValid());

    vr::text::GlyphAtlasHost atlas_host{};
    atlas_host.Initialize(freetype_host);
    atlas_host.MapFont(21U, base_face);

    using Text3D = vr::ecs::Text<vr::ecs::Dim3>;
    using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;
    std::array<Text3D, 2U> text_components{};

    for (auto& component : text_components) {
        TextSystem3D::Initialize(component);
        TextSystem3D::SetRuntimeRoute(component, 21U, 2U, 0U, 0U);
        VR_REQUIRE(TextSystem3D::SetText(component, "3D"));
        TextSystem3D::SetWorldSize(component, 0.75F);
    }

    // component 0: follow object orientation
    TextSystem3D::SetBillboard(text_components[0U], false);

    // component 1: billboard facing camera, keep object scale
    TextSystem3D::SetBillboard(text_components[1U], true);

    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    std::array<Transform3D, 2U> text_transforms{};
    for (auto& transform : text_transforms) {
        TransformSystem3D::Initialize(transform);
    }

    TransformSystem3D::SetLocalRotationEulerXyz(text_transforms[0U],
                                                 0.0F,
                                                 0.0F,
                                                 1.57079632679F);
    TransformSystem3D::SetLocalScale(text_transforms[0U],
                                     vr::ecs::Float3{.x = 1.0F, .y = 2.0F, .z = 1.0F});

    TransformSystem3D::SetLocalPosition(text_transforms[1U],
                                        vr::ecs::Float3{.x = 2.0F, .y = 0.0F, .z = 0.0F});
    TransformSystem3D::SetLocalScale(text_transforms[1U],
                                     vr::ecs::Float3{.x = 2.0F, .y = 3.0F, .z = 1.0F});

    TransformSystem3D::UpdateHierarchy(text_transforms.data(),
                                       static_cast<std::uint32_t>(text_transforms.size()));

    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
    Camera3D camera{};
    CameraSystem3D::Initialize(camera);

    Transform3D camera_transform{};
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform,
                                        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 5.0F});
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);

    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    vr::ecs::TextRender3DScratch scratch{};
    vr::ecs::TextRuntimeBuildConfig build_config{};
    build_config.dim3_pixels_per_world_unit = 96.0F;

    const auto stats = vr::ecs::TextRender3DSystem::Build(text_components.data(),
                                                           text_transforms.data(),
                                                           static_cast<std::uint32_t>(text_components.size()),
                                                           camera,
                                                           camera_transform,
                                                           atlas_host,
                                                           freetype_host,
                                                           scratch,
                                                           build_config);
    VR_REQUIRE(stats.runtime.built_component_count == 2U);
    VR_REQUIRE(stats.emitted_instance_count > 0U);
    VR_REQUIRE(!scratch.instances.empty());
    VR_REQUIRE(!scratch.draw_batches.empty());

    const vr::ecs::Text3DGpuInstance* comp0_instance = FindFirstInstanceByComponent(scratch, 0U);
    const vr::ecs::Text3DGpuInstance* comp1_instance = FindFirstInstanceByComponent(scratch, 1U);
    VR_REQUIRE(comp0_instance != nullptr);
    VR_REQUIRE(comp1_instance != nullptr);

    // non-billboard component follows its own transform basis
    VR_CHECK(NearlyEqual(comp0_instance->basis_right_world.x, 0.0F, 1e-3F));
    VR_CHECK(NearlyEqual(comp0_instance->basis_right_world.y, 1.0F, 1e-3F));

    // billboard component faces camera, but keeps transform scale in basis length
    VR_CHECK(NearlyEqual(comp1_instance->basis_right_world.x, 2.0F, 1e-3F));
    VR_CHECK(NearlyEqual(comp1_instance->basis_right_world.y, 0.0F, 1e-3F));
    VR_CHECK(NearlyEqual(comp1_instance->basis_up_world.x, 0.0F, 1e-3F));
    VR_CHECK(NearlyEqual(comp1_instance->basis_up_world.y, 3.0F, 1e-3F));

    VR_CHECK(stats.billboard_instance_count > 0U);
}

VR_TEST_CASE(EcsTextRender3DSystem_build_frame_data_uses_camera_runtime,
             "unit;core;ecs;text;runtime") {
    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;

    Transform3D camera_transform{};
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform,
                                        vr::ecs::Float3{.x = 1.0F, .y = 2.0F, .z = 3.0F});
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);

    Camera3D camera{};
    CameraSystem3D::Initialize(camera);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    const vr::ecs::Text3DFrameData frame_data =
        vr::ecs::TextRender3DSystem::BuildFrameData(camera, camera_transform);

    VR_CHECK(NearlyEqual(frame_data.camera_position.x, 1.0F));
    VR_CHECK(NearlyEqual(frame_data.camera_position.y, 2.0F));
    VR_CHECK(NearlyEqual(frame_data.camera_position.z, 3.0F));
    VR_CHECK(NearlyEqual(frame_data.camera_right.x, 1.0F));
    VR_CHECK(NearlyEqual(frame_data.camera_right.y, 0.0F));
    VR_CHECK(NearlyEqual(frame_data.camera_up.x, 0.0F));
    VR_CHECK(NearlyEqual(frame_data.camera_up.y, 1.0F));
}

VR_TEST_CASE(EcsTextRender3DSystem_depth_batch_split_follows_component_depth_flags,
             "unit;core;ecs;text;runtime") {
    const std::string font_path = FindTestFontPath();
    if (font_path.empty()) {
        VR_SKIP("No usable system font found for TextRender3DSystem depth batch test.");
    }

    vr::text::FreeTypeHost freetype_host{};
    freetype_host.Initialize();

    vr::text::FontFaceCreateInfo face_create_info{};
    face_create_info.file_path = font_path;
    face_create_info.pixel_height = 24U;
    const vr::text::FontFaceId base_face = freetype_host.RegisterFace(face_create_info);
    VR_REQUIRE(base_face.IsValid());

    vr::text::GlyphAtlasHost atlas_host{};
    atlas_host.Initialize(freetype_host);
    atlas_host.MapFont(31U, base_face);

    using Text3D = vr::ecs::Text<vr::ecs::Dim3>;
    using TextSystem3D = vr::ecs::TextSystem<vr::ecs::Dim3>;
    std::array<Text3D, 2U> text_components{};

    for (auto& component : text_components) {
        TextSystem3D::Initialize(component);
        TextSystem3D::SetRuntimeRoute(component, 31U, 7U, 0U, 0U);
        TextSystem3D::SetText(component, "Depth");
        TextSystem3D::SetWorldSize(component, 0.5F);
        TextSystem3D::SetBillboard(component, true);
    }

    TextSystem3D::SetDepthTest(text_components[0U], false);
    TextSystem3D::SetDepthWrite(text_components[0U], false);

    TextSystem3D::SetDepthTest(text_components[1U], true);
    TextSystem3D::SetDepthWrite(text_components[1U], true);

    using Transform3D = vr::ecs::Transform<vr::ecs::Dim3>;
    using TransformSystem3D = vr::ecs::TransformSystem<vr::ecs::Dim3>;
    std::array<Transform3D, 2U> text_transforms{};
    for (auto& transform : text_transforms) {
        TransformSystem3D::Initialize(transform);
    }
    TransformSystem3D::SetLocalPosition(text_transforms[0U],
                                        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 0.2F});
    TransformSystem3D::SetLocalPosition(text_transforms[1U],
                                        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = -0.2F});
    TransformSystem3D::UpdateHierarchy(text_transforms.data(),
                                       static_cast<std::uint32_t>(text_transforms.size()));

    using Camera3D = vr::ecs::Camera<vr::ecs::Dim3>;
    using CameraSystem3D = vr::ecs::CameraSystem<vr::ecs::Dim3>;
    Camera3D camera{};
    CameraSystem3D::Initialize(camera);

    Transform3D camera_transform{};
    TransformSystem3D::Initialize(camera_transform);
    TransformSystem3D::SetLocalPosition(camera_transform,
                                        vr::ecs::Float3{.x = 0.0F, .y = 0.0F, .z = 3.0F});
    TransformSystem3D::UpdateHierarchy(&camera_transform, 1U);
    CameraSystem3D::MarkViewDirty(camera);
    CameraSystem3D::Update(camera, camera_transform);

    vr::ecs::TextRender3DScratch scratch{};
    const auto stats = vr::ecs::TextRender3DSystem::Build(text_components.data(),
                                                           text_transforms.data(),
                                                           static_cast<std::uint32_t>(text_components.size()),
                                                           camera,
                                                           camera_transform,
                                                           atlas_host,
                                                           freetype_host,
                                                           scratch);

    VR_REQUIRE(stats.runtime.built_component_count == 2U);
    VR_REQUIRE(stats.emitted_instance_count > 0U);
    VR_REQUIRE(stats.emitted_batch_count > 0U);
    VR_CHECK(stats.depth_test_batch_count > 0U);
    VR_CHECK(stats.depth_write_batch_count > 0U);

    bool has_depth_disabled_batch = false;
    bool has_depth_enabled_batch = false;
    bool has_depth_write_batch = false;
    for (const auto& batch : scratch.draw_batches) {
        if ((batch.depth_flags & 0x1U) == 0U) {
            has_depth_disabled_batch = true;
        } else {
            has_depth_enabled_batch = true;
        }
        if ((batch.depth_flags & 0x2U) != 0U) {
            has_depth_write_batch = true;
        }
    }

    VR_CHECK(has_depth_disabled_batch);
    VR_CHECK(has_depth_enabled_batch);
    VR_CHECK(has_depth_write_batch);
}

} // namespace

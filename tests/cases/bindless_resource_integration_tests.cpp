#include "support/test_framework.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/geometry/geometry_image_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/render_target_format_utils.hpp"
#include "vr/render/render_target_host.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/shadow/shadow_atlas_host.hpp"
#include "vr/surface/surface_image_host.hpp"
#include "vr/text/freetype_host.hpp"
#include "vr/text/glyph_atlas_host.hpp"
#include "vr/text/glyph_upload_host.hpp"
#include "vr/vulkan_context.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::string ToLower(std::string_view value_) {
    std::string lowered{};
    lowered.reserve(value_.size());
    for (const unsigned char ch : value_) {
        lowered.push_back(static_cast<char>(std::tolower(ch)));
    }
    return lowered;
}

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view text_,
                                           std::string_view needle_) {
    const std::string lowered_text = ToLower(text_);
    const std::string lowered_needle = ToLower(needle_);
    return lowered_text.find(lowered_needle) != std::string::npos;
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

[[nodiscard]] bool IsEnvironmentSkipError(std::string_view message_) {
    constexpr std::array<std::string_view, 12U> patterns{
        "vkcreateinstance",
        "vkenumeratephysicaldevices",
        "no vulkan physical devices found",
        "no suitable vulkan physical device found",
        "vkcreatedevice",
        "vkgetphysicaldevicefeatures2",
        "descriptor indexing",
        "runtime descriptor array",
        "synchronization2",
        "driver",
        "feature query unavailable",
        "loader"
    };

    for (const auto pattern : patterns) {
        if (ContainsCaseInsensitive(message_, pattern)) {
            return true;
        }
    }
    return false;
}

struct HeadlessBindlessFixture final {
    vr::VulkanContext context{};
    vr::resource::GpuMemoryHost gpu_memory{};
    vr::render::UploadHost upload{};
    vr::render::DescriptorHost descriptor{};
    vr::resource::SamplerHost sampler{};
    vr::asset::TextureHost texture{};
    vr::surface::SurfaceImageHost surface_image{};
    vr::geometry::GeometryImageHost geometry_image{};
    vr::shadow::ShadowAtlasHost shadow_atlas{};
    vr::render::RenderTargetHost render_target{};
    bool initialized = false;

    void Initialize() {
        vr::VulkanInstanceCreateInfo instance_info{};
        instance_info.enable_validation = false;

        vr::VulkanDeviceCreateInfo device_info{};
        device_info.required_vulkan13_features.synchronization2 = VK_TRUE;
        vr::EnableRecommendedBindlessOptionalFeatures(device_info);

        context.Initialize(instance_info, device_info, VK_NULL_HANDLE);
        gpu_memory.Initialize(context, {});

        vr::render::UploadHostCreateInfo upload_info{};
        upload_info.frames_in_flight = 1U;
        upload.Initialize(context, gpu_memory, upload_info);

        vr::render::DescriptorHostCreateInfo descriptor_info{};
        descriptor_info.frames_in_flight = 1U;
        descriptor_info.preallocate_first_pool_per_frame = false;
        descriptor_info.enable_validation = false;
        descriptor.Initialize(context, descriptor_info);

        sampler.Initialize(context, {});

        texture.Initialize(context, gpu_memory, {});
        surface_image.Initialize(context, gpu_memory, {});
        geometry_image.Initialize(context, gpu_memory, {});
        shadow_atlas.Initialize(context, gpu_memory, {});
        render_target.Initialize(context, gpu_memory, {});
        initialized = true;
    }

    void Shutdown() noexcept {
        if (!initialized) {
            return;
        }

        if (render_target.IsInitialized()) {
            render_target.Shutdown(context);
        }
        if (shadow_atlas.IsInitialized()) {
            shadow_atlas.Shutdown(context);
        }
        if (geometry_image.IsInitialized()) {
            geometry_image.Shutdown(context);
        }
        if (surface_image.IsInitialized()) {
            surface_image.Shutdown(context);
        }
        if (texture.IsInitialized()) {
            texture.Shutdown(context);
        }
        if (descriptor.IsInitialized()) {
            descriptor.Shutdown(context);
        }
        if (sampler.IsInitialized()) {
            sampler.Shutdown(context);
        }
        if (upload.IsInitialized()) {
            upload.Shutdown(context);
        }
        if (gpu_memory.IsInitialized()) {
            gpu_memory.Shutdown();
        }
        if (context.IsInitialized()) {
            context.Shutdown();
        }
        initialized = false;
    }

    ~HeadlessBindlessFixture() {
        Shutdown();
    }
};

void UploadTexture2D(HeadlessBindlessFixture& fixture_,
                     vr::asset::TextureId texture_id_,
                     std::uint32_t width_,
                     std::uint32_t height_,
                     const std::uint8_t* pixels_,
                     std::uint32_t byte_count_,
                     bool force_recreate_ = false) {
    vr::asset::TextureCreateInfo create_info{};
    create_info.texture_id = texture_id_;
    create_info.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    create_info.extent = VkExtent3D{width_, height_, 1U};
    create_info.mip_levels = 1U;
    create_info.array_layers = 1U;
    create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    create_info.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    create_info.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    create_info.force_recreate = force_recreate_;

    vr::asset::TextureSubresourceUploadInfo subresource{};
    subresource.pixels = pixels_;
    subresource.size_bytes = byte_count_;
    subresource.image_extent = VkExtent3D{width_, height_, 1U};

    vr::asset::TextureUploadInfo upload_info{};
    upload_info.create = create_info;
    upload_info.subresources = &subresource;
    upload_info.subresource_count = 1U;

    const std::uint64_t last_submitted = fixture_.upload.LastSubmittedValue();
    const std::uint64_t completed = fixture_.upload.CompletedSubmitValue();
    fixture_.upload.BeginFrame(fixture_.context, 0U);
    fixture_.texture.UploadTexture(fixture_.context,
                                   fixture_.upload,
                                   0U,
                                   last_submitted,
                                   completed,
                                   upload_info);
    const auto end_result =
        fixture_.upload.EndFrameAndSubmit(fixture_.context, 0U);
    if (end_result.submitted) {
        fixture_.upload.WaitFrame(fixture_.context, 0U);
    }
    fixture_.texture.BeginFrame(fixture_.context,
                                fixture_.upload.CompletedSubmitValue());
}

void UploadSurfaceImage2D(HeadlessBindlessFixture& fixture_,
                          std::uint32_t image_id_,
                          std::uint32_t width_,
                          std::uint32_t height_,
                          const std::uint8_t* pixels_,
                          bool force_recreate_ = false) {
    vr::surface::SurfaceImageUploadInfo upload_info{};
    upload_info.image_id = image_id_;
    upload_info.pixels = pixels_;
    upload_info.width = width_;
    upload_info.height = height_;
    upload_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    upload_info.bytes_per_pixel = 4U;
    upload_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    upload_info.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    upload_info.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    upload_info.force_recreate = force_recreate_;

    const std::uint64_t last_submitted = fixture_.upload.LastSubmittedValue();
    const std::uint64_t completed = fixture_.upload.CompletedSubmitValue();
    fixture_.upload.BeginFrame(fixture_.context, 0U);
    fixture_.surface_image.UploadImage(fixture_.context,
                                       fixture_.upload,
                                       0U,
                                       last_submitted,
                                       completed,
                                       upload_info);
    const auto end_result =
        fixture_.upload.EndFrameAndSubmit(fixture_.context, 0U);
    if (end_result.submitted) {
        fixture_.upload.WaitFrame(fixture_.context, 0U);
    }
    fixture_.surface_image.BeginFrame(fixture_.context,
                                      fixture_.upload.CompletedSubmitValue());
}

void UploadGeometryImage2D(HeadlessBindlessFixture& fixture_,
                           std::uint32_t image_id_,
                           std::uint32_t width_,
                           std::uint32_t height_,
                           const std::uint8_t* pixels_,
                           bool force_recreate_ = false) {
    vr::geometry::GeometryImageUploadInfo upload_info{};
    upload_info.image_id = image_id_;
    upload_info.pixels = pixels_;
    upload_info.width = width_;
    upload_info.height = height_;
    upload_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    upload_info.bytes_per_pixel = 4U;
    upload_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    upload_info.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    upload_info.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    upload_info.force_recreate = force_recreate_;

    const std::uint64_t last_submitted = fixture_.upload.LastSubmittedValue();
    const std::uint64_t completed = fixture_.upload.CompletedSubmitValue();
    fixture_.upload.BeginFrame(fixture_.context, 0U);
    fixture_.geometry_image.UploadImage(fixture_.context,
                                        fixture_.upload,
                                        0U,
                                        last_submitted,
                                        completed,
                                        upload_info);
    const auto end_result =
        fixture_.upload.EndFrameAndSubmit(fixture_.context, 0U);
    if (end_result.submitted) {
        fixture_.upload.WaitFrame(fixture_.context, 0U);
    }
    fixture_.geometry_image.BeginFrame(fixture_.context,
                                       fixture_.upload.CompletedSubmitValue());
}

void UploadGlyphAtlasPages(HeadlessBindlessFixture& fixture_,
                           vr::text::GlyphUploadHost& glyph_upload_host_,
                           vr::text::GlyphAtlasHost& glyph_atlas_host_) {
    const std::uint64_t last_submitted = fixture_.upload.LastSubmittedValue();
    const std::uint64_t completed = fixture_.upload.CompletedSubmitValue();
    fixture_.upload.BeginFrame(fixture_.context, 0U);
    glyph_upload_host_.UploadDirtyPages(fixture_.context,
                                        fixture_.upload,
                                        0U,
                                        glyph_atlas_host_,
                                        last_submitted,
                                        completed);
    const auto end_result =
        fixture_.upload.EndFrameAndSubmit(fixture_.context, 0U);
    if (end_result.submitted) {
        fixture_.upload.WaitFrame(fixture_.context, 0U);
    }
}

[[nodiscard]] vr::render::RenderTargetDesc MakePersistentSampledColorTargetDesc(
    std::uint32_t width_,
    std::uint32_t height_,
    const char* debug_name_) {
    vr::render::RenderTargetDesc desc{};
    desc.debug_name = debug_name_;
    desc.dimension = vr::render::RenderTargetDimension::image_2d;
    desc.lifetime = vr::render::RenderTargetLifetime::persistent;
    desc.scale_mode = vr::render::RenderTargetScaleMode::absolute;
    desc.width = width_;
    desc.height = height_;
    desc.depth = 1U;
    desc.format = VK_FORMAT_R8G8B8A8_UNORM;
    desc.samples = VK_SAMPLE_COUNT_1_BIT;
    desc.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    desc.color_encoding = vr::render::RenderTargetColorEncoding::linear;
    return desc;
}

[[nodiscard]] vr::render::BindlessTableId CreateSampledImageTableFromPlaceholder(
    HeadlessBindlessFixture& fixture_,
    vr::asset::TextureId placeholder_texture_id_) {
    const auto* placeholder_record =
        fixture_.texture.FindTexture(placeholder_texture_id_);
    if (placeholder_record == nullptr ||
        placeholder_record->resource.default_view == VK_NULL_HANDLE) {
        throw std::runtime_error("Bindless placeholder texture was not uploaded correctly");
    }

    vr::render::BindlessTableDesc table_desc{};
    table_desc.debug_name = "TestBindlessSampledImageTable";
    table_desc.descriptor_type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    table_desc.requested_capacity = 64U;
    table_desc.stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT;
    table_desc.enable_partially_bound = true;
    table_desc.enable_variable_descriptor_count = true;
    table_desc.enable_update_after_bind = false;
    table_desc.placeholder_image_info.imageView = placeholder_record->resource.default_view;
    table_desc.placeholder_image_info.imageLayout = placeholder_record->shader_read_layout;
    return fixture_.descriptor.CreateBindlessTable(fixture_.context, table_desc);
}

VR_TEST_CASE(BindlessIntegration_descriptor_host_persistent_table_recycles_slots,
             "integration;gpu;bindless;descriptor") {
    HeadlessBindlessFixture fixture{};

    try {
        fixture.Initialize();
        if (!fixture.context.DescriptorIndexingCapsInfo().enabled) {
            VR_SKIP("Bindless descriptor indexing is not enabled on the active Vulkan device.");
        }

        constexpr std::array<std::uint8_t, 4U> placeholder_rgba{
            255U, 255U, 255U, 255U
        };
        UploadTexture2D(fixture,
                        vr::asset::TextureId{9001U},
                        1U,
                        1U,
                        placeholder_rgba.data(),
                        static_cast<std::uint32_t>(placeholder_rgba.size()));

        const auto table =
            CreateSampledImageTableFromPlaceholder(fixture, vr::asset::TextureId{9001U});
        if (fixture.descriptor.GetBindlessCapacity(table) < 2U) {
            VR_SKIP("Bindless sampled-image table capacity is too small for slot recycling test.");
        }
        const VkDescriptorSet set_before = fixture.descriptor.GetBindlessDescriptorSet(table);
        fixture.descriptor.FlushBindlessWrites(fixture.context,
                                               fixture.upload.CompletedSubmitValue());
        fixture.descriptor.BeginFrame(fixture.context, 0U);
        const VkDescriptorSet set_after = fixture.descriptor.GetBindlessDescriptorSet(table);

        VR_REQUIRE(set_before != VK_NULL_HANDLE);
        VR_CHECK(set_before == set_after);
        VR_CHECK(fixture.descriptor.GetBindlessCapacity(table) > 0U);

        const auto table_stats = fixture.descriptor.GetBindlessTableStats(table);
        VR_CHECK(table_stats.has_placeholder);
        VR_CHECK(table_stats.live_count == 1U);

        const vr::render::BindlessSlot slot_a =
            fixture.descriptor.AllocateBindlessSlot(table);
        VR_CHECK(slot_a.IsValid());
        VR_CHECK(slot_a.index != 0U);
        VR_CHECK(fixture.descriptor.IsBindlessSlotAlive(table, slot_a));

        fixture.descriptor.QueueBindlessPlaceholderWrite(table, slot_a);
        fixture.descriptor.FreeBindlessSlotDeferred(table, slot_a, 0U);
        fixture.descriptor.FlushBindlessWrites(fixture.context, 0U);

        VR_CHECK(!fixture.descriptor.IsBindlessSlotAlive(table, slot_a));

        const vr::render::BindlessSlot slot_b =
            fixture.descriptor.AllocateBindlessSlot(table);
        VR_CHECK(slot_b.IsValid());
        VR_CHECK(slot_b.index == slot_a.index);
        VR_CHECK(slot_b.generation != slot_a.generation);
    } catch (const std::exception& exception_) {
        fixture.Shutdown();
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(BindlessIntegration_texture_host_slot_is_stable_across_recreate_and_not_reused_early,
             "integration;gpu;bindless;texture") {
    HeadlessBindlessFixture fixture{};

    try {
        fixture.Initialize();
        if (!fixture.context.DescriptorIndexingCapsInfo().enabled) {
            VR_SKIP("Bindless descriptor indexing is not enabled on the active Vulkan device.");
        }

        constexpr std::array<std::uint8_t, 4U> placeholder_rgba{
            255U, 255U, 255U, 255U
        };
        constexpr std::array<std::uint8_t, 16U> texture_rgba_a{
            255U, 0U,   0U,   255U,
            0U,   255U, 0U,   255U,
            0U,   0U,   255U, 255U,
            255U, 255U, 0U,   255U
        };
        constexpr std::array<std::uint8_t, 64U> texture_rgba_b{
            255U, 64U,  64U,  255U, 64U,  255U, 64U,  255U,
            64U,  64U,  255U, 255U, 255U, 255U, 64U,  255U,
            255U, 64U,  255U, 255U, 64U,  255U, 255U, 255U,
            255U, 255U, 255U, 255U, 32U,  32U,  32U,  255U,
            255U, 0U,   128U, 255U, 0U,   255U, 128U, 255U,
            128U, 0U,   255U, 255U, 255U, 128U, 0U,   255U,
            128U, 255U, 0U,   255U, 0U,   128U, 255U, 255U,
            200U, 200U, 200U, 255U, 16U,  16U,  16U,  255U
        };

        UploadTexture2D(fixture,
                        vr::asset::TextureId{9002U},
                        1U,
                        1U,
                        placeholder_rgba.data(),
                        static_cast<std::uint32_t>(placeholder_rgba.size()));

        const auto table =
            CreateSampledImageTableFromPlaceholder(fixture, vr::asset::TextureId{9002U});
        if (fixture.descriptor.GetBindlessCapacity(table) < 3U) {
            VR_SKIP("Bindless sampled-image table capacity is too small for texture host test.");
        }
        fixture.texture.ConfigureBindless({
            .descriptor_host = &fixture.descriptor,
            .image_table = table,
        });

        UploadTexture2D(fixture,
                        vr::asset::TextureId{1U},
                        2U,
                        2U,
                        texture_rgba_a.data(),
                        static_cast<std::uint32_t>(texture_rgba_a.size()));
        fixture.descriptor.FlushBindlessWrites(fixture.context,
                                               fixture.upload.CompletedSubmitValue());
        const vr::render::BindlessSlot slot_a =
            fixture.texture.ResolveBindlessImageSlot(vr::asset::TextureId{1U});
        VR_REQUIRE(slot_a.IsValid());

        UploadTexture2D(fixture,
                        vr::asset::TextureId{1U},
                        4U,
                        4U,
                        texture_rgba_b.data(),
                        static_cast<std::uint32_t>(texture_rgba_b.size()),
                        true);
        fixture.descriptor.FlushBindlessWrites(fixture.context,
                                               fixture.upload.CompletedSubmitValue());
        const vr::render::BindlessSlot slot_b =
            fixture.texture.ResolveBindlessImageSlot(vr::asset::TextureId{1U});
        VR_REQUIRE(slot_b.IsValid());
        VR_CHECK(slot_b.index == slot_a.index);
        VR_CHECK(slot_b.generation == slot_a.generation);

        VR_REQUIRE(fixture.texture.RemoveTexture(fixture.context,
                                                vr::asset::TextureId{1U},
                                                5U,
                                                4U));
        fixture.descriptor.FlushBindlessWrites(fixture.context, 4U);
        VR_CHECK(!fixture.descriptor.IsBindlessSlotAlive(table, slot_a));

        UploadTexture2D(fixture,
                        vr::asset::TextureId{2U},
                        2U,
                        2U,
                        texture_rgba_a.data(),
                        static_cast<std::uint32_t>(texture_rgba_a.size()));
        fixture.descriptor.FlushBindlessWrites(fixture.context, 4U);
        const vr::render::BindlessSlot slot_c =
            fixture.texture.ResolveBindlessImageSlot(vr::asset::TextureId{2U});
        VR_REQUIRE(slot_c.IsValid());
        VR_CHECK(slot_c.index != slot_a.index);

        fixture.descriptor.FlushBindlessWrites(fixture.context, 5U);
    } catch (const std::exception& exception_) {
        fixture.Shutdown();
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(BindlessIntegration_resource_system_wires_texture_host_and_global_tables,
             "integration;gpu;bindless;runtime") {
    HeadlessBindlessFixture fixture{};
    vr::render::BindlessResourceSystem bindless_resources{};

    try {
        fixture.Initialize();
        if (!fixture.context.DescriptorIndexingCapsInfo().enabled) {
            VR_SKIP("Bindless descriptor indexing is not enabled on the active Vulkan device.");
        }

        bindless_resources.Initialize(fixture.context,
                                      fixture.gpu_memory,
                                      fixture.descriptor,
                                      fixture.sampler,
                                      {});
        bindless_resources.ConfigureTextureHost(fixture.texture);

        const auto& bindless_config = fixture.texture.BindlessConfig();
        VR_REQUIRE(bindless_config.Enabled());
        VR_CHECK(bindless_config.image_table.value ==
                 bindless_resources.SampledImageTable().value);
        VR_CHECK(bindless_config.default_sampler_slot.index ==
                 bindless_resources.DefaultSamplerSlot().index);
        VR_CHECK(bindless_config.default_sampler_slot.generation ==
                 bindless_resources.DefaultSamplerSlot().generation);

        constexpr std::array<std::uint8_t, 16U> texture_rgba{
            255U, 128U,  64U, 255U,
            64U,  255U, 128U, 255U,
            32U,  64U,  255U, 255U,
            255U, 255U, 255U, 255U
        };

        UploadTexture2D(fixture,
                        vr::asset::TextureId{42U},
                        2U,
                        2U,
                        texture_rgba.data(),
                        static_cast<std::uint32_t>(texture_rgba.size()));

        fixture.descriptor.FlushBindlessWrites(fixture.context,
                                               fixture.upload.CompletedSubmitValue());

        VR_REQUIRE(bindless_resources.SampledImageSet() != VK_NULL_HANDLE);
        VR_REQUIRE(bindless_resources.SamplerSet() != VK_NULL_HANDLE);
        VR_REQUIRE(bindless_resources.SampledImageLayout() != VK_NULL_HANDLE);
        VR_REQUIRE(bindless_resources.SamplerLayout() != VK_NULL_HANDLE);
        VR_REQUIRE(bindless_resources.PlaceholderImage().default_view != VK_NULL_HANDLE);
        VR_REQUIRE(bindless_resources.DefaultSampler() != VK_NULL_HANDLE);
        VR_REQUIRE(bindless_resources.PlaceholderImageSlot().IsValid());
        VR_REQUIRE(bindless_resources.DefaultSamplerSlot().IsValid());
        VR_CHECK(bindless_resources.PlaceholderImageSlot().index == 0U);
        VR_CHECK(bindless_resources.DefaultSamplerSlot().index == 0U);

        const vr::render::BindlessSlot texture_slot =
            bindless_resources.ResolveTextureImageSlot(fixture.texture,
                                                       vr::asset::TextureId{42U});
        const vr::render::BindlessSlot sampler_slot =
            bindless_resources.ResolveTextureSamplerSlot(fixture.texture,
                                                         vr::asset::TextureId{42U});
        VR_REQUIRE(texture_slot.IsValid());
        VR_REQUIRE(sampler_slot.IsValid());
        VR_CHECK(texture_slot.index != bindless_resources.PlaceholderImageSlot().index);
        VR_CHECK(sampler_slot.index == bindless_resources.DefaultSamplerSlot().index);
        VR_CHECK(sampler_slot.generation == bindless_resources.DefaultSamplerSlot().generation);

        const auto& stats = bindless_resources.Stats();
        VR_CHECK(stats.initialized);
        VR_CHECK(stats.sampled_image_table.value == bindless_resources.SampledImageTable().value);
        VR_CHECK(stats.sampler_table.value == bindless_resources.SamplerTable().value);
        VR_CHECK(stats.sampled_image.live_count >= 2U);
        VR_CHECK(stats.sampler.live_count >= 1U);

        fixture.texture.ConfigureBindless({});
        bindless_resources.Shutdown(fixture.context);
    } catch (const std::exception& exception_) {
        if (fixture.texture.IsInitialized()) {
            fixture.texture.ConfigureBindless({});
        }
        if (bindless_resources.IsInitialized()) {
            bindless_resources.Shutdown(fixture.context);
        }
        fixture.Shutdown();
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(BindlessIntegration_surface_image_host_slot_is_stable_across_recreate_and_not_reused_early,
             "integration;gpu;bindless;surface") {
    HeadlessBindlessFixture fixture{};

    try {
        fixture.Initialize();
        if (!fixture.context.DescriptorIndexingCapsInfo().enabled) {
            VR_SKIP("Bindless descriptor indexing is not enabled on the active Vulkan device.");
        }

        constexpr std::array<std::uint8_t, 4U> placeholder_rgba{
            255U, 255U, 255U, 255U
        };
        constexpr std::array<std::uint8_t, 16U> image_rgba_a{
            255U, 32U,  32U,  255U,
            32U,  255U, 32U,  255U,
            32U,  32U,  255U, 255U,
            255U, 255U, 255U, 255U
        };
        constexpr std::array<std::uint8_t, 64U> image_rgba_b{
            0U,   0U,   0U,   255U, 255U, 0U,   0U,   255U,
            0U,   255U, 0U,   255U, 0U,   0U,   255U, 255U,
            255U, 255U, 0U,   255U, 255U, 0U,   255U, 255U,
            0U,   255U, 255U, 255U, 200U, 200U, 200U, 255U,
            16U,  16U,  16U,  255U, 128U, 0U,   64U,  255U,
            64U,  128U, 0U,   255U, 0U,   64U,  128U, 255U,
            255U, 128U, 64U,  255U, 64U,  255U, 128U, 255U,
            128U, 64U,  255U, 255U, 255U, 64U,  128U, 255U
        };

        UploadTexture2D(fixture,
                        vr::asset::TextureId{9003U},
                        1U,
                        1U,
                        placeholder_rgba.data(),
                        static_cast<std::uint32_t>(placeholder_rgba.size()));

        const auto table =
            CreateSampledImageTableFromPlaceholder(fixture, vr::asset::TextureId{9003U});
        if (fixture.descriptor.GetBindlessCapacity(table) < 3U) {
            VR_SKIP("Bindless sampled-image table capacity is too small for surface image host test.");
        }
        fixture.surface_image.ConfigureBindless({
            .descriptor_host = &fixture.descriptor,
            .image_table = table,
        });

        UploadSurfaceImage2D(fixture,
                             11U,
                             2U,
                             2U,
                             image_rgba_a.data());
        fixture.descriptor.FlushBindlessWrites(fixture.context,
                                               fixture.upload.CompletedSubmitValue());
        const vr::render::BindlessSlot slot_a =
            fixture.surface_image.ResolveBindlessImageSlot(11U);
        VR_REQUIRE(slot_a.IsValid());

        UploadSurfaceImage2D(fixture,
                             11U,
                             4U,
                             4U,
                             image_rgba_b.data(),
                             true);
        fixture.descriptor.FlushBindlessWrites(fixture.context,
                                               fixture.upload.CompletedSubmitValue());
        const vr::render::BindlessSlot slot_b =
            fixture.surface_image.ResolveBindlessImageSlot(11U);
        VR_REQUIRE(slot_b.IsValid());
        VR_CHECK(slot_b.index == slot_a.index);
        VR_CHECK(slot_b.generation == slot_a.generation);

        VR_REQUIRE(fixture.surface_image.RemoveImage(fixture.context,
                                                    11U,
                                                    7U,
                                                    6U));
        fixture.descriptor.FlushBindlessWrites(fixture.context, 6U);
        VR_CHECK(!fixture.descriptor.IsBindlessSlotAlive(table, slot_a));

        UploadSurfaceImage2D(fixture,
                             12U,
                             2U,
                             2U,
                             image_rgba_a.data());
        fixture.descriptor.FlushBindlessWrites(fixture.context, 6U);
        const vr::render::BindlessSlot slot_c =
            fixture.surface_image.ResolveBindlessImageSlot(12U);
        VR_REQUIRE(slot_c.IsValid());
        VR_CHECK(slot_c.index != slot_a.index);

        fixture.descriptor.FlushBindlessWrites(fixture.context, 7U);
    } catch (const std::exception& exception_) {
        fixture.Shutdown();
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(BindlessIntegration_geometry_image_host_slot_is_stable_across_recreate_and_not_reused_early,
             "integration;gpu;bindless;geometry") {
    HeadlessBindlessFixture fixture{};

    try {
        fixture.Initialize();
        if (!fixture.context.DescriptorIndexingCapsInfo().enabled) {
            VR_SKIP("Bindless descriptor indexing is not enabled on the active Vulkan device.");
        }

        constexpr std::array<std::uint8_t, 4U> placeholder_rgba{
            255U, 255U, 255U, 255U
        };
        constexpr std::array<std::uint8_t, 16U> image_rgba_a{
            255U, 64U,  64U,  255U,
            64U,  255U, 64U,  255U,
            64U,  64U,  255U, 255U,
            255U, 255U, 255U, 255U
        };
        constexpr std::array<std::uint8_t, 64U> image_rgba_b{
            16U,  16U,  16U,  255U, 255U, 0U,   64U,  255U,
            0U,   255U, 64U,  255U, 0U,   64U,  255U, 255U,
            255U, 255U, 64U,  255U, 255U, 64U,  255U, 255U,
            64U,  255U, 255U, 255U, 200U, 200U, 200U, 255U,
            255U, 0U,   128U, 255U, 0U,   255U, 128U, 255U,
            128U, 0U,   255U, 255U, 255U, 128U, 0U,   255U,
            128U, 255U, 0U,   255U, 0U,   128U, 255U, 255U,
            32U,  32U,  32U,  255U, 255U, 255U, 255U, 255U
        };

        UploadTexture2D(fixture,
                        vr::asset::TextureId{9004U},
                        1U,
                        1U,
                        placeholder_rgba.data(),
                        static_cast<std::uint32_t>(placeholder_rgba.size()));

        const auto table =
            CreateSampledImageTableFromPlaceholder(fixture, vr::asset::TextureId{9004U});
        if (fixture.descriptor.GetBindlessCapacity(table) < 3U) {
            VR_SKIP("Bindless sampled-image table capacity is too small for geometry image host test.");
        }
        fixture.geometry_image.ConfigureBindless({
            .descriptor_host = &fixture.descriptor,
            .image_table = table,
        });

        UploadGeometryImage2D(fixture,
                              21U,
                              2U,
                              2U,
                              image_rgba_a.data());
        fixture.descriptor.FlushBindlessWrites(fixture.context,
                                               fixture.upload.CompletedSubmitValue());
        const vr::render::BindlessSlot slot_a =
            fixture.geometry_image.ResolveBindlessImageSlot(21U);
        VR_REQUIRE(slot_a.IsValid());

        UploadGeometryImage2D(fixture,
                              21U,
                              4U,
                              4U,
                              image_rgba_b.data(),
                              true);
        fixture.descriptor.FlushBindlessWrites(fixture.context,
                                               fixture.upload.CompletedSubmitValue());
        const vr::render::BindlessSlot slot_b =
            fixture.geometry_image.ResolveBindlessImageSlot(21U);
        VR_REQUIRE(slot_b.IsValid());
        VR_CHECK(slot_b.index == slot_a.index);
        VR_CHECK(slot_b.generation == slot_a.generation);

        VR_REQUIRE(fixture.geometry_image.RemoveImage(fixture.context,
                                                      21U,
                                                      9U,
                                                      8U));
        fixture.descriptor.FlushBindlessWrites(fixture.context, 8U);
        VR_CHECK(!fixture.descriptor.IsBindlessSlotAlive(table, slot_a));

        UploadGeometryImage2D(fixture,
                              22U,
                              2U,
                              2U,
                              image_rgba_a.data());
        fixture.descriptor.FlushBindlessWrites(fixture.context, 8U);
        const vr::render::BindlessSlot slot_c =
            fixture.geometry_image.ResolveBindlessImageSlot(22U);
        VR_REQUIRE(slot_c.IsValid());
        VR_CHECK(slot_c.index != slot_a.index);

        fixture.descriptor.FlushBindlessWrites(fixture.context, 9U);
    } catch (const std::exception& exception_) {
        fixture.Shutdown();
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(BindlessIntegration_render_target_host_slot_tracks_recreate_and_deferred_free,
             "integration;gpu;bindless;render_target") {
    HeadlessBindlessFixture fixture{};

    try {
        fixture.Initialize();
        if (!fixture.context.DescriptorIndexingCapsInfo().enabled) {
            VR_SKIP("Bindless descriptor indexing is not enabled on the active Vulkan device.");
        }
        if (!vr::render::IsColorAttachmentSampledFormatSupported(fixture.context,
                                                                 VK_FORMAT_R8G8B8A8_UNORM)) {
            VR_SKIP("R8G8B8A8_UNORM sampled color attachments are not supported on the active Vulkan device.");
        }

        constexpr std::array<std::uint8_t, 4U> placeholder_rgba{
            255U, 255U, 255U, 255U
        };
        UploadTexture2D(fixture,
                        vr::asset::TextureId{9005U},
                        1U,
                        1U,
                        placeholder_rgba.data(),
                        static_cast<std::uint32_t>(placeholder_rgba.size()));

        const auto table =
            CreateSampledImageTableFromPlaceholder(fixture, vr::asset::TextureId{9005U});
        if (fixture.descriptor.GetBindlessCapacity(table) < 3U) {
            VR_SKIP("Bindless sampled-image table capacity is too small for render target host test.");
        }
        fixture.render_target.ConfigureBindless({
            .descriptor_host = &fixture.descriptor,
            .image_table = table,
        });

        const auto desc_a =
            MakePersistentSampledColorTargetDesc(32U, 32U, "BindlessIntegrationRenderTargetA");
        const vr::render::RenderTargetHandle handle_a =
            fixture.render_target.CreatePersistentTarget(fixture.context, desc_a);
        const vr::render::BindlessSlot slot_a =
            fixture.render_target.EnsureBindlessImageSlot(handle_a);
        fixture.descriptor.FlushBindlessWrites(fixture.context, 0U);
        VR_REQUIRE(slot_a.IsValid());

        auto desc_b = desc_a;
        desc_b.width = 64U;
        desc_b.height = 64U;
        const auto ensure_result =
            fixture.render_target.EnsurePersistentTarget(fixture.context,
                                                         handle_a,
                                                         desc_b,
                                                         {},
                                                         11U,
                                                         10U);
        VR_REQUIRE(vr::render::IsValidRenderTargetHandle(ensure_result.handle));
        VR_CHECK(ensure_result.recreated || ensure_result.revision_changed);
        const vr::render::BindlessSlot slot_b =
            fixture.render_target.EnsureBindlessImageSlot(ensure_result.handle);
        fixture.descriptor.FlushBindlessWrites(fixture.context, 10U);
        VR_REQUIRE(slot_b.IsValid());
        VR_CHECK(slot_b.index == slot_a.index);
        VR_CHECK(slot_b.generation == slot_a.generation);

        VR_REQUIRE(fixture.render_target.DestroyTarget(fixture.context,
                                                       ensure_result.handle,
                                                       13U,
                                                       12U));
        fixture.descriptor.FlushBindlessWrites(fixture.context, 12U);
        VR_CHECK(!fixture.descriptor.IsBindlessSlotAlive(table, slot_a));

        const vr::render::RenderTargetHandle handle_b =
            fixture.render_target.CreatePersistentTarget(fixture.context, desc_a);
        const vr::render::BindlessSlot slot_c =
            fixture.render_target.EnsureBindlessImageSlot(handle_b);
        fixture.descriptor.FlushBindlessWrites(fixture.context, 12U);
        VR_REQUIRE(slot_c.IsValid());
        VR_CHECK(slot_c.index != slot_a.index);

        fixture.descriptor.FlushBindlessWrites(fixture.context, 13U);

        const vr::render::RenderTargetHandle handle_c =
            fixture.render_target.CreatePersistentTarget(fixture.context, desc_a);
        const vr::render::BindlessSlot slot_d =
            fixture.render_target.EnsureBindlessImageSlot(handle_c);
        fixture.descriptor.FlushBindlessWrites(fixture.context, 13U);
        VR_REQUIRE(slot_d.IsValid());
        VR_CHECK(slot_d.index == slot_a.index);
        VR_CHECK(slot_d.generation != slot_a.generation);
    } catch (const std::exception& exception_) {
        fixture.Shutdown();
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(BindlessIntegration_shadow_atlas_host_slot_is_stable_across_resize,
             "integration;gpu;bindless;shadow") {
    HeadlessBindlessFixture fixture{};
    vr::render::BindlessResourceSystem bindless_resources{};

    try {
        fixture.Initialize();
        if (!fixture.context.DescriptorIndexingCapsInfo().enabled) {
            VR_SKIP("Bindless descriptor indexing is not enabled on the active Vulkan device.");
        }

        bindless_resources.Initialize(fixture.context,
                                      fixture.gpu_memory,
                                      fixture.descriptor,
                                      fixture.sampler,
                                      {});
        bindless_resources.ConfigureShadowAtlasHost(fixture.shadow_atlas);

        vr::shadow::ShadowAtlasRequest request_a{
            .namespace_id = 101U,
            .width = 64U,
            .height = 64U,
            .layer_count = 2U,
        };
        fixture.shadow_atlas.EnsureAtlases(fixture.context, 0U, 0U, &request_a, 1U);
        fixture.shadow_atlas.BeginFrame(fixture.context, 0U);
        fixture.descriptor.FlushBindlessWrites(fixture.context, 0U);

        const vr::render::BindlessSlot slot_a =
            fixture.shadow_atlas.ResolveBindlessAtlasSlot(101U);
        VR_REQUIRE(slot_a.IsValid());

        vr::shadow::ShadowAtlasRequest request_b{
            .namespace_id = 101U,
            .width = 128U,
            .height = 128U,
            .layer_count = 4U,
        };
        fixture.shadow_atlas.EnsureAtlases(fixture.context, 3U, 2U, &request_b, 1U);
        fixture.shadow_atlas.BeginFrame(fixture.context, 2U);
        fixture.descriptor.FlushBindlessWrites(fixture.context, 2U);

        const vr::render::BindlessSlot slot_b =
            fixture.shadow_atlas.ResolveBindlessAtlasSlot(101U);
        VR_REQUIRE(slot_b.IsValid());
        VR_CHECK(slot_b.index == slot_a.index);
        VR_CHECK(slot_b.generation == slot_a.generation);

        vr::shadow::ShadowAtlasRequest requests_c[2U]{
            request_b,
            vr::shadow::ShadowAtlasRequest{
                .namespace_id = 102U,
                .width = 32U,
                .height = 32U,
                .layer_count = 1U,
            }
        };
        fixture.shadow_atlas.EnsureAtlases(fixture.context, 4U, 3U, requests_c, 2U);
        fixture.shadow_atlas.BeginFrame(fixture.context, 3U);
        fixture.descriptor.FlushBindlessWrites(fixture.context, 3U);

        const vr::render::BindlessSlot slot_c =
            fixture.shadow_atlas.ResolveBindlessAtlasSlot(102U);
        VR_REQUIRE(slot_c.IsValid());
        VR_CHECK(slot_c.index != slot_a.index);

        bindless_resources.Shutdown(fixture.context);
    } catch (const std::exception& exception_) {
        if (bindless_resources.IsInitialized()) {
            bindless_resources.Shutdown(fixture.context);
        }
        fixture.Shutdown();
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(BindlessIntegration_registered_sampler_slots_are_cached_and_distinct,
             "integration;gpu;bindless;sampler") {
    HeadlessBindlessFixture fixture{};
    vr::render::BindlessResourceSystem bindless_resources{};

    try {
        fixture.Initialize();
        if (!fixture.context.DescriptorIndexingCapsInfo().enabled) {
            VR_SKIP("Bindless descriptor indexing is not enabled on the active Vulkan device.");
        }

        bindless_resources.Initialize(fixture.context,
                                      fixture.gpu_memory,
                                      fixture.descriptor,
                                      fixture.sampler,
                                      {});

        const vr::render::BindlessSlot invalid_slot =
            bindless_resources.ResolveRegisteredSamplerSlot({});
        VR_CHECK(invalid_slot.index == bindless_resources.DefaultSamplerSlot().index);
        VR_CHECK(invalid_slot.generation == bindless_resources.DefaultSamplerSlot().generation);

        vr::resource::SamplerDesc linear_repeat{};
        linear_repeat.mag_filter = VK_FILTER_LINEAR;
        linear_repeat.min_filter = VK_FILTER_LINEAR;
        linear_repeat.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        linear_repeat.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        linear_repeat.address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        vr::resource::SamplerDesc nearest_clamp{};
        nearest_clamp.mag_filter = VK_FILTER_NEAREST;
        nearest_clamp.min_filter = VK_FILTER_NEAREST;
        nearest_clamp.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        nearest_clamp.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        nearest_clamp.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        const vr::resource::SamplerId sampler_a =
            fixture.sampler.RegisterSampler(fixture.context, linear_repeat);
        const vr::resource::SamplerId sampler_b =
            fixture.sampler.RegisterSampler(fixture.context, nearest_clamp);
        VR_REQUIRE(sampler_a.IsValid());
        VR_REQUIRE(sampler_b.IsValid());

        const vr::render::BindlessSlot slot_a_first =
            bindless_resources.ResolveRegisteredSamplerSlot(sampler_a);
        fixture.descriptor.FlushBindlessWrites(fixture.context, 0U);
        const vr::render::BindlessSlot slot_a_second =
            bindless_resources.ResolveRegisteredSamplerSlot(sampler_a);
        const vr::render::BindlessSlot slot_b =
            bindless_resources.ResolveRegisteredSamplerSlot(sampler_b);
        fixture.descriptor.FlushBindlessWrites(fixture.context, 0U);

        VR_REQUIRE(slot_a_first.IsValid());
        VR_REQUIRE(slot_a_second.IsValid());
        VR_REQUIRE(slot_b.IsValid());
        VR_CHECK(slot_a_first.index == slot_a_second.index);
        VR_CHECK(slot_a_first.generation == slot_a_second.generation);
        VR_CHECK(slot_b.index != slot_a_first.index);

        const auto& stats = bindless_resources.Stats();
        VR_CHECK(stats.sampler.live_count >= 3U);

        bindless_resources.Shutdown(fixture.context);
    } catch (const std::exception& exception_) {
        if (bindless_resources.IsInitialized()) {
            bindless_resources.Shutdown(fixture.context);
        }
        fixture.Shutdown();
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

VR_TEST_CASE(BindlessIntegration_glyph_upload_host_slot_survives_page_recreate_and_growth,
             "integration;gpu;bindless;glyph") {
    HeadlessBindlessFixture fixture{};
    vr::render::BindlessResourceSystem bindless_resources{};
    vr::text::FreeTypeHost freetype_host{};
    vr::text::GlyphAtlasHost atlas_host_a{};
    vr::text::GlyphAtlasHost atlas_host_b{};
    vr::text::GlyphAtlasHost atlas_host_c{};
    vr::text::GlyphUploadHost glyph_upload_host{};

    try {
        fixture.Initialize();
        if (!fixture.context.DescriptorIndexingCapsInfo().enabled) {
            VR_SKIP("Bindless descriptor indexing is not enabled on the active Vulkan device.");
        }

        const std::string font_path = FindTestFontPath();
        if (font_path.empty()) {
            VR_SKIP("No usable system font found for GlyphUploadHost bindless test.");
        }

        bindless_resources.Initialize(fixture.context,
                                      fixture.gpu_memory,
                                      fixture.descriptor,
                                      fixture.sampler,
                                      {});

        freetype_host.Initialize();
        vr::text::FontFaceCreateInfo face_create_info{};
        face_create_info.file_path = font_path;
        face_create_info.pixel_height = 28U;
        const vr::text::FontFaceId face_id = freetype_host.RegisterFace(face_create_info);
        VR_REQUIRE(face_id.IsValid());

        auto map_glyphs = [&](vr::text::GlyphAtlasHost& atlas_host_,
                              std::uint32_t page_width_,
                              std::uint32_t page_height_,
                              std::uint32_t codepoint_end_) {
            vr::text::GlyphAtlasCreateInfo atlas_create_info{};
            atlas_create_info.page_width = page_width_;
            atlas_create_info.page_height = page_height_;
            atlas_create_info.max_page_count = 32U;
            atlas_create_info.glyph_padding = 1U;
            atlas_host_.Initialize(freetype_host, atlas_create_info);
            atlas_host_.MapFont(7U, face_id);

            vr::text::GlyphAtlasResolveRequest request{};
            request.font_id = 7U;
            for (std::uint32_t codepoint = 33U; codepoint < codepoint_end_; ++codepoint) {
                request.codepoint = codepoint;
                (void)atlas_host_.ResolveGlyph(request);
            }
        };

        glyph_upload_host.Initialize(fixture.context,
                                     fixture.gpu_memory,
                                     fixture.sampler,
                                     {});
        bindless_resources.ConfigureGlyphUploadHost(glyph_upload_host);

        map_glyphs(atlas_host_a, 64U, 64U, 80U);
        UploadGlyphAtlasPages(fixture, glyph_upload_host, atlas_host_a);
        fixture.descriptor.FlushBindlessWrites(fixture.context,
                                               fixture.upload.CompletedSubmitValue());
        const vr::render::BindlessSlot slot_a =
            glyph_upload_host.ResolveBindlessImageSlot(0U);
        VR_REQUIRE(slot_a.IsValid());

        map_glyphs(atlas_host_b, 96U, 96U, 80U);
        UploadGlyphAtlasPages(fixture, glyph_upload_host, atlas_host_b);
        fixture.descriptor.FlushBindlessWrites(fixture.context,
                                               fixture.upload.CompletedSubmitValue());
        const vr::render::BindlessSlot slot_b =
            glyph_upload_host.ResolveBindlessImageSlot(0U);
        VR_REQUIRE(slot_b.IsValid());
        VR_CHECK(slot_b.index == slot_a.index);
        VR_CHECK(slot_b.generation == slot_a.generation);

        map_glyphs(atlas_host_c, 64U, 64U, 128U);
        UploadGlyphAtlasPages(fixture, glyph_upload_host, atlas_host_c);
        fixture.descriptor.FlushBindlessWrites(fixture.context,
                                               fixture.upload.CompletedSubmitValue());
        VR_REQUIRE(glyph_upload_host.PageCount() >= 2U);
        const vr::render::BindlessSlot slot_c =
            glyph_upload_host.ResolveBindlessImageSlot(0U);
        const vr::render::BindlessSlot slot_page_1 =
            glyph_upload_host.ResolveBindlessImageSlot(1U);
        VR_REQUIRE(slot_c.IsValid());
        VR_REQUIRE(slot_page_1.IsValid());
        VR_CHECK(slot_c.index == slot_a.index);
        VR_CHECK(slot_c.generation == slot_a.generation);
        VR_CHECK(slot_page_1.index != slot_a.index);

        glyph_upload_host.Shutdown(fixture.context);
        atlas_host_c.Shutdown();
        atlas_host_b.Shutdown();
        atlas_host_a.Shutdown();
        freetype_host.Shutdown();
        bindless_resources.Shutdown(fixture.context);
    } catch (const std::exception& exception_) {
        if (glyph_upload_host.IsInitialized()) {
            glyph_upload_host.Shutdown(fixture.context);
        }
        atlas_host_c.Shutdown();
        atlas_host_b.Shutdown();
        atlas_host_a.Shutdown();
        if (freetype_host.IsInitialized()) {
            freetype_host.Shutdown();
        }
        if (bindless_resources.IsInitialized()) {
            bindless_resources.Shutdown(fixture.context);
        }
        fixture.Shutdown();
        if (IsEnvironmentSkipError(exception_.what())) {
            VR_SKIP(exception_.what());
        }
        throw;
    }
}

} // namespace

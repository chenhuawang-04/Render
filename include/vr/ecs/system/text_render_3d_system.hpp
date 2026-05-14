#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/text_runtime_system.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace vr::ecs {

template<typename T>
using TextRender3DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct Text3DGpuInstance final {
    float rect_x0;
    float rect_y0;
    float rect_x1;
    float rect_y1;

    float uv_u0;
    float uv_v0;
    float uv_u1;
    float uv_v1;

    Float3 origin_world;
    float reserved0;

    Float3 basis_right_world;
    float reserved1;

    Float3 basis_up_world;
    float reserved2;

    std::uint32_t color_rgba8;
    std::uint32_t outline_color_rgba8;
    std::uint32_t params;
    std::uint32_t atlas_page_id;

    std::uint32_t component_index;
    std::uint32_t user_data;
    std::uint32_t glyph_index;
    std::uint32_t reserved3;
};

struct Text3DFrameData final {
    Matrix4x4 view_projection;
    Float3 camera_position;
    float reserved0;

    Float3 camera_right;
    float reserved1;

    Float3 camera_up;
    float reserved2;
};

struct TextRender3DBuildStats final {
    TextRuntimeBuildStats runtime{};
    std::uint32_t emitted_instance_count = 0U;
    std::uint32_t emitted_batch_count = 0U;
    std::uint32_t billboard_instance_count = 0U;
    std::uint32_t depth_test_batch_count = 0U;
    std::uint32_t depth_write_batch_count = 0U;
};

struct Text3DDrawBatch final {
    std::uint64_t sort_key;
    std::uint32_t glyph_begin;
    std::uint32_t glyph_count;
    std::uint32_t atlas_page_id;
    std::uint32_t font_id;
    std::uint32_t visual_resource_id;
    std::uint32_t first_component_index;
    std::uint32_t depth_flags;
};

struct TextRender3DScratch final {
    TextRuntimeScratch<Dim3> runtime_scratch{};
    TextRender3DMcVector<Text3DGpuInstance> instances{};
    TextRender3DMcVector<Text3DDrawBatch> draw_batches{};
};

static_assert(PurePodComponent<Text3DGpuInstance>);
static_assert(PurePodComponent<Text3DFrameData>);
static_assert(PurePodComponent<Text3DDrawBatch>);
static_assert(alignof(Text3DGpuInstance) == 4U);
static_assert(sizeof(Text3DGpuInstance) == 112U);
static_assert(offsetof(Text3DGpuInstance, rect_x0) == 0U);
static_assert(offsetof(Text3DGpuInstance, uv_u0) == 16U);
static_assert(offsetof(Text3DGpuInstance, origin_world) == 32U);
static_assert(offsetof(Text3DGpuInstance, basis_right_world) == 48U);
static_assert(offsetof(Text3DGpuInstance, basis_up_world) == 64U);
static_assert(offsetof(Text3DGpuInstance, color_rgba8) == 80U);
static_assert(offsetof(Text3DGpuInstance, outline_color_rgba8) == 84U);
static_assert(offsetof(Text3DGpuInstance, params) == 88U);
static_assert(offsetof(Text3DGpuInstance, atlas_page_id) == 92U);

class TextRender3DSystem final {
public:
    using TextType = Text<Dim3>;
    using TransformType = Transform<Dim3>;
    using CameraType = Camera<Dim3>;

    static void Reserve(TextRender3DScratch& scratch_,
                        std::uint32_t component_count_,
                        std::uint32_t glyph_capacity_hint_ = 0U) {
        TextRuntimeSystem<Dim3>::Reserve(scratch_.runtime_scratch,
                                         component_count_,
                                         glyph_capacity_hint_);

        if (glyph_capacity_hint_ > 0U) {
            const auto target = static_cast<std::size_t>(glyph_capacity_hint_);
            if (scratch_.instances.capacity() < target) {
                scratch_.instances.reserve(target);
            }
            if (scratch_.draw_batches.capacity() < target) {
                scratch_.draw_batches.reserve(target);
            }
        }
    }

    [[nodiscard]] static Text3DFrameData BuildFrameData(const CameraType& camera_,
                                                        const TransformType& camera_transform_) noexcept {
        Text3DFrameData out{};
        out.view_projection = camera_.runtime.view_projection_matrix;
        out.camera_position = ExtractTranslation(camera_transform_.runtime.world_matrix);
        out.camera_right = NormalizeOrFallback(ExtractBasisX(camera_transform_.runtime.world_matrix),
                                               Float3{.x = 1.0F, .y = 0.0F, .z = 0.0F});
        out.camera_up = NormalizeOrFallback(ExtractBasisY(camera_transform_.runtime.world_matrix),
                                            Float3{.x = 0.0F, .y = 1.0F, .z = 0.0F});
        out.reserved0 = 0.0F;
        out.reserved1 = 0.0F;
        out.reserved2 = 0.0F;
        return out;
    }

    [[nodiscard]] static TextRender3DBuildStats Build(TextType* components_,
                                                       const TransformType* transforms_,
                                                       std::uint32_t component_count_,
                                                       const CameraType& camera_,
                                                       const TransformType& camera_transform_,
                                                       text::GlyphAtlasHost& atlas_host_,
                                                       text::FreeTypeHost& freetype_host_,
                                                       TextRender3DScratch& scratch_,
                                                       const TextRuntimeBuildConfig& build_config_ = {},
                                                       const TextRuntimeBuildHint& runtime_build_hint_ = {}) {
        TextRender3DBuildStats stats{};

        scratch_.instances.clear();
        scratch_.draw_batches.clear();

        if (components_ == nullptr || transforms_ == nullptr || component_count_ == 0U) {
            return stats;
        }

        stats.runtime = TextRuntimeSystem<Dim3>::Build(components_,
                                                       component_count_,
                                                       atlas_host_,
                                                       freetype_host_,
                                                       scratch_.runtime_scratch,
                                                       build_config_,
                                                       runtime_build_hint_);

        const TextRender3DBuildStats compose_stats = BuildFromRuntime(components_,
                                                                       transforms_,
                                                                       component_count_,
                                                                       camera_,
                                                                       camera_transform_,
                                                                       scratch_,
                                                                       stats.runtime);
        return compose_stats;
    }

    [[nodiscard]] static TextRender3DBuildStats BuildFromRuntime(TextType* components_,
                                                                  const TransformType* transforms_,
                                                                  std::uint32_t component_count_,
                                                                  const CameraType& camera_,
                                                                  const TransformType& camera_transform_,
                                                                  TextRender3DScratch& scratch_,
                                                                  const TextRuntimeBuildStats& runtime_stats_) {
        TextRender3DBuildStats stats{};
        stats.runtime = runtime_stats_;

        scratch_.instances.clear();
        scratch_.draw_batches.clear();

        if (components_ == nullptr || transforms_ == nullptr || component_count_ == 0U) {
            return stats;
        }
        if (scratch_.runtime_scratch.glyph_quads.empty()) {
            return stats;
        }

        const Text3DFrameData frame_data = BuildFrameData(camera_, camera_transform_);

        scratch_.instances.resize(scratch_.runtime_scratch.glyph_quads.size());

        for (std::uint32_t i = 0U;
             i < scratch_.runtime_scratch.glyph_quads.size();
             ++i) {
            const TextGlyphQuad& quad = scratch_.runtime_scratch.glyph_quads[i];
            if (quad.component_index >= component_count_) {
                continue;
            }

            const TextType& component = components_[quad.component_index];
            const TransformType& transform = transforms_[quad.component_index];

            const Matrix4x4& world = transform.runtime.world_matrix;
            const Float3 basis_x = ExtractBasisX(world);
            const Float3 basis_y = ExtractBasisY(world);

            const bool billboard = component.style.billboard != 0U;
            Float3 right_world = basis_x;
            Float3 up_world = basis_y;

            if (billboard) {
                const float scale_x = std::max(1e-6F, Length(basis_x));
                const float scale_y = std::max(1e-6F, Length(basis_y));

                right_world = Scale(frame_data.camera_right, scale_x);
                up_world = Scale(frame_data.camera_up, scale_y);
                ++stats.billboard_instance_count;
            }

            Text3DGpuInstance instance{};
            instance.rect_x0 = quad.x0;
            instance.rect_y0 = quad.y0;
            instance.rect_x1 = quad.x1;
            instance.rect_y1 = quad.y1;

            instance.uv_u0 = quad.u0;
            instance.uv_v0 = quad.v0;
            instance.uv_u1 = quad.u1;
            instance.uv_v1 = quad.v1;

            instance.origin_world = ExtractTranslation(world);
            instance.reserved0 = 0.0F;

            instance.basis_right_world = right_world;
            instance.reserved1 = 0.0F;

            instance.basis_up_world = up_world;
            instance.reserved2 = 0.0F;

            instance.color_rgba8 = PackRgba8(quad.color);
            instance.outline_color_rgba8 = PackRgba8(quad.outline_color);
            instance.params = PackParams(quad,
                                         component.style.depth_test != 0U,
                                         component.style.depth_write != 0U);
            instance.atlas_page_id = quad.atlas_page_id;

            instance.component_index = quad.component_index;
            instance.user_data = quad.user_data;
            instance.glyph_index = quad.glyph_index;
            instance.reserved3 = 0U;

            scratch_.instances[i] = instance;
        }

        Rebuild3DDrawBatches(scratch_, stats);

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

    [[nodiscard]] static std::uint32_t PackParams(const TextGlyphQuad& quad_,
                                                  bool depth_test_enabled_,
                                                  bool depth_write_enabled_) noexcept {
        std::uint32_t packed = 0U;
        packed |= (quad_.sdf_enabled != 0U) ? 0x1U : 0U;
        packed |= (quad_.outline_enabled != 0U) ? 0x2U : 0U;
        packed |= depth_test_enabled_ ? 0x4U : 0U;
        packed |= depth_write_enabled_ ? 0x8U : 0U;
        packed |= (static_cast<std::uint32_t>(quad_.outline_width_px) << 8U);
        return packed;
    }

    [[nodiscard]] static Float3 ExtractBasisX(const Matrix4x4& matrix_) noexcept {
        return Float3{
            .x = matrix_.m[0],
            .y = matrix_.m[1],
            .z = matrix_.m[2],
        };
    }

    [[nodiscard]] static Float3 ExtractBasisY(const Matrix4x4& matrix_) noexcept {
        return Float3{
            .x = matrix_.m[4],
            .y = matrix_.m[5],
            .z = matrix_.m[6],
        };
    }

    [[nodiscard]] static Float3 ExtractTranslation(const Matrix4x4& matrix_) noexcept {
        return Float3{
            .x = matrix_.m[12],
            .y = matrix_.m[13],
            .z = matrix_.m[14],
        };
    }

    [[nodiscard]] static float Length(const Float3& value_) noexcept {
        return std::sqrt(value_.x * value_.x +
                         value_.y * value_.y +
                         value_.z * value_.z);
    }

    [[nodiscard]] static Float3 Scale(const Float3& value_, float factor_) noexcept {
        return Float3{
            .x = value_.x * factor_,
            .y = value_.y * factor_,
            .z = value_.z * factor_,
        };
    }

    [[nodiscard]] static Float3 NormalizeOrFallback(const Float3& value_,
                                                    const Float3& fallback_) noexcept {
        const float length = Length(value_);
        if (length <= 1e-8F) {
            return fallback_;
        }

        const float inv = 1.0F / length;
        return Scale(value_, inv);
    }

    [[nodiscard]] static std::uint32_t DepthFlagsFromInstanceParams(std::uint32_t params_) noexcept {
        std::uint32_t flags = 0U;
        const bool depth_test = (params_ & 0x4U) != 0U;
        const bool depth_write = (params_ & 0x8U) != 0U;
        if (depth_test) {
            flags |= 0x1U;
            if (depth_write) {
                flags |= 0x2U;
            }
        }
        return flags;
    }

    static void AppendOrMergeDrawBatch3D(TextRender3DScratch& scratch_,
                                         const TextDrawBatch& source_batch_,
                                         std::uint32_t glyph_begin_,
                                         std::uint32_t glyph_count_,
                                         std::uint32_t depth_flags_) {
        if (glyph_count_ == 0U) {
            return;
        }

        if (!scratch_.draw_batches.empty()) {
            Text3DDrawBatch& last = scratch_.draw_batches.back();
            if (last.sort_key == source_batch_.sort_key &&
                last.atlas_page_id == source_batch_.atlas_page_id &&
                last.font_id == source_batch_.font_id &&
                last.visual_resource_id == source_batch_.visual_resource_id &&
                last.depth_flags == depth_flags_ &&
                last.glyph_begin + last.glyph_count == glyph_begin_) {
                last.glyph_count += glyph_count_;
                return;
            }
        }

        Text3DDrawBatch batch{};
        batch.sort_key = source_batch_.sort_key;
        batch.glyph_begin = glyph_begin_;
        batch.glyph_count = glyph_count_;
        batch.atlas_page_id = source_batch_.atlas_page_id;
        batch.font_id = source_batch_.font_id;
        batch.visual_resource_id = source_batch_.visual_resource_id;
        batch.first_component_index = source_batch_.first_component_index;
        batch.depth_flags = depth_flags_;
        scratch_.draw_batches.push_back(batch);
    }

    static void Rebuild3DDrawBatches(TextRender3DScratch& scratch_,
                                     TextRender3DBuildStats& stats_) {
        scratch_.draw_batches.clear();
        if (scratch_.runtime_scratch.draw_batches.empty() ||
            scratch_.instances.empty()) {
            return;
        }

        for (const TextDrawBatch& source_batch : scratch_.runtime_scratch.draw_batches) {
            if (source_batch.glyph_count == 0U) {
                continue;
            }
            const std::uint32_t begin = source_batch.glyph_begin;
            if (begin >= scratch_.instances.size()) {
                continue;
            }

            const std::uint32_t available =
                static_cast<std::uint32_t>(scratch_.instances.size()) - begin;
            const std::uint32_t count = std::min(source_batch.glyph_count, available);
            if (count == 0U) {
                continue;
            }

            std::uint32_t segment_begin = begin;
            std::uint32_t segment_flags =
                DepthFlagsFromInstanceParams(scratch_.instances[begin].params);

            for (std::uint32_t i = 1U; i < count; ++i) {
                const std::uint32_t glyph_index = begin + i;
                const std::uint32_t flags =
                    DepthFlagsFromInstanceParams(scratch_.instances[glyph_index].params);
                if (flags == segment_flags) {
                    continue;
                }

                const std::uint32_t segment_count = glyph_index - segment_begin;
                AppendOrMergeDrawBatch3D(scratch_,
                                         source_batch,
                                         segment_begin,
                                         segment_count,
                                         segment_flags);

                segment_begin = glyph_index;
                segment_flags = flags;
            }

            const std::uint32_t segment_count = begin + count - segment_begin;
            AppendOrMergeDrawBatch3D(scratch_,
                                     source_batch,
                                     segment_begin,
                                     segment_count,
                                     segment_flags);
        }

        for (const Text3DDrawBatch& batch : scratch_.draw_batches) {
            if ((batch.depth_flags & 0x1U) != 0U) {
                ++stats_.depth_test_batch_count;
            }
            if ((batch.depth_flags & 0x2U) != 0U) {
                ++stats_.depth_write_batch_count;
            }
        }
    }
};

} // namespace vr::ecs


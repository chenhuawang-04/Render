#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/light_prepare_stage.hpp"

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <limits>

namespace vr::render {

template<typename T>
using LightFrameCoordinatorMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

template<ecs::DimensionTag DimensionT>
struct LightFrameCoordinatorStats final {
    std::uint32_t prepare_call_count = 0U;
    std::uint32_t runtime_build_call_count = 0U;
    std::uint32_t culling_build_call_count = 0U;
    std::uint32_t same_frame_reuse_hit_count = 0U;
    std::uint32_t cross_frame_reuse_hit_count = 0U;
    std::uint64_t light_dirty_hint_input_count = 0U;
    std::uint64_t light_dirty_hint_unique_count = 0U;
    std::uint64_t light_dirty_hint_out_of_range_drop_count = 0U;
    std::uint64_t light_dirty_hint_duplicate_drop_count = 0U;
    std::uint64_t transform_dirty_hint_input_count = 0U;
    std::uint64_t transform_dirty_hint_unique_count = 0U;
    std::uint64_t transform_dirty_hint_out_of_range_drop_count = 0U;
    std::uint64_t transform_dirty_hint_duplicate_drop_count = 0U;
};

template<ecs::DimensionTag DimensionT>
class LightFrameCoordinator final {
public:
    using LightType = ecs::Light<DimensionT>;
    using TransformType = ecs::Transform<DimensionT>;
    using CameraType = ecs::Camera<DimensionT>;

    using PrepareStageType = LightPrepareStage<DimensionT>;
    using PrepareStageResultType = LightPrepareStageResult<DimensionT>;
    using RuntimeScratchType = ecs::LightRuntimeScratch<DimensionT>;
    using RuntimeBuildConfigType = ecs::LightRuntimeBuildConfig;
    using CullingScratchType = ecs::LightCullingScratch<DimensionT>;
    using CullingBuildConfigType = ecs::LightCullingBuildConfig<DimensionT>;
    using CullingSystemType = ecs::LightCullingSystem<DimensionT>;

    static constexpr std::uint32_t invalid_frame_index = (std::numeric_limits<std::uint32_t>::max)();

    LightFrameCoordinator() = default;
    ~LightFrameCoordinator() = default;

    LightFrameCoordinator(const LightFrameCoordinator&) = delete;
    LightFrameCoordinator& operator=(const LightFrameCoordinator&) = delete;
    LightFrameCoordinator(LightFrameCoordinator&&) = delete;
    LightFrameCoordinator& operator=(LightFrameCoordinator&&) = delete;

    void SetLightData(LightType* light_components_,
                      const TransformType* transforms_,
                      std::uint32_t light_count_) noexcept {
        if (light_components == light_components_ &&
            transforms == transforms_ &&
            light_count == light_count_) {
            return;
        }
        light_components = light_components_;
        transforms = transforms_;
        light_count = light_count_;
        ++source_revision;
        InvalidateCaches();
        ResetAccumulatedDirtyHints();
    }

    void SetCamera(const CameraType* camera_component_) noexcept {
        if (camera_component == camera_component_) {
            return;
        }
        camera_component = camera_component_;
        ++camera_revision;
        culling_cache_valid = false;
        frame_cache_valid = false;
    }

    void SetLightDirtyHint(const std::uint32_t* dirty_component_indices_,
                           std::uint32_t dirty_component_count_) noexcept {
        AppendDirtyHint(dirty_component_indices_,
                        dirty_component_count_,
                        accumulated_light_dirty_indices,
                        stats.light_dirty_hint_input_count);
        if (dirty_component_indices_ != nullptr && dirty_component_count_ > 0U) {
            ++light_dirty_revision;
            frame_cache_valid = false;
            runtime_cache_valid = false;
            culling_cache_valid = false;
        }
    }

    void SetTransformDirtyHint(const std::uint32_t* dirty_component_indices_,
                               std::uint32_t dirty_component_count_) noexcept {
        AppendDirtyHint(dirty_component_indices_,
                        dirty_component_count_,
                        accumulated_transform_dirty_indices,
                        stats.transform_dirty_hint_input_count);
        if (dirty_component_indices_ != nullptr && dirty_component_count_ > 0U) {
            ++transform_dirty_revision;
            frame_cache_valid = false;
            runtime_cache_valid = false;
            culling_cache_valid = false;
        }
    }

    void Reserve(std::uint32_t light_count_,
                 const CullingBuildConfigType& culling_config_ = CullingSystemType::DefaultBuildConfig()) {
        PrepareStageType::Reserve(runtime_scratch, culling_scratch, light_count_, culling_config_);
        const std::size_t reserve_count = static_cast<std::size_t>(light_count_);
        if (accumulated_light_dirty_indices.capacity() < reserve_count) {
            accumulated_light_dirty_indices.reserve(reserve_count);
        }
        if (accumulated_transform_dirty_indices.capacity() < reserve_count) {
            accumulated_transform_dirty_indices.reserve(reserve_count);
        }
        if (normalized_light_dirty_indices.capacity() < reserve_count) {
            normalized_light_dirty_indices.reserve(reserve_count);
        }
        if (normalized_transform_dirty_indices.capacity() < reserve_count) {
            normalized_transform_dirty_indices.reserve(reserve_count);
        }
        if (light_dirty_marker_stamps.size() < reserve_count) {
            light_dirty_marker_stamps.resize(reserve_count, 0U);
        }
        if (transform_dirty_marker_stamps.size() < reserve_count) {
            transform_dirty_marker_stamps.resize(reserve_count, 0U);
        }
    }

    void ResetAll() noexcept {
        light_components = nullptr;
        transforms = nullptr;
        camera_component = nullptr;
        light_count = 0U;

        runtime_scratch = {};
        culling_scratch = {};

        accumulated_light_dirty_indices.clear();
        accumulated_transform_dirty_indices.clear();
        normalized_light_dirty_indices.clear();
        normalized_transform_dirty_indices.clear();
        light_dirty_marker_stamps.clear();
        transform_dirty_marker_stamps.clear();
        light_dirty_marker_epoch = 1U;
        transform_dirty_marker_epoch = 1U;

        last_prepare_result = {};
        stats = {};
        source_revision = 0U;
        camera_revision = 0U;
        light_dirty_revision = 0U;
        transform_dirty_revision = 0U;
        InvalidateCaches();
    }

    [[nodiscard]] PrepareStageResultType PrepareFrame(
        std::uint32_t frame_index_,
        const RuntimeBuildConfigType& runtime_config_ = RuntimeBuildConfigType{},
        const CullingBuildConfigType& culling_config_ = CullingSystemType::DefaultBuildConfig()) {
        ++stats.prepare_call_count;
        if (light_components == nullptr || light_count == 0U) {
            last_prepare_result = {};
            InvalidateCaches();
            return last_prepare_result;
        }

        const bool can_reuse_same_frame =
            frame_cache_valid &&
            frame_cache_frame_index == frame_index_ &&
            frame_cache_source_revision == source_revision &&
            frame_cache_camera_revision == camera_revision &&
            frame_cache_light_dirty_revision == light_dirty_revision &&
            frame_cache_transform_dirty_revision == transform_dirty_revision &&
            frame_cache_culling_config_signature == ComposeCullingConfigSignature(culling_config_);
        if (can_reuse_same_frame) {
            ++stats.same_frame_reuse_hit_count;
            PrepareStageResultType reused_result = last_prepare_result;
            reused_result.runtime_build_invoked = false;
            reused_result.culling_build_invoked = false;
            return reused_result;
        }

        NormalizeDirtyHints();

        const bool runtime_dirty = !normalized_light_dirty_indices.empty() ||
                                   !normalized_transform_dirty_indices.empty();
        const bool need_runtime_build = !runtime_cache_valid ||
                                        runtime_dirty ||
                                        runtime_cache_source_revision != source_revision ||
                                        runtime_cache_light_count != light_count ||
                                        runtime_cache_light_components != light_components ||
                                        runtime_cache_transforms != transforms;

        PrepareStageResultType result{};
        result.has_light_data = true;
        if (need_runtime_build) {
            result.runtime_stats = PrepareStageType::BuildRuntimeOnly(
                light_components,
                transforms,
                light_count,
                runtime_scratch,
                normalized_light_dirty_indices.empty() ? nullptr : normalized_light_dirty_indices.data(),
                static_cast<std::uint32_t>(normalized_light_dirty_indices.size()),
                normalized_transform_dirty_indices.empty() ? nullptr : normalized_transform_dirty_indices.data(),
                static_cast<std::uint32_t>(normalized_transform_dirty_indices.size()),
                runtime_config_).runtime_stats;
            result.runtime_build_invoked = true;
            ++stats.runtime_build_call_count;

            runtime_cache_valid = true;
            runtime_cache_source_revision = source_revision;
            runtime_cache_light_dirty_revision = light_dirty_revision;
            runtime_cache_transform_dirty_revision = transform_dirty_revision;
            runtime_cache_light_count = light_count;
            runtime_cache_light_components = light_components;
            runtime_cache_transforms = transforms;
            runtime_cache_style_signature = result.runtime_stats.style_signature;
            runtime_cache_binding_signature = result.runtime_stats.binding_signature;
            runtime_cache_transform_signature = result.runtime_stats.transform_signature;
        } else {
            result.runtime_stats = last_prepare_result.runtime_stats;
            ++stats.cross_frame_reuse_hit_count;
        }

        const std::uint64_t culling_config_signature = ComposeCullingConfigSignature(culling_config_);
        const bool need_culling_build = !culling_cache_valid ||
                                        need_runtime_build ||
                                        culling_cache_camera_revision != camera_revision ||
                                        culling_cache_culling_config_signature != culling_config_signature ||
                                        culling_cache_style_signature != runtime_cache_style_signature ||
                                        culling_cache_binding_signature != runtime_cache_binding_signature ||
                                        culling_cache_transform_signature != runtime_cache_transform_signature;

        if (need_culling_build) {
            result.culling_stats = CullingSystemType::Build(light_components,
                                                            ecs::LightRuntimeSystem<DimensionT>::DerivedGeomData(runtime_scratch),
                                                            ecs::LightRuntimeSystem<DimensionT>::DerivedOpticalData(runtime_scratch),
                                                            light_count,
                                                            camera_component,
                                                            culling_scratch,
                                                            culling_config_);
            result.culling_build_invoked = true;
            ++stats.culling_build_call_count;

            culling_cache_valid = true;
            culling_cache_style_signature = runtime_cache_style_signature;
            culling_cache_binding_signature = runtime_cache_binding_signature;
            culling_cache_transform_signature = runtime_cache_transform_signature;
            culling_cache_camera_revision = camera_revision;
            culling_cache_culling_config_signature = culling_config_signature;
        } else {
            result.culling_stats = last_prepare_result.culling_stats;
            ++stats.cross_frame_reuse_hit_count;
        }

        result.has_light_data = true;
        ConsumeNormalizedHints();

        last_prepare_result = result;
        frame_cache_valid = true;
        frame_cache_frame_index = frame_index_;
        frame_cache_source_revision = source_revision;
        frame_cache_camera_revision = camera_revision;
        frame_cache_light_dirty_revision = light_dirty_revision;
        frame_cache_transform_dirty_revision = transform_dirty_revision;
        frame_cache_culling_config_signature = culling_config_signature;
        return result;
    }

    [[nodiscard]] const RuntimeScratchType& RuntimeScratch() const noexcept {
        return runtime_scratch;
    }

    [[nodiscard]] const CullingScratchType& CullingScratch() const noexcept {
        return culling_scratch;
    }

    [[nodiscard]] const PrepareStageResultType& LastPrepareResult() const noexcept {
        return last_prepare_result;
    }

    [[nodiscard]] const LightFrameCoordinatorStats<DimensionT>& Stats() const noexcept {
        return stats;
    }

private:
    void InvalidateCaches() noexcept {
        frame_cache_valid = false;
        frame_cache_frame_index = invalid_frame_index;
        frame_cache_source_revision = 0U;
        frame_cache_camera_revision = 0U;
        frame_cache_light_dirty_revision = 0U;
        frame_cache_transform_dirty_revision = 0U;
        frame_cache_culling_config_signature = 0U;

        runtime_cache_valid = false;
        runtime_cache_source_revision = 0U;
        runtime_cache_light_dirty_revision = 0U;
        runtime_cache_transform_dirty_revision = 0U;
        runtime_cache_light_count = 0U;
        runtime_cache_light_components = nullptr;
        runtime_cache_transforms = nullptr;
        runtime_cache_style_signature = 0U;
        runtime_cache_binding_signature = 0U;
        runtime_cache_transform_signature = 0U;

        culling_cache_valid = false;
        culling_cache_style_signature = 0U;
        culling_cache_binding_signature = 0U;
        culling_cache_transform_signature = 0U;
        culling_cache_camera_revision = 0U;
        culling_cache_culling_config_signature = 0U;
    }

    void ResetAccumulatedDirtyHints() noexcept {
        accumulated_light_dirty_indices.clear();
        accumulated_transform_dirty_indices.clear();
        normalized_light_dirty_indices.clear();
        normalized_transform_dirty_indices.clear();
    }

    static void AppendDirtyHint(const std::uint32_t* dirty_component_indices_,
                                std::uint32_t dirty_component_count_,
                                LightFrameCoordinatorMcVector<std::uint32_t>& out_indices_,
                                std::uint64_t& input_counter_) {
        if (dirty_component_indices_ == nullptr || dirty_component_count_ == 0U) {
            return;
        }
        input_counter_ += static_cast<std::uint64_t>(dirty_component_count_);
        const std::size_t old_size = out_indices_.size();
        out_indices_.resize(old_size + dirty_component_count_);
        for (std::uint32_t i = 0U; i < dirty_component_count_; ++i) {
            out_indices_[old_size + i] = dirty_component_indices_[i];
        }
    }

    void NormalizeDirtyHints() {
        NormalizeOneDirtyHint(accumulated_light_dirty_indices,
                              normalized_light_dirty_indices,
                              light_dirty_marker_stamps,
                              light_dirty_marker_epoch,
                              stats.light_dirty_hint_unique_count,
                              stats.light_dirty_hint_out_of_range_drop_count,
                              stats.light_dirty_hint_duplicate_drop_count);
        NormalizeOneDirtyHint(accumulated_transform_dirty_indices,
                              normalized_transform_dirty_indices,
                              transform_dirty_marker_stamps,
                              transform_dirty_marker_epoch,
                              stats.transform_dirty_hint_unique_count,
                              stats.transform_dirty_hint_out_of_range_drop_count,
                              stats.transform_dirty_hint_duplicate_drop_count);
    }

    void ConsumeNormalizedHints() noexcept {
        accumulated_light_dirty_indices.clear();
        accumulated_transform_dirty_indices.clear();
        normalized_light_dirty_indices.clear();
        normalized_transform_dirty_indices.clear();
    }

    void NormalizeOneDirtyHint(const LightFrameCoordinatorMcVector<std::uint32_t>& raw_indices_,
                               LightFrameCoordinatorMcVector<std::uint32_t>& normalized_indices_,
                               LightFrameCoordinatorMcVector<std::uint32_t>& marker_stamps_,
                               std::uint32_t& marker_epoch_,
                               std::uint64_t& unique_counter_,
                               std::uint64_t& out_of_range_counter_,
                               std::uint64_t& duplicate_counter_) {
        normalized_indices_.clear();
        if (raw_indices_.empty()) {
            return;
        }
        if (marker_stamps_.size() < light_count) {
            marker_stamps_.resize(light_count, 0U);
        }
        if (marker_epoch_ == 0U || marker_epoch_ == (std::numeric_limits<std::uint32_t>::max)()) {
            for (std::size_t i = 0U; i < marker_stamps_.size(); ++i) {
                marker_stamps_[i] = 0U;
            }
            marker_epoch_ = 1U;
        } else {
            ++marker_epoch_;
        }

        for (const std::uint32_t component_index : raw_indices_) {
            if (component_index >= light_count) {
                ++out_of_range_counter_;
                continue;
            }
            if (marker_stamps_[component_index] == marker_epoch_) {
                ++duplicate_counter_;
                continue;
            }
            marker_stamps_[component_index] = marker_epoch_;
            normalized_indices_.push_back(component_index);
            ++unique_counter_;
        }
    }

    [[nodiscard]] static std::uint64_t ComposeCullingConfigSignature(const CullingBuildConfigType& config_) noexcept {
        auto bit_cast_float_to_u32 = [](float value_) noexcept -> std::uint32_t {
            std::uint32_t out = 0U;
            std::memcpy(&out, &value_, sizeof(std::uint32_t));
            return out;
        };
        std::uint64_t hash = 14695981039346656037ULL;
        auto hash_combine = [&hash](std::uint64_t value_) noexcept {
            hash ^= value_;
            hash *= 1099511628211ULL;
        };
        if constexpr (std::same_as<DimensionT, ecs::Dim2>) {
            hash_combine(static_cast<std::uint64_t>(config_.tile_count_x));
            hash_combine(static_cast<std::uint64_t>(config_.tile_count_y));
            hash_combine(static_cast<std::uint64_t>(config_.max_lights_per_tile));
            hash_combine(static_cast<std::uint64_t>(config_.stable_sort));
            hash_combine(static_cast<std::uint64_t>(config_.include_hidden_lights));
        } else {
            hash_combine(static_cast<std::uint64_t>(config_.cluster_count_x));
            hash_combine(static_cast<std::uint64_t>(config_.cluster_count_y));
            hash_combine(static_cast<std::uint64_t>(config_.cluster_count_z));
            hash_combine(static_cast<std::uint64_t>(config_.max_lights_per_cluster));
            hash_combine(static_cast<std::uint64_t>(config_.stable_sort));
            hash_combine(static_cast<std::uint64_t>(config_.include_hidden_lights));
            hash_combine(static_cast<std::uint64_t>(config_.reverse_z));
            hash_combine(static_cast<std::uint64_t>(bit_cast_float_to_u32(config_.near_plane)));
            hash_combine(static_cast<std::uint64_t>(bit_cast_float_to_u32(config_.far_plane)));
            hash_combine(static_cast<std::uint64_t>(bit_cast_float_to_u32(config_.z_slice_scale)));
            hash_combine(static_cast<std::uint64_t>(bit_cast_float_to_u32(config_.z_slice_bias)));
        }
        return hash;
    }

    LightType* light_components = nullptr;
    const TransformType* transforms = nullptr;
    const CameraType* camera_component = nullptr;
    std::uint32_t light_count = 0U;

    RuntimeScratchType runtime_scratch{};
    CullingScratchType culling_scratch{};

    LightFrameCoordinatorMcVector<std::uint32_t> accumulated_light_dirty_indices{};
    LightFrameCoordinatorMcVector<std::uint32_t> accumulated_transform_dirty_indices{};
    LightFrameCoordinatorMcVector<std::uint32_t> normalized_light_dirty_indices{};
    LightFrameCoordinatorMcVector<std::uint32_t> normalized_transform_dirty_indices{};
    LightFrameCoordinatorMcVector<std::uint32_t> light_dirty_marker_stamps{};
    LightFrameCoordinatorMcVector<std::uint32_t> transform_dirty_marker_stamps{};
    std::uint32_t light_dirty_marker_epoch = 1U;
    std::uint32_t transform_dirty_marker_epoch = 1U;

    PrepareStageResultType last_prepare_result{};

    std::uint64_t source_revision = 0U;
    std::uint64_t camera_revision = 0U;
    std::uint64_t light_dirty_revision = 0U;
    std::uint64_t transform_dirty_revision = 0U;

    bool frame_cache_valid = false;
    std::uint32_t frame_cache_frame_index = invalid_frame_index;
    std::uint64_t frame_cache_source_revision = 0U;
    std::uint64_t frame_cache_camera_revision = 0U;
    std::uint64_t frame_cache_light_dirty_revision = 0U;
    std::uint64_t frame_cache_transform_dirty_revision = 0U;
    std::uint64_t frame_cache_culling_config_signature = 0U;

    bool runtime_cache_valid = false;
    std::uint64_t runtime_cache_source_revision = 0U;
    std::uint64_t runtime_cache_light_dirty_revision = 0U;
    std::uint64_t runtime_cache_transform_dirty_revision = 0U;
    std::uint32_t runtime_cache_light_count = 0U;
    const LightType* runtime_cache_light_components = nullptr;
    const TransformType* runtime_cache_transforms = nullptr;
    std::uint64_t runtime_cache_style_signature = 0U;
    std::uint64_t runtime_cache_binding_signature = 0U;
    std::uint64_t runtime_cache_transform_signature = 0U;

    bool culling_cache_valid = false;
    std::uint64_t culling_cache_style_signature = 0U;
    std::uint64_t culling_cache_binding_signature = 0U;
    std::uint64_t culling_cache_transform_signature = 0U;
    std::uint64_t culling_cache_camera_revision = 0U;
    std::uint64_t culling_cache_culling_config_signature = 0U;

    LightFrameCoordinatorStats<DimensionT> stats{};
};

} // namespace vr::render


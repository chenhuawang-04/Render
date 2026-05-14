#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/shadow_prepare_stage.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace vr::render {

template<typename T>
using ShadowFrameCoordinatorMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

template<ecs::DimensionTag DimensionT>
struct ShadowFrameCoordinatorStats final {
    std::uint32_t prepare_call_count = 0U;
    std::uint32_t runtime_build_call_count = 0U;
    std::uint32_t caster_build_call_count = 0U;
    std::uint32_t same_frame_reuse_hit_count = 0U;
    std::uint32_t cross_frame_reuse_hit_count = 0U;

    std::uint64_t shadow_dirty_hint_input_count = 0U;
    std::uint64_t shadow_dirty_hint_unique_count = 0U;
    std::uint64_t shadow_dirty_hint_out_of_range_drop_count = 0U;
    std::uint64_t shadow_dirty_hint_duplicate_drop_count = 0U;

    std::uint64_t transform_dirty_hint_input_count = 0U;
    std::uint64_t transform_dirty_hint_unique_count = 0U;
    std::uint64_t transform_dirty_hint_out_of_range_drop_count = 0U;
    std::uint64_t transform_dirty_hint_duplicate_drop_count = 0U;
};

template<ecs::DimensionTag DimensionT>
class ShadowFrameCoordinator final {
public:
    using ShadowType = ecs::Shadow<DimensionT>;
    using TransformType = ecs::Transform<DimensionT>;
    using CameraType = ecs::Camera<DimensionT>;
    using BoundsType = ecs::Bounds<DimensionT>;

    using PrepareStageType = ShadowPrepareStage<DimensionT>;
    using PrepareStageResultType = ShadowPrepareStageResult<DimensionT>;

    using RuntimeScratchType = ecs::ShadowRuntimeScratch<DimensionT>;
    using RuntimeBuildConfigType = ecs::ShadowRuntimeBuildConfig;
    using CasterScratchType = ecs::ShadowCasterScratch<DimensionT>;
    using CasterBuildConfigType = ecs::ShadowCasterBuildConfig;

    static constexpr std::uint32_t invalid_frame_index = (std::numeric_limits<std::uint32_t>::max)();

    ShadowFrameCoordinator() = default;
    ~ShadowFrameCoordinator() = default;

    ShadowFrameCoordinator(const ShadowFrameCoordinator&) = delete;
    ShadowFrameCoordinator& operator=(const ShadowFrameCoordinator&) = delete;
    ShadowFrameCoordinator(ShadowFrameCoordinator&&) = delete;
    ShadowFrameCoordinator& operator=(ShadowFrameCoordinator&&) = delete;

    void SetShadowData(ShadowType* shadow_components_,
                       const TransformType* transforms_,
                       std::uint32_t shadow_count_) noexcept {
        if (shadow_components == shadow_components_ &&
            transforms == transforms_ &&
            shadow_count == shadow_count_) {
            return;
        }
        shadow_components = shadow_components_;
        transforms = transforms_;
        shadow_count = shadow_count_;
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
        frame_cache_valid = false;
        runtime_cache_valid = false;
        caster_cache_valid = false;
    }

    void SetCasterBounds(const BoundsType* caster_bounds_,
                         std::uint32_t caster_count_) noexcept {
        if (caster_bounds == caster_bounds_ && caster_count == caster_count_) {
            return;
        }
        caster_bounds = caster_bounds_;
        caster_count = caster_count_;
        ++caster_source_revision;
        frame_cache_valid = false;
        caster_cache_valid = false;
    }

    void SetShadowDirtyHint(const std::uint32_t* dirty_component_indices_,
                            std::uint32_t dirty_component_count_) noexcept {
        AppendDirtyHint(dirty_component_indices_,
                        dirty_component_count_,
                        accumulated_shadow_dirty_indices,
                        stats.shadow_dirty_hint_input_count);
        if (dirty_component_indices_ != nullptr && dirty_component_count_ > 0U) {
            ++shadow_dirty_revision;
            frame_cache_valid = false;
            runtime_cache_valid = false;
            caster_cache_valid = false;
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
            caster_cache_valid = false;
        }
    }

    void Reserve(std::uint32_t shadow_count_, std::uint32_t caster_count_) {
        PrepareStageType::Reserve(runtime_scratch, caster_scratch, shadow_count_, caster_count_);

        const std::size_t reserve_count = static_cast<std::size_t>(shadow_count_);
        if (accumulated_shadow_dirty_indices.capacity() < reserve_count) {
            accumulated_shadow_dirty_indices.reserve(reserve_count);
        }
        if (accumulated_transform_dirty_indices.capacity() < reserve_count) {
            accumulated_transform_dirty_indices.reserve(reserve_count);
        }
        if (normalized_shadow_dirty_indices.capacity() < reserve_count) {
            normalized_shadow_dirty_indices.reserve(reserve_count);
        }
        if (normalized_transform_dirty_indices.capacity() < reserve_count) {
            normalized_transform_dirty_indices.reserve(reserve_count);
        }
        if (shadow_dirty_marker_stamps.size() < reserve_count) {
            shadow_dirty_marker_stamps.resize(reserve_count, 0U);
        }
        if (transform_dirty_marker_stamps.size() < reserve_count) {
            transform_dirty_marker_stamps.resize(reserve_count, 0U);
        }
    }

    [[nodiscard]] PrepareStageResultType PrepareFrame(
        std::uint32_t frame_index_,
        const RuntimeBuildConfigType& runtime_config_ = RuntimeBuildConfigType{},
        const CasterBuildConfigType& caster_config_ = CasterBuildConfigType{}) {
        ++stats.prepare_call_count;

        if (shadow_components == nullptr || shadow_count == 0U) {
            last_prepare_result = {};
            InvalidateCaches();
            return last_prepare_result;
        }

        const bool can_reuse_same_frame =
            frame_cache_valid &&
            frame_cache_frame_index == frame_index_ &&
            frame_cache_source_revision == source_revision &&
            frame_cache_camera_revision == camera_revision &&
            frame_cache_caster_source_revision == caster_source_revision &&
            frame_cache_shadow_dirty_revision == shadow_dirty_revision &&
            frame_cache_transform_dirty_revision == transform_dirty_revision &&
            frame_cache_runtime_config_signature == ComposeRuntimeConfigSignature(runtime_config_) &&
            frame_cache_caster_config_signature == ComposeCasterConfigSignature(caster_config_);
        if (can_reuse_same_frame) {
            ++stats.same_frame_reuse_hit_count;
            PrepareStageResultType reused_result = last_prepare_result;
            reused_result.runtime_build_invoked = false;
            reused_result.caster_build_invoked = false;
            return reused_result;
        }

        NormalizeDirtyHints();

        const bool runtime_dirty = !normalized_shadow_dirty_indices.empty() ||
                                   !normalized_transform_dirty_indices.empty();
        const bool need_runtime_build = !runtime_cache_valid ||
                                        runtime_dirty ||
                                        runtime_cache_source_revision != source_revision ||
                                        runtime_cache_camera_revision != camera_revision ||
                                        runtime_cache_shadow_count != shadow_count ||
                                        runtime_cache_shadow_components != shadow_components ||
                                        runtime_cache_transforms != transforms;

        PrepareStageResultType result{};
        result.has_shadow_data = true;

        if (need_runtime_build) {
            result.runtime_stats = PrepareStageType::BuildRuntimeOnly(
                shadow_components,
                transforms,
                camera_component,
                shadow_count,
                runtime_scratch,
                normalized_shadow_dirty_indices.empty() ? nullptr : normalized_shadow_dirty_indices.data(),
                static_cast<std::uint32_t>(normalized_shadow_dirty_indices.size()),
                normalized_transform_dirty_indices.empty() ? nullptr : normalized_transform_dirty_indices.data(),
                static_cast<std::uint32_t>(normalized_transform_dirty_indices.size()),
                runtime_config_).runtime_stats;
            result.runtime_build_invoked = true;
            ++stats.runtime_build_call_count;

            runtime_cache_valid = true;
            runtime_cache_source_revision = source_revision;
            runtime_cache_camera_revision = camera_revision;
            runtime_cache_shadow_dirty_revision = shadow_dirty_revision;
            runtime_cache_transform_dirty_revision = transform_dirty_revision;
            runtime_cache_shadow_count = shadow_count;
            runtime_cache_shadow_components = shadow_components;
            runtime_cache_transforms = transforms;
            runtime_cache_style_signature = result.runtime_stats.style_signature;
            runtime_cache_binding_signature = result.runtime_stats.binding_signature;
            runtime_cache_transform_signature = result.runtime_stats.transform_signature;
            runtime_cache_camera_signature = result.runtime_stats.camera_signature;
        } else {
            result.runtime_stats = last_prepare_result.runtime_stats;
            ++stats.cross_frame_reuse_hit_count;
        }

        const bool need_caster_build = !caster_cache_valid ||
                                       need_runtime_build ||
                                       caster_cache_caster_source_revision != caster_source_revision ||
                                       caster_cache_style_signature != runtime_cache_style_signature ||
                                       caster_cache_binding_signature != runtime_cache_binding_signature ||
                                       caster_cache_transform_signature != runtime_cache_transform_signature ||
                                       caster_cache_camera_signature != runtime_cache_camera_signature ||
                                       caster_cache_caster_config_signature != ComposeCasterConfigSignature(caster_config_);
        if (need_caster_build) {
            result.caster_stats = ecs::ShadowCasterSystem<DimensionT>::Build(
                shadow_components,
                shadow_count,
                ecs::ShadowRuntimeSystem<DimensionT>::ViewRecords(runtime_scratch),
                ecs::ShadowRuntimeSystem<DimensionT>::ViewRecordCount(runtime_scratch),
                caster_bounds,
                caster_count,
                caster_scratch,
                caster_config_);
            result.caster_build_invoked = true;
            ++stats.caster_build_call_count;

            caster_cache_valid = true;
            caster_cache_caster_source_revision = caster_source_revision;
            caster_cache_style_signature = runtime_cache_style_signature;
            caster_cache_binding_signature = runtime_cache_binding_signature;
            caster_cache_transform_signature = runtime_cache_transform_signature;
            caster_cache_camera_signature = runtime_cache_camera_signature;
            caster_cache_caster_config_signature = ComposeCasterConfigSignature(caster_config_);
        } else {
            result.caster_stats = last_prepare_result.caster_stats;
            ++stats.cross_frame_reuse_hit_count;
        }

        result.has_shadow_data = true;
        ConsumeNormalizedHints();

        last_prepare_result = result;
        frame_cache_valid = true;
        frame_cache_frame_index = frame_index_;
        frame_cache_source_revision = source_revision;
        frame_cache_camera_revision = camera_revision;
        frame_cache_caster_source_revision = caster_source_revision;
        frame_cache_shadow_dirty_revision = shadow_dirty_revision;
        frame_cache_transform_dirty_revision = transform_dirty_revision;
        frame_cache_runtime_config_signature = ComposeRuntimeConfigSignature(runtime_config_);
        frame_cache_caster_config_signature = ComposeCasterConfigSignature(caster_config_);
        return result;
    }

    [[nodiscard]] const RuntimeScratchType& RuntimeScratch() const noexcept {
        return runtime_scratch;
    }

    [[nodiscard]] const ShadowType* ShadowComponents() const noexcept {
        return shadow_components;
    }

    [[nodiscard]] std::uint32_t ShadowCount() const noexcept {
        return shadow_count;
    }

    [[nodiscard]] const CasterScratchType& CasterScratch() const noexcept {
        return caster_scratch;
    }

    [[nodiscard]] const PrepareStageResultType& LastPrepareResult() const noexcept {
        return last_prepare_result;
    }

    [[nodiscard]] const ShadowFrameCoordinatorStats<DimensionT>& Stats() const noexcept {
        return stats;
    }

private:
    static constexpr std::uint64_t k_hash_offset_basis = 14695981039346656037ULL;
    static constexpr std::uint64_t k_hash_prime = 1099511628211ULL;

    static void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= k_hash_prime;
    }

    static void HashFloat(std::uint64_t& hash_, float value_) noexcept {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value_, sizeof(bits));
        HashCombine(hash_, static_cast<std::uint64_t>(bits));
    }

    static std::uint64_t ComposeRuntimeConfigSignature(const RuntimeBuildConfigType& config_) noexcept {
        std::uint64_t hash = k_hash_offset_basis;
        HashCombine(hash, static_cast<std::uint64_t>(config_.force_full_rebuild));
        HashCombine(hash, static_cast<std::uint64_t>(config_.rebuild_keys_even_if_clean));
        HashCombine(hash, static_cast<std::uint64_t>(config_.merge_gap));
        HashCombine(hash, static_cast<std::uint64_t>(config_.atlas_width));
        HashCombine(hash, static_cast<std::uint64_t>(config_.atlas_height));
        HashCombine(hash, static_cast<std::uint64_t>(config_.atlas_layer_count));
        HashCombine(hash, static_cast<std::uint64_t>(config_.enable_atlas_packing));
        return hash;
    }

    static std::uint64_t ComposeCasterConfigSignature(const CasterBuildConfigType& config_) noexcept {
        std::uint64_t hash = k_hash_offset_basis;
        HashCombine(hash, static_cast<std::uint64_t>(config_.max_casters_per_view));
        HashCombine(hash, static_cast<std::uint64_t>(config_.stable_sort));
        HashCombine(hash, static_cast<std::uint64_t>(config_.include_hidden_shadows));
        return hash;
    }

    void InvalidateCaches() noexcept {
        frame_cache_valid = false;
        frame_cache_frame_index = invalid_frame_index;
        frame_cache_source_revision = 0U;
        frame_cache_camera_revision = 0U;
        frame_cache_caster_source_revision = 0U;
        frame_cache_shadow_dirty_revision = 0U;
        frame_cache_transform_dirty_revision = 0U;
        frame_cache_runtime_config_signature = 0U;
        frame_cache_caster_config_signature = 0U;

        runtime_cache_valid = false;
        runtime_cache_source_revision = 0U;
        runtime_cache_camera_revision = 0U;
        runtime_cache_shadow_dirty_revision = 0U;
        runtime_cache_transform_dirty_revision = 0U;
        runtime_cache_shadow_count = 0U;
        runtime_cache_shadow_components = nullptr;
        runtime_cache_transforms = nullptr;
        runtime_cache_style_signature = 0U;
        runtime_cache_binding_signature = 0U;
        runtime_cache_transform_signature = 0U;
        runtime_cache_camera_signature = 0U;

        caster_cache_valid = false;
        caster_cache_caster_source_revision = 0U;
        caster_cache_style_signature = 0U;
        caster_cache_binding_signature = 0U;
        caster_cache_transform_signature = 0U;
        caster_cache_camera_signature = 0U;
        caster_cache_caster_config_signature = 0U;
    }

    void ResetAccumulatedDirtyHints() noexcept {
        accumulated_shadow_dirty_indices.clear();
        accumulated_transform_dirty_indices.clear();
        normalized_shadow_dirty_indices.clear();
        normalized_transform_dirty_indices.clear();
    }

    static void AppendDirtyHint(const std::uint32_t* dirty_component_indices_,
                                std::uint32_t dirty_component_count_,
                                ShadowFrameCoordinatorMcVector<std::uint32_t>& out_indices_,
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
        NormalizeOneDirtyHint(accumulated_shadow_dirty_indices,
                              normalized_shadow_dirty_indices,
                              shadow_dirty_marker_stamps,
                              shadow_dirty_marker_epoch,
                              stats.shadow_dirty_hint_unique_count,
                              stats.shadow_dirty_hint_out_of_range_drop_count,
                              stats.shadow_dirty_hint_duplicate_drop_count);
        NormalizeOneDirtyHint(accumulated_transform_dirty_indices,
                              normalized_transform_dirty_indices,
                              transform_dirty_marker_stamps,
                              transform_dirty_marker_epoch,
                              stats.transform_dirty_hint_unique_count,
                              stats.transform_dirty_hint_out_of_range_drop_count,
                              stats.transform_dirty_hint_duplicate_drop_count);
    }

    void ConsumeNormalizedHints() noexcept {
        accumulated_shadow_dirty_indices.clear();
        accumulated_transform_dirty_indices.clear();
        normalized_shadow_dirty_indices.clear();
        normalized_transform_dirty_indices.clear();
    }

    void NormalizeOneDirtyHint(const ShadowFrameCoordinatorMcVector<std::uint32_t>& raw_indices_,
                               ShadowFrameCoordinatorMcVector<std::uint32_t>& normalized_indices_,
                               ShadowFrameCoordinatorMcVector<std::uint32_t>& marker_stamps_,
                               std::uint32_t& marker_epoch_,
                               std::uint64_t& unique_counter_,
                               std::uint64_t& out_of_range_counter_,
                               std::uint64_t& duplicate_counter_) {
        normalized_indices_.clear();
        if (raw_indices_.empty()) {
            return;
        }
        if (marker_stamps_.size() < shadow_count) {
            marker_stamps_.resize(shadow_count, 0U);
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
            if (component_index >= shadow_count) {
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

private:
    ShadowType* shadow_components = nullptr;
    const TransformType* transforms = nullptr;
    const CameraType* camera_component = nullptr;
    const BoundsType* caster_bounds = nullptr;
    std::uint32_t shadow_count = 0U;
    std::uint32_t caster_count = 0U;

    RuntimeScratchType runtime_scratch{};
    CasterScratchType caster_scratch{};

    ShadowFrameCoordinatorMcVector<std::uint32_t> accumulated_shadow_dirty_indices{};
    ShadowFrameCoordinatorMcVector<std::uint32_t> accumulated_transform_dirty_indices{};
    ShadowFrameCoordinatorMcVector<std::uint32_t> normalized_shadow_dirty_indices{};
    ShadowFrameCoordinatorMcVector<std::uint32_t> normalized_transform_dirty_indices{};
    ShadowFrameCoordinatorMcVector<std::uint32_t> shadow_dirty_marker_stamps{};
    ShadowFrameCoordinatorMcVector<std::uint32_t> transform_dirty_marker_stamps{};
    std::uint32_t shadow_dirty_marker_epoch = 1U;
    std::uint32_t transform_dirty_marker_epoch = 1U;

    PrepareStageResultType last_prepare_result{};
    ShadowFrameCoordinatorStats<DimensionT> stats{};

    std::uint32_t source_revision = 0U;
    std::uint32_t camera_revision = 0U;
    std::uint32_t caster_source_revision = 0U;
    std::uint32_t shadow_dirty_revision = 0U;
    std::uint32_t transform_dirty_revision = 0U;

    bool frame_cache_valid = false;
    std::uint32_t frame_cache_frame_index = invalid_frame_index;
    std::uint32_t frame_cache_source_revision = 0U;
    std::uint32_t frame_cache_camera_revision = 0U;
    std::uint32_t frame_cache_caster_source_revision = 0U;
    std::uint32_t frame_cache_shadow_dirty_revision = 0U;
    std::uint32_t frame_cache_transform_dirty_revision = 0U;
    std::uint64_t frame_cache_runtime_config_signature = 0U;
    std::uint64_t frame_cache_caster_config_signature = 0U;

    bool runtime_cache_valid = false;
    std::uint32_t runtime_cache_source_revision = 0U;
    std::uint32_t runtime_cache_camera_revision = 0U;
    std::uint32_t runtime_cache_shadow_dirty_revision = 0U;
    std::uint32_t runtime_cache_transform_dirty_revision = 0U;
    std::uint32_t runtime_cache_shadow_count = 0U;
    ShadowType* runtime_cache_shadow_components = nullptr;
    const TransformType* runtime_cache_transforms = nullptr;
    std::uint64_t runtime_cache_style_signature = 0U;
    std::uint64_t runtime_cache_binding_signature = 0U;
    std::uint64_t runtime_cache_transform_signature = 0U;
    std::uint64_t runtime_cache_camera_signature = 0U;

    bool caster_cache_valid = false;
    std::uint32_t caster_cache_caster_source_revision = 0U;
    std::uint64_t caster_cache_style_signature = 0U;
    std::uint64_t caster_cache_binding_signature = 0U;
    std::uint64_t caster_cache_transform_signature = 0U;
    std::uint64_t caster_cache_camera_signature = 0U;
    std::uint64_t caster_cache_caster_config_signature = 0U;
};

} // namespace vr::render


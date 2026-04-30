#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/light_shadow_link_stage.hpp"

#include <cstddef>
#include <cstdint>

namespace vr::render {

template<typename T>
using LightShadowLinkCoordinatorMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct LightShadowLinkCoordinator2DPrepareInfo final {
    std::uint64_t signature = 0U;
    std::uint64_t light_signature = 0U;
    std::uint64_t shadow_signature = 0U;
    const ecs::LightGpuRecord2D* light_records = nullptr;
    std::uint32_t light_record_count = 0U;
    const ecs::Shadow<ecs::Dim2>* shadow_components = nullptr;
    std::uint32_t shadow_component_count = 0U;
    const ecs::ShadowGpuRecord2D* shadow_records = nullptr;
    std::uint32_t shadow_record_count = 0U;
    std::uint32_t shadow_namespace_hint = 0U;

    const std::uint32_t* light_updated_component_indices = nullptr;
    std::uint32_t light_updated_component_count = 0U;
    std::uint8_t allow_incremental_light_patch = 1U;
};

struct LightShadowLinkCoordinator2DResult final {
    LightShadowLinkStageResult2D link_result{};
    bool cache_reused = false;
};

struct LightShadowLinkCoordinator2DStats final {
    std::uint32_t build_count = 0U;
    std::uint32_t cache_reuse_hit_count = 0U;
    std::uint32_t incremental_patch_count = 0U;
    std::uint64_t incremental_patched_light_count = 0U;
};

class LightShadowLinkCoordinator2D final {
public:
    LightShadowLinkCoordinator2D() = default;
    ~LightShadowLinkCoordinator2D() = default;

    LightShadowLinkCoordinator2D(const LightShadowLinkCoordinator2D&) = delete;
    LightShadowLinkCoordinator2D& operator=(const LightShadowLinkCoordinator2D&) = delete;
    LightShadowLinkCoordinator2D(LightShadowLinkCoordinator2D&&) = delete;
    LightShadowLinkCoordinator2D& operator=(LightShadowLinkCoordinator2D&&) = delete;

    void Reset() noexcept {
        cache_signature = 0U;
        cache_shadow_signature = 0U;
        cached_shadow_namespace_id = 0U;
        cached_linked_light_count = 0U;
        cached_namespace_drop_count = 0U;
        cached_unmapped_light_count = 0U;
        cache_valid = false;
        linked_light_records_scratch.clear();
        cached_light_record_count = 0U;
    }

    void Reserve(std::uint32_t light_record_count_) {
        const std::size_t target = static_cast<std::size_t>(light_record_count_);
        if (linked_light_records_scratch.capacity() < target) {
            linked_light_records_scratch.reserve(target);
        }
    }

    [[nodiscard]] LightShadowLinkCoordinator2DResult Prepare(
        const LightShadowLinkCoordinator2DPrepareInfo& prepare_info_) {
        LightShadowLinkCoordinator2DResult result{};
        if (prepare_info_.light_records == nullptr || prepare_info_.light_record_count == 0U) {
            cache_valid = false;
            linked_light_records_scratch.clear();
            cached_light_record_count = 0U;
            return result;
        }

        const std::uint64_t resolved_shadow_signature =
            (prepare_info_.shadow_signature != 0U) ? prepare_info_.shadow_signature : prepare_info_.signature;

        const bool can_reuse =
            cache_valid &&
            cache_signature == prepare_info_.signature &&
            linked_light_records_scratch.size() == prepare_info_.light_record_count;
        if (can_reuse) {
            result.link_result.linked_light_records = linked_light_records_scratch.data();
            result.link_result.linked_light_record_count = prepare_info_.light_record_count;
            result.link_result.shadow_namespace_id = cached_shadow_namespace_id;
            result.link_result.linked_light_count = cached_linked_light_count;
            result.link_result.namespace_drop_count = cached_namespace_drop_count;
            result.link_result.unmapped_light_count = cached_unmapped_light_count;
            result.cache_reused = true;
            ++stats.cache_reuse_hit_count;
            return result;
        }

        const std::uint32_t incremental_patched_count =
            TryIncrementalLightPatch(prepare_info_, resolved_shadow_signature);
        if (incremental_patched_count > 0U) {
            cache_signature = prepare_info_.signature;
            cache_shadow_signature = resolved_shadow_signature;

            result.link_result.linked_light_records = linked_light_records_scratch.data();
            result.link_result.linked_light_record_count = prepare_info_.light_record_count;
            result.link_result.shadow_namespace_id = cached_shadow_namespace_id;
            result.link_result.linked_light_count = cached_linked_light_count;
            result.link_result.namespace_drop_count = cached_namespace_drop_count;
            result.link_result.unmapped_light_count = cached_unmapped_light_count;
            result.cache_reused = false;
            ++stats.incremental_patch_count;
            stats.incremental_patched_light_count += incremental_patched_count;
            return result;
        }

        linked_light_records_scratch.clear();
        result.link_result = LightShadowLinkStage::BuildLinkedLightRecords2D(
            prepare_info_.light_records,
            prepare_info_.light_record_count,
            prepare_info_.shadow_components,
            prepare_info_.shadow_component_count,
            prepare_info_.shadow_records,
            prepare_info_.shadow_record_count,
            prepare_info_.shadow_namespace_hint,
            linked_light_records_scratch);
        result.cache_reused = false;

        cache_signature = prepare_info_.signature;
        cache_shadow_signature = resolved_shadow_signature;
        cached_shadow_namespace_id = result.link_result.shadow_namespace_id;
        cached_linked_light_count = result.link_result.linked_light_count;
        cached_namespace_drop_count = result.link_result.namespace_drop_count;
        cached_unmapped_light_count = result.link_result.unmapped_light_count;
        cache_valid = true;
        cached_light_record_count = prepare_info_.light_record_count;
        ++stats.build_count;
        return result;
    }

    [[nodiscard]] const LightShadowLinkCoordinator2DStats& Stats() const noexcept {
        return stats;
    }

private:
    [[nodiscard]] bool CanUseIncrementalPath(const LightShadowLinkCoordinator2DPrepareInfo& prepare_info_,
                                             std::uint64_t resolved_shadow_signature_) const noexcept {
        if (!cache_valid ||
            prepare_info_.allow_incremental_light_patch == 0U ||
            prepare_info_.light_updated_component_indices == nullptr ||
            prepare_info_.light_updated_component_count == 0U) {
            return false;
        }
        if (cache_shadow_signature != resolved_shadow_signature_) {
            return false;
        }
        if (linked_light_records_scratch.size() != prepare_info_.light_record_count ||
            cached_light_record_count != prepare_info_.light_record_count) {
            return false;
        }
        if (prepare_info_.light_updated_component_count >= prepare_info_.light_record_count) {
            return false;
        }
        const std::uint64_t updated_count = static_cast<std::uint64_t>(prepare_info_.light_updated_component_count);
        const std::uint64_t light_count = static_cast<std::uint64_t>(prepare_info_.light_record_count);
        return (updated_count * 4U) < (light_count * 3U);
    }

    [[nodiscard]] std::uint32_t TryIncrementalLightPatch(
        const LightShadowLinkCoordinator2DPrepareInfo& prepare_info_,
        std::uint64_t resolved_shadow_signature_) {
        if (!CanUseIncrementalPath(prepare_info_, resolved_shadow_signature_)) {
            return 0U;
        }

        std::uint32_t patched_count = 0U;
        for (std::uint32_t i = 0U; i < prepare_info_.light_updated_component_count; ++i) {
            const std::uint32_t light_index = prepare_info_.light_updated_component_indices[i];
            if (light_index >= prepare_info_.light_record_count) {
                continue;
            }

            ecs::LightGpuRecord2D patched_record = prepare_info_.light_records[light_index];
            const ecs::LightGpuRecord2D& linked_record = linked_light_records_scratch[light_index];
            patched_record.shadow_view_begin = linked_record.shadow_view_begin;
            patched_record.shadow_meta = linked_record.shadow_meta;
            patched_record.shadow_namespace_id = linked_record.shadow_namespace_id;
            patched_record.reserved0 = linked_record.reserved0;
            linked_light_records_scratch[light_index] = patched_record;
            ++patched_count;
        }
        return patched_count;
    }

private:
    LightShadowLinkCoordinatorMcVector<ecs::LightGpuRecord2D> linked_light_records_scratch{};

    std::uint64_t cache_signature = 0U;
    std::uint64_t cache_shadow_signature = 0U;
    std::uint32_t cached_light_record_count = 0U;
    std::uint32_t cached_shadow_namespace_id = 0U;
    std::uint32_t cached_linked_light_count = 0U;
    std::uint32_t cached_namespace_drop_count = 0U;
    std::uint32_t cached_unmapped_light_count = 0U;
    bool cache_valid = false;

    LightShadowLinkCoordinator2DStats stats{};
};

struct LightShadowLinkCoordinator3DPrepareInfo final {
    std::uint64_t signature = 0U;
    std::uint64_t light_signature = 0U;
    std::uint64_t shadow_signature = 0U;
    const ecs::LightGpuRecord3D* light_records = nullptr;
    std::uint32_t light_record_count = 0U;
    const ecs::Shadow<ecs::Dim3>* shadow_components = nullptr;
    std::uint32_t shadow_component_count = 0U;
    const ecs::ShadowGpuRecord3D* shadow_records = nullptr;
    std::uint32_t shadow_record_count = 0U;
    std::uint32_t shadow_namespace_hint = 0U;

    const std::uint32_t* light_updated_component_indices = nullptr;
    std::uint32_t light_updated_component_count = 0U;
    std::uint8_t allow_incremental_light_patch = 1U;
};

struct LightShadowLinkCoordinator3DResult final {
    LightShadowLinkStageResult3D link_result{};
    bool cache_reused = false;
};

struct LightShadowLinkCoordinator3DStats final {
    std::uint32_t build_count = 0U;
    std::uint32_t cache_reuse_hit_count = 0U;
    std::uint32_t incremental_patch_count = 0U;
    std::uint64_t incremental_patched_light_count = 0U;
};

class LightShadowLinkCoordinator3D final {
public:
    LightShadowLinkCoordinator3D() = default;
    ~LightShadowLinkCoordinator3D() = default;

    LightShadowLinkCoordinator3D(const LightShadowLinkCoordinator3D&) = delete;
    LightShadowLinkCoordinator3D& operator=(const LightShadowLinkCoordinator3D&) = delete;
    LightShadowLinkCoordinator3D(LightShadowLinkCoordinator3D&&) = delete;
    LightShadowLinkCoordinator3D& operator=(LightShadowLinkCoordinator3D&&) = delete;

    void Reset() noexcept {
        cache_signature = 0U;
        cache_shadow_signature = 0U;
        cached_shadow_namespace_id = 0U;
        cached_linked_light_count = 0U;
        cached_namespace_drop_count = 0U;
        cached_unmapped_light_count = 0U;
        cache_valid = false;
        linked_light_records_scratch.clear();
        cached_light_record_count = 0U;
    }

    void Reserve(std::uint32_t light_record_count_) {
        const std::size_t target = static_cast<std::size_t>(light_record_count_);
        if (linked_light_records_scratch.capacity() < target) {
            linked_light_records_scratch.reserve(target);
        }
    }

    [[nodiscard]] LightShadowLinkCoordinator3DResult Prepare(
        const LightShadowLinkCoordinator3DPrepareInfo& prepare_info_) {
        LightShadowLinkCoordinator3DResult result{};
        if (prepare_info_.light_records == nullptr || prepare_info_.light_record_count == 0U) {
            cache_valid = false;
            linked_light_records_scratch.clear();
            cached_light_record_count = 0U;
            return result;
        }

        const std::uint64_t resolved_shadow_signature =
            (prepare_info_.shadow_signature != 0U) ? prepare_info_.shadow_signature : prepare_info_.signature;

        const bool can_reuse =
            cache_valid &&
            cache_signature == prepare_info_.signature &&
            linked_light_records_scratch.size() == prepare_info_.light_record_count;
        if (can_reuse) {
            result.link_result.linked_light_records = linked_light_records_scratch.data();
            result.link_result.linked_light_record_count = prepare_info_.light_record_count;
            result.link_result.shadow_namespace_id = cached_shadow_namespace_id;
            result.link_result.linked_light_count = cached_linked_light_count;
            result.link_result.namespace_drop_count = cached_namespace_drop_count;
            result.link_result.unmapped_light_count = cached_unmapped_light_count;
            result.cache_reused = true;
            ++stats.cache_reuse_hit_count;
            return result;
        }

        const std::uint32_t incremental_patched_count =
            TryIncrementalLightPatch(prepare_info_, resolved_shadow_signature);
        if (incremental_patched_count > 0U) {
            cache_signature = prepare_info_.signature;
            cache_shadow_signature = resolved_shadow_signature;

            result.link_result.linked_light_records = linked_light_records_scratch.data();
            result.link_result.linked_light_record_count = prepare_info_.light_record_count;
            result.link_result.shadow_namespace_id = cached_shadow_namespace_id;
            result.link_result.linked_light_count = cached_linked_light_count;
            result.link_result.namespace_drop_count = cached_namespace_drop_count;
            result.link_result.unmapped_light_count = cached_unmapped_light_count;
            result.cache_reused = false;
            ++stats.incremental_patch_count;
            stats.incremental_patched_light_count += incremental_patched_count;
            return result;
        }

        linked_light_records_scratch.clear();
        result.link_result = LightShadowLinkStage::BuildLinkedLightRecords3D(
            prepare_info_.light_records,
            prepare_info_.light_record_count,
            prepare_info_.shadow_components,
            prepare_info_.shadow_component_count,
            prepare_info_.shadow_records,
            prepare_info_.shadow_record_count,
            prepare_info_.shadow_namespace_hint,
            linked_light_records_scratch);
        result.cache_reused = false;

        cache_signature = prepare_info_.signature;
        cache_shadow_signature = resolved_shadow_signature;
        cached_shadow_namespace_id = result.link_result.shadow_namespace_id;
        cached_linked_light_count = result.link_result.linked_light_count;
        cached_namespace_drop_count = result.link_result.namespace_drop_count;
        cached_unmapped_light_count = result.link_result.unmapped_light_count;
        cache_valid = true;
        cached_light_record_count = prepare_info_.light_record_count;
        ++stats.build_count;
        return result;
    }

    [[nodiscard]] const LightShadowLinkCoordinator3DStats& Stats() const noexcept {
        return stats;
    }

private:
    [[nodiscard]] bool CanUseIncrementalPath(const LightShadowLinkCoordinator3DPrepareInfo& prepare_info_,
                                             std::uint64_t resolved_shadow_signature_) const noexcept {
        if (!cache_valid ||
            prepare_info_.allow_incremental_light_patch == 0U ||
            prepare_info_.light_updated_component_indices == nullptr ||
            prepare_info_.light_updated_component_count == 0U) {
            return false;
        }
        if (cache_shadow_signature != resolved_shadow_signature_) {
            return false;
        }
        if (linked_light_records_scratch.size() != prepare_info_.light_record_count ||
            cached_light_record_count != prepare_info_.light_record_count) {
            return false;
        }
        if (prepare_info_.light_updated_component_count >= prepare_info_.light_record_count) {
            return false;
        }
        const std::uint64_t updated_count = static_cast<std::uint64_t>(prepare_info_.light_updated_component_count);
        const std::uint64_t light_count = static_cast<std::uint64_t>(prepare_info_.light_record_count);
        return (updated_count * 4U) < (light_count * 3U);
    }

    [[nodiscard]] std::uint32_t TryIncrementalLightPatch(
        const LightShadowLinkCoordinator3DPrepareInfo& prepare_info_,
        std::uint64_t resolved_shadow_signature_) {
        if (!CanUseIncrementalPath(prepare_info_, resolved_shadow_signature_)) {
            return 0U;
        }

        std::uint32_t patched_count = 0U;
        for (std::uint32_t i = 0U; i < prepare_info_.light_updated_component_count; ++i) {
            const std::uint32_t light_index = prepare_info_.light_updated_component_indices[i];
            if (light_index >= prepare_info_.light_record_count) {
                continue;
            }

            ecs::LightGpuRecord3D patched_record = prepare_info_.light_records[light_index];
            const ecs::LightGpuRecord3D& linked_record = linked_light_records_scratch[light_index];
            patched_record.shadow_view_begin = linked_record.shadow_view_begin;
            patched_record.shadow_meta = linked_record.shadow_meta;
            patched_record.shadow_namespace_id = linked_record.shadow_namespace_id;
            linked_light_records_scratch[light_index] = patched_record;
            ++patched_count;
        }
        return patched_count;
    }

private:
    LightShadowLinkCoordinatorMcVector<ecs::LightGpuRecord3D> linked_light_records_scratch{};

    std::uint64_t cache_signature = 0U;
    std::uint64_t cache_shadow_signature = 0U;
    std::uint32_t cached_light_record_count = 0U;
    std::uint32_t cached_shadow_namespace_id = 0U;
    std::uint32_t cached_linked_light_count = 0U;
    std::uint32_t cached_namespace_drop_count = 0U;
    std::uint32_t cached_unmapped_light_count = 0U;
    bool cache_valid = false;

    LightShadowLinkCoordinator3DStats stats{};
};

} // namespace vr::render

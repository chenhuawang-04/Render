#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/system/light_gpu_layout.hpp"
#include "vr/ecs/system/shadow_gpu_layout.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace vr::render {

template<typename T>
using LightShadowLinkStageMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct LightShadowLinkStageResult2D final {
    const ecs::LightGpuRecord2D* linked_light_records = nullptr;
    std::uint32_t linked_light_record_count = 0U;
    std::uint32_t shadow_namespace_id = 0U;
    std::uint32_t linked_light_count = 0U;
    std::uint32_t namespace_drop_count = 0U;
    std::uint32_t unmapped_light_count = 0U;
};

struct LightShadowLinkStageResult3D final {
    const ecs::LightGpuRecord3D* linked_light_records = nullptr;
    std::uint32_t linked_light_record_count = 0U;
    std::uint32_t shadow_namespace_id = 0U;
    std::uint32_t linked_light_count = 0U;
    std::uint32_t namespace_drop_count = 0U;
    std::uint32_t unmapped_light_count = 0U;
};

class LightShadowLinkStage final {
public:
    static constexpr std::uint32_t invalid_shadow_view_begin = (std::numeric_limits<std::uint32_t>::max)();

    [[nodiscard]] static LightShadowLinkStageResult2D BuildLinkedLightRecords2D(
        const ecs::LightGpuRecord2D* light_records_,
        std::uint32_t light_record_count_,
        const ecs::Shadow<ecs::Dim2>* shadow_components_,
        std::uint32_t shadow_component_count_,
        const ecs::ShadowGpuRecord2D* shadow_records_,
        std::uint32_t shadow_record_count_,
        std::uint32_t shadow_namespace_hint_,
        LightShadowLinkStageMcVector<ecs::LightGpuRecord2D>& linked_light_records_scratch_) {
        LightShadowLinkStageResult2D result{};
        result.shadow_namespace_id = shadow_namespace_hint_;

        linked_light_records_scratch_.clear();
        if (light_records_ == nullptr || light_record_count_ == 0U) {
            return result;
        }

        linked_light_records_scratch_.resize(light_record_count_);
        for (std::uint32_t light_index = 0U; light_index < light_record_count_; ++light_index) {
            ecs::LightGpuRecord2D record = light_records_[light_index];
            record.shadow_view_begin = invalid_shadow_view_begin;
            record.shadow_meta = 0U;
            record.shadow_namespace_id = 0U;
            record.reserved0 = 0U;
            linked_light_records_scratch_[light_index] = record;
        }
        result.linked_light_records = linked_light_records_scratch_.data();
        result.linked_light_record_count = light_record_count_;
        result.unmapped_light_count = light_record_count_;

        if (shadow_components_ == nullptr ||
            shadow_records_ == nullptr ||
            shadow_component_count_ == 0U ||
            shadow_record_count_ == 0U) {
            return result;
        }

        const std::uint32_t shadow_count = std::min(shadow_component_count_, shadow_record_count_);
        for (std::uint32_t shadow_index = 0U; shadow_index < shadow_count; ++shadow_index) {
            const ecs::Shadow<ecs::Dim2>& shadow_component = shadow_components_[shadow_index];
            if (shadow_component.visibility.enabled == 0U || shadow_component.visibility.visible == 0U) {
                continue;
            }

            const std::uint32_t light_index = shadow_component.binding.light_component_index;
            if (light_index >= light_record_count_) {
                continue;
            }

            const ecs::ShadowGpuRecord2D& shadow_record = shadow_records_[shadow_index];
            if (shadow_record.view_count == 0U) {
                continue;
            }

            if (result.shadow_namespace_id == 0U) {
                result.shadow_namespace_id = shadow_component.binding.atlas_namespace_id;
            }
            if (shadow_component.binding.atlas_namespace_id != result.shadow_namespace_id) {
                ++result.namespace_drop_count;
                continue;
            }

            ecs::LightGpuRecord2D& light_record = linked_light_records_scratch_[light_index];
            if (light_record.shadow_view_begin != invalid_shadow_view_begin) {
                continue;
            }

            light_record.shadow_view_begin = shadow_record.first_view_index;
            light_record.shadow_meta =
                (shadow_record.view_count & 0xFFFFU) |
                ((shadow_record.projection_kind & 0xFFU) << 16U);
            light_record.shadow_namespace_id = shadow_component.binding.atlas_namespace_id;
            ++result.linked_light_count;
        }

        if (result.linked_light_count <= result.unmapped_light_count) {
            result.unmapped_light_count -= result.linked_light_count;
        } else {
            result.unmapped_light_count = 0U;
        }
        return result;
    }

    [[nodiscard]] static LightShadowLinkStageResult3D BuildLinkedLightRecords3D(
        const ecs::LightGpuRecord3D* light_records_,
        std::uint32_t light_record_count_,
        const ecs::Shadow<ecs::Dim3>* shadow_components_,
        std::uint32_t shadow_component_count_,
        const ecs::ShadowGpuRecord3D* shadow_records_,
        std::uint32_t shadow_record_count_,
        std::uint32_t shadow_namespace_hint_,
        LightShadowLinkStageMcVector<ecs::LightGpuRecord3D>& linked_light_records_scratch_) {
        LightShadowLinkStageResult3D result{};
        result.shadow_namespace_id = shadow_namespace_hint_;

        linked_light_records_scratch_.clear();
        if (light_records_ == nullptr || light_record_count_ == 0U) {
            return result;
        }

        linked_light_records_scratch_.resize(light_record_count_);
        for (std::uint32_t light_index = 0U; light_index < light_record_count_; ++light_index) {
            ecs::LightGpuRecord3D record = light_records_[light_index];
            record.reserved0 = invalid_shadow_view_begin;
            record.reserved1 = 0U;
            record.reserved2 = 0U;
            linked_light_records_scratch_[light_index] = record;
        }
        result.linked_light_records = linked_light_records_scratch_.data();
        result.linked_light_record_count = light_record_count_;
        result.unmapped_light_count = light_record_count_;

        if (shadow_components_ == nullptr ||
            shadow_records_ == nullptr ||
            shadow_component_count_ == 0U ||
            shadow_record_count_ == 0U) {
            return result;
        }

        const std::uint32_t shadow_count = std::min(shadow_component_count_, shadow_record_count_);
        for (std::uint32_t shadow_index = 0U; shadow_index < shadow_count; ++shadow_index) {
            const ecs::Shadow<ecs::Dim3>& shadow_component = shadow_components_[shadow_index];
            if (shadow_component.visibility.enabled == 0U || shadow_component.visibility.visible == 0U) {
                continue;
            }

            const std::uint32_t light_index = shadow_component.binding.light_component_index;
            if (light_index >= light_record_count_) {
                continue;
            }

            const ecs::ShadowGpuRecord3D& shadow_record = shadow_records_[shadow_index];
            if (shadow_record.view_count == 0U) {
                continue;
            }

            if (result.shadow_namespace_id == 0U) {
                result.shadow_namespace_id = shadow_component.binding.atlas_namespace_id;
            }
            if (shadow_component.binding.atlas_namespace_id != result.shadow_namespace_id) {
                ++result.namespace_drop_count;
                continue;
            }

            ecs::LightGpuRecord3D& light_record = linked_light_records_scratch_[light_index];
            if (light_record.reserved0 != invalid_shadow_view_begin) {
                continue;
            }

            light_record.reserved0 = shadow_record.first_view_index;
            light_record.reserved1 =
                (shadow_record.view_count & 0xFFFFU) |
                ((shadow_record.projection_kind & 0xFFU) << 16U);
            light_record.reserved2 = shadow_component.binding.atlas_namespace_id;
            ++result.linked_light_count;
        }

        if (result.linked_light_count <= result.unmapped_light_count) {
            result.unmapped_light_count -= result.linked_light_count;
        } else {
            result.unmapped_light_count = 0U;
        }
        return result;
    }
};

} // namespace vr::render

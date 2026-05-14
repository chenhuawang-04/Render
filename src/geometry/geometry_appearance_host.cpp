#include "vr/geometry/geometry_appearance_host.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace vr::geometry {

namespace {

[[nodiscard]] GeometryAppearanceDesc NormalizeAppearanceDesc(const GeometryAppearanceDesc& desc_) noexcept {
    GeometryAppearanceDesc normalized = desc_;
    const auto finite_or = [](float value_, float fallback_) noexcept {
        return std::isfinite(value_) ? value_ : fallback_;
    };

    normalized.uv_scale_u = finite_or(normalized.uv_scale_u, 1.0F);
    normalized.uv_scale_v = finite_or(normalized.uv_scale_v, 1.0F);
    normalized.uv_bias_u = finite_or(normalized.uv_bias_u, 0.0F);
    normalized.uv_bias_v = finite_or(normalized.uv_bias_v, 0.0F);
    normalized.alpha_cutoff = std::clamp(finite_or(normalized.alpha_cutoff, 0.0F), 0.0F, 1.0F);
    normalized.metallic_factor = std::clamp(finite_or(normalized.metallic_factor, 0.0F), 0.0F, 1.0F);
    normalized.roughness_factor = std::clamp(finite_or(normalized.roughness_factor, 1.0F), 0.04F, 1.0F);
    normalized.normal_scale = std::clamp(finite_or(normalized.normal_scale, 1.0F), 0.0F, 4.0F);
    normalized.occlusion_strength =
        std::clamp(finite_or(normalized.occlusion_strength, 1.0F), 0.0F, 1.0F);
    return normalized;
}

} // namespace

void GeometryAppearanceHost::Initialize(const GeometryAppearanceHostCreateInfo& create_info_) {
    create_info_cache = create_info_;
    appearances.clear();
    stats = {};

    if (create_info_cache.reserve_appearance_count > 0U) {
        appearances.reserve(create_info_cache.reserve_appearance_count);
    }
    initialized = true;
}

void GeometryAppearanceHost::Shutdown() noexcept {
    appearances.clear();
    create_info_cache = {};
    stats = {};
    initialized = false;
}

void GeometryAppearanceHost::UpsertAppearance(const GeometryAppearanceDesc& desc_) {
    if (!initialized) {
        throw std::runtime_error("GeometryAppearanceHost::UpsertAppearance called before Initialize");
    }
    if (desc_.appearance_id == 0U) {
        throw std::invalid_argument("GeometryAppearanceHost::UpsertAppearance appearance_id must be non-zero");
    }
    const GeometryAppearanceDesc normalized_desc = NormalizeAppearanceDesc(desc_);

    const std::size_t lower_bound_index = LowerBoundAppearanceIndex(normalized_desc.appearance_id);
    const bool exists = lower_bound_index < appearances.size() &&
                        appearances[lower_bound_index].desc.appearance_id == normalized_desc.appearance_id;
    if (exists) {
        AppearanceRecord& record = appearances[lower_bound_index];
        record.desc = normalized_desc;
        ++record.revision;
        ++stats.updated_appearance_count;
    } else {
        const std::size_t old_size = appearances.size();
        appearances.resize(old_size + 1U);
        for (std::size_t index = old_size; index > lower_bound_index; --index) {
            appearances[index] = std::move(appearances[index - 1U]);
        }
        AppearanceRecord record{};
        record.desc = normalized_desc;
        record.revision = 1U;
        appearances[lower_bound_index] = record;
        ++stats.added_appearance_count;
    }

    stats.appearance_count = static_cast<std::uint32_t>(appearances.size());
    ++stats.revision;
}

bool GeometryAppearanceHost::RemoveAppearance(std::uint32_t appearance_id_) noexcept {
    if (!initialized || appearance_id_ == 0U) {
        return false;
    }

    const std::size_t lower_bound_index = LowerBoundAppearanceIndex(appearance_id_);
    if (lower_bound_index >= appearances.size() ||
        appearances[lower_bound_index].desc.appearance_id != appearance_id_) {
        return false;
    }

    appearances.erase(appearances.begin() + static_cast<std::ptrdiff_t>(lower_bound_index));
    ++stats.removed_appearance_count;
    stats.appearance_count = static_cast<std::uint32_t>(appearances.size());
    ++stats.revision;
    return true;
}

const GeometryAppearanceHost::AppearanceRecord* GeometryAppearanceHost::FindAppearance(std::uint32_t appearance_id_) const noexcept {
    if (!initialized || appearance_id_ == 0U) {
        return nullptr;
    }

    const std::size_t lower_bound_index = LowerBoundAppearanceIndex(appearance_id_);
    if (lower_bound_index >= appearances.size()) {
        return nullptr;
    }
    const AppearanceRecord& record = appearances[lower_bound_index];
    if (record.desc.appearance_id != appearance_id_) {
        return nullptr;
    }
    return &record;
}

bool GeometryAppearanceHost::IsInitialized() const noexcept {
    return initialized;
}

const GeometryAppearanceHostStats& GeometryAppearanceHost::Stats() const noexcept {
    return stats;
}

std::size_t GeometryAppearanceHost::LowerBoundAppearanceIndex(std::uint32_t appearance_id_) const noexcept {
    std::size_t first = 0U;
    std::size_t count = appearances.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (appearances[it].desc.appearance_id < appearance_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

} // namespace vr::geometry


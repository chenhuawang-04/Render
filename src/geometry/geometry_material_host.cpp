#include "vr/geometry/geometry_material_host.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace vr::geometry {

void GeometryMaterialHost::Initialize(const GeometryMaterialHostCreateInfo& create_info_) {
    create_info_cache = create_info_;
    materials.clear();
    stats = {};

    if (create_info_cache.reserve_material_count > 0U) {
        materials.reserve(create_info_cache.reserve_material_count);
    }
    initialized = true;
}

void GeometryMaterialHost::Shutdown() noexcept {
    materials.clear();
    create_info_cache = {};
    stats = {};
    initialized = false;
}

void GeometryMaterialHost::UpsertMaterial(const GeometryMaterialDesc& desc_) {
    if (!initialized) {
        throw std::runtime_error("GeometryMaterialHost::UpsertMaterial called before Initialize");
    }
    if (desc_.material_id == 0U) {
        throw std::invalid_argument("GeometryMaterialHost::UpsertMaterial material_id must be non-zero");
    }

    const std::size_t lower_bound_index = LowerBoundMaterialIndex(desc_.material_id);
    const bool exists = lower_bound_index < materials.size() &&
                        materials[lower_bound_index].desc.material_id == desc_.material_id;
    if (exists) {
        MaterialRecord& record = materials[lower_bound_index];
        record.desc = desc_;
        ++record.revision;
        ++stats.updated_material_count;
    } else {
        const std::size_t old_size = materials.size();
        materials.resize(old_size + 1U);
        for (std::size_t index = old_size; index > lower_bound_index; --index) {
            materials[index] = std::move(materials[index - 1U]);
        }
        MaterialRecord record{};
        record.desc = desc_;
        record.revision = 1U;
        materials[lower_bound_index] = record;
        ++stats.added_material_count;
    }

    stats.material_count = static_cast<std::uint32_t>(materials.size());
}

bool GeometryMaterialHost::RemoveMaterial(std::uint32_t material_id_) noexcept {
    if (!initialized || material_id_ == 0U) {
        return false;
    }

    const std::size_t lower_bound_index = LowerBoundMaterialIndex(material_id_);
    if (lower_bound_index >= materials.size() ||
        materials[lower_bound_index].desc.material_id != material_id_) {
        return false;
    }

    materials.erase(materials.begin() + static_cast<std::ptrdiff_t>(lower_bound_index));
    ++stats.removed_material_count;
    stats.material_count = static_cast<std::uint32_t>(materials.size());
    return true;
}

const GeometryMaterialHost::MaterialRecord* GeometryMaterialHost::FindMaterial(std::uint32_t material_id_) const noexcept {
    if (!initialized || material_id_ == 0U) {
        return nullptr;
    }

    const std::size_t lower_bound_index = LowerBoundMaterialIndex(material_id_);
    if (lower_bound_index >= materials.size()) {
        return nullptr;
    }
    const MaterialRecord& record = materials[lower_bound_index];
    if (record.desc.material_id != material_id_) {
        return nullptr;
    }
    return &record;
}

bool GeometryMaterialHost::IsInitialized() const noexcept {
    return initialized;
}

const GeometryMaterialHostStats& GeometryMaterialHost::Stats() const noexcept {
    return stats;
}

std::size_t GeometryMaterialHost::LowerBoundMaterialIndex(std::uint32_t material_id_) const noexcept {
    std::size_t first = 0U;
    std::size_t count = materials.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (materials[it].desc.material_id < material_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

} // namespace vr::geometry


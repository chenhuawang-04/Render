#include "vr/animation/animation_path_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vr::animation {
namespace {

constexpr std::uint32_t k_invalid_record_index = (std::numeric_limits<std::uint32_t>::max)();

template<typename T>
void ReserveIfNeeded(AnimationPathMcVector<T>& storage_, std::uint32_t target_) {
    if (target_ > 0U && storage_.capacity() < static_cast<std::size_t>(target_)) {
        storage_.reserve(target_);
    }
}

template<typename T>
void InsertVectorValue(AnimationPathMcVector<T>& storage_,
                       std::size_t index_,
                       const T& value_) {
    const std::size_t old_size = storage_.size();
    storage_.resize(old_size + 1U);
    for (std::size_t i = old_size; i > index_; --i) {
        storage_[i] = std::move(storage_[i - 1U]);
    }
    storage_[index_] = value_;
}

template<typename T>
void EraseVectorValue(AnimationPathMcVector<T>& storage_,
                      std::size_t index_) noexcept {
    const std::size_t old_size = storage_.size();
    for (std::size_t i = index_; i + 1U < old_size; ++i) {
        storage_[i] = std::move(storage_[i + 1U]);
    }
    storage_.resize(old_size - 1U);
}

} // namespace

void AnimationPathHost::Initialize(const AnimationPathHostCreateInfo& create_info_) {
    create_info_cache = create_info_;
    stats = {};
    paths.clear();
    lookup.clear();
    slots.clear();
    free_slot_indices.clear();
    segments_2d.clear();
    segments_3d.clear();

    ReserveIfNeeded(paths, create_info_cache.reserve_path_count);
    ReserveIfNeeded(lookup, create_info_cache.reserve_path_count);
    ReserveIfNeeded(slots, create_info_cache.reserve_path_count);
    ReserveIfNeeded(free_slot_indices, create_info_cache.reserve_path_count);
    ReserveIfNeeded(segments_2d, create_info_cache.reserve_segment_count);
    ReserveIfNeeded(segments_3d, create_info_cache.reserve_segment_count);

    initialized = true;
}

void AnimationPathHost::Shutdown() noexcept {
    paths.clear();
    lookup.clear();
    slots.clear();
    free_slot_indices.clear();
    segments_2d.clear();
    segments_3d.clear();
    create_info_cache = {};
    stats = {};
    initialized = false;
}

ecs::AnimationPathHandle AnimationPathHost::UpsertPath(const AnimationPath2DDesc& desc_) {
    if (!initialized) {
        throw std::runtime_error("AnimationPathHost::UpsertPath(2D) called before Initialize");
    }
    if (desc_.path_id == 0U) {
        throw std::invalid_argument("AnimationPathHost::UpsertPath(2D) path_id must be non-zero");
    }

    const std::size_t lookup_index = LowerBoundLookupIndex(desc_.path_id);
    const bool exists = lookup_index < lookup.size() && lookup[lookup_index].path_id == desc_.path_id;
    AnimationPathRecord* record = nullptr;
    if (exists) {
        const std::uint32_t slot_index = lookup[lookup_index].slot_index;
        if (slot_index >= slots.size() || slots[slot_index].alive == 0U || slots[slot_index].record_index >= paths.size()) {
            throw std::runtime_error("AnimationPathHost::UpsertPath(2D) encountered invalid slot state");
        }
        record = &paths[slots[slot_index].record_index];
        if (record->kind != AnimationPathKind::path2d) {
            throw std::invalid_argument("AnimationPathHost::UpsertPath(2D) path kind mismatch");
        }
        ++record->revision;
        ++stats.updated_path_count;
    } else {
        const ecs::AnimationPathHandle handle = AllocateHandle();
        paths.push_back(AnimationPathRecord{});
        record = &paths.back();
        record->path_id = desc_.path_id;
        record->handle = handle;
        record->kind = AnimationPathKind::path2d;
        record->revision = 1U;

        PathLookupEntry entry{};
        entry.path_id = desc_.path_id;
        entry.slot_index = handle.index;
        InsertVectorValue(lookup, lookup_index, entry);
        slots[handle.index].record_index = static_cast<std::uint32_t>(paths.size() - 1U);

        UpdateStatsByKind(AnimationPathKind::path2d, +1);
        ++stats.added_path_count;
    }

    record->segment_begin = static_cast<std::uint32_t>(segments_2d.size());
    record->segment_count = desc_.segment_count;
    if (desc_.segments != nullptr && desc_.segment_count > 0U) {
        const std::size_t old_size = segments_2d.size();
        segments_2d.resize(old_size + static_cast<std::size_t>(desc_.segment_count));
        for (std::uint32_t i = 0U; i < desc_.segment_count; ++i) {
            segments_2d[old_size + static_cast<std::size_t>(i)] = desc_.segments[i];
        }
    }

    stats.path_count = static_cast<std::uint32_t>(paths.size());
    ++stats.revision;
    return record->handle;
}

ecs::AnimationPathHandle AnimationPathHost::UpsertPath(const AnimationPath3DDesc& desc_) {
    if (!initialized) {
        throw std::runtime_error("AnimationPathHost::UpsertPath(3D) called before Initialize");
    }
    if (desc_.path_id == 0U) {
        throw std::invalid_argument("AnimationPathHost::UpsertPath(3D) path_id must be non-zero");
    }

    const std::size_t lookup_index = LowerBoundLookupIndex(desc_.path_id);
    const bool exists = lookup_index < lookup.size() && lookup[lookup_index].path_id == desc_.path_id;
    AnimationPathRecord* record = nullptr;
    if (exists) {
        const std::uint32_t slot_index = lookup[lookup_index].slot_index;
        if (slot_index >= slots.size() || slots[slot_index].alive == 0U || slots[slot_index].record_index >= paths.size()) {
            throw std::runtime_error("AnimationPathHost::UpsertPath(3D) encountered invalid slot state");
        }
        record = &paths[slots[slot_index].record_index];
        if (record->kind != AnimationPathKind::path3d) {
            throw std::invalid_argument("AnimationPathHost::UpsertPath(3D) path kind mismatch");
        }
        ++record->revision;
        ++stats.updated_path_count;
    } else {
        const ecs::AnimationPathHandle handle = AllocateHandle();
        paths.push_back(AnimationPathRecord{});
        record = &paths.back();
        record->path_id = desc_.path_id;
        record->handle = handle;
        record->kind = AnimationPathKind::path3d;
        record->revision = 1U;

        PathLookupEntry entry{};
        entry.path_id = desc_.path_id;
        entry.slot_index = handle.index;
        InsertVectorValue(lookup, lookup_index, entry);
        slots[handle.index].record_index = static_cast<std::uint32_t>(paths.size() - 1U);

        UpdateStatsByKind(AnimationPathKind::path3d, +1);
        ++stats.added_path_count;
    }

    record->segment_begin = static_cast<std::uint32_t>(segments_3d.size());
    record->segment_count = desc_.segment_count;
    if (desc_.segments != nullptr && desc_.segment_count > 0U) {
        const std::size_t old_size = segments_3d.size();
        segments_3d.resize(old_size + static_cast<std::size_t>(desc_.segment_count));
        for (std::uint32_t i = 0U; i < desc_.segment_count; ++i) {
            segments_3d[old_size + static_cast<std::size_t>(i)] = desc_.segments[i];
        }
    }

    stats.path_count = static_cast<std::uint32_t>(paths.size());
    ++stats.revision;
    return record->handle;
}

bool AnimationPathHost::RemovePath(std::uint32_t path_id_) noexcept {
    if (!initialized || path_id_ == 0U) {
        return false;
    }

    const std::size_t lookup_index = LowerBoundLookupIndex(path_id_);
    if (lookup_index >= lookup.size() || lookup[lookup_index].path_id != path_id_) {
        return false;
    }

    const std::uint32_t slot_index = lookup[lookup_index].slot_index;
    if (slot_index >= slots.size() || slots[slot_index].alive == 0U || slots[slot_index].record_index >= paths.size()) {
        return false;
    }

    const std::uint32_t record_index = slots[slot_index].record_index;
    const AnimationPathKind kind = paths[record_index].kind;
    UpdateStatsByKind(kind, -1);

    const std::uint32_t last_index = static_cast<std::uint32_t>(paths.size() - 1U);
    if (record_index != last_index) {
        paths[record_index] = std::move(paths[last_index]);
        UpdateMovedRecordSlot(record_index);
    }
    paths.pop_back();

    slots[slot_index].alive = 0U;
    ++slots[slot_index].generation;
    slots[slot_index].record_index = k_invalid_record_index;
    free_slot_indices.push_back(slot_index);
    RemoveLookupIndex(lookup_index);

    ++stats.removed_path_count;
    stats.path_count = static_cast<std::uint32_t>(paths.size());
    ++stats.revision;
    return true;
}

const AnimationPathRecord* AnimationPathHost::FindPathById(std::uint32_t path_id_) const noexcept {
    if (!initialized || path_id_ == 0U) {
        return nullptr;
    }
    const std::size_t lookup_index = LowerBoundLookupIndex(path_id_);
    if (lookup_index >= lookup.size() || lookup[lookup_index].path_id != path_id_) {
        return nullptr;
    }
    const std::uint32_t slot_index = lookup[lookup_index].slot_index;
    if (slot_index >= slots.size()) {
        return nullptr;
    }
    const PathSlot& slot = slots[slot_index];
    if (slot.alive == 0U || slot.record_index >= paths.size()) {
        return nullptr;
    }
    return &paths[slot.record_index];
}

const AnimationPathRecord* AnimationPathHost::FindPathByHandle(ecs::AnimationPathHandle handle_) const noexcept {
    if (!initialized || handle_.index == ecs::invalid_animation_handle_index || handle_.index >= slots.size()) {
        return nullptr;
    }
    const PathSlot& slot = slots[handle_.index];
    if (slot.alive == 0U || slot.generation != handle_.generation || slot.record_index >= paths.size()) {
        return nullptr;
    }
    return &paths[slot.record_index];
}

const AnimationPathRecord* AnimationPathHost::FindPath2DById(std::uint32_t path_id_) const noexcept {
    const AnimationPathRecord* record = FindPathById(path_id_);
    return (record != nullptr && record->kind == AnimationPathKind::path2d) ? record : nullptr;
}

const AnimationPathRecord* AnimationPathHost::FindPath2DByHandle(ecs::AnimationPathHandle handle_) const noexcept {
    const AnimationPathRecord* record = FindPathByHandle(handle_);
    return (record != nullptr && record->kind == AnimationPathKind::path2d) ? record : nullptr;
}

const AnimationPathRecord* AnimationPathHost::FindPath3DById(std::uint32_t path_id_) const noexcept {
    const AnimationPathRecord* record = FindPathById(path_id_);
    return (record != nullptr && record->kind == AnimationPathKind::path3d) ? record : nullptr;
}

const AnimationPathRecord* AnimationPathHost::FindPath3DByHandle(ecs::AnimationPathHandle handle_) const noexcept {
    const AnimationPathRecord* record = FindPathByHandle(handle_);
    return (record != nullptr && record->kind == AnimationPathKind::path3d) ? record : nullptr;
}

ecs::AnimationSplineView<ecs::Dim2> AnimationPathHost::BuildSplineView2D(const AnimationPathRecord& path_) const noexcept {
    if (path_.kind != AnimationPathKind::path2d || path_.segment_count == 0U || path_.segment_begin >= segments_2d.size()) {
        return ecs::AnimationSplineView<ecs::Dim2>{
            .segments = nullptr,
            .segment_count = 0U,
        };
    }
    return ecs::AnimationSplineView<ecs::Dim2>{
        .segments = segments_2d.data() + path_.segment_begin,
        .segment_count = path_.segment_count,
    };
}

ecs::AnimationSplineView<ecs::Dim3> AnimationPathHost::BuildSplineView3D(const AnimationPathRecord& path_) const noexcept {
    if (path_.kind != AnimationPathKind::path3d || path_.segment_count == 0U || path_.segment_begin >= segments_3d.size()) {
        return ecs::AnimationSplineView<ecs::Dim3>{
            .segments = nullptr,
            .segment_count = 0U,
        };
    }
    return ecs::AnimationSplineView<ecs::Dim3>{
        .segments = segments_3d.data() + path_.segment_begin,
        .segment_count = path_.segment_count,
    };
}

bool AnimationPathHost::IsInitialized() const noexcept {
    return initialized;
}

const AnimationPathHostStats& AnimationPathHost::Stats() const noexcept {
    return stats;
}

std::size_t AnimationPathHost::LowerBoundLookupIndex(std::uint32_t path_id_) const noexcept {
    std::size_t first = 0U;
    std::size_t count = lookup.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (lookup[it].path_id < path_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

ecs::AnimationPathHandle AnimationPathHost::AllocateHandle() {
    if (!free_slot_indices.empty()) {
        const std::uint32_t slot_index = free_slot_indices.back();
        free_slot_indices.pop_back();
        PathSlot& slot = slots[slot_index];
        slot.alive = 1U;
        slot.record_index = k_invalid_record_index;
        return ecs::AnimationPathHandle{
            .index = slot_index,
            .generation = slot.generation,
        };
    }

    const std::uint32_t slot_index = static_cast<std::uint32_t>(slots.size());
    PathSlot slot{};
    slot.generation = 1U;
    slot.record_index = k_invalid_record_index;
    slot.alive = 1U;
    slots.push_back(slot);
    return ecs::AnimationPathHandle{
        .index = slot_index,
        .generation = 1U,
    };
}

void AnimationPathHost::UpdateStatsByKind(AnimationPathKind kind_, int delta_) noexcept {
    auto apply = [delta_](std::uint32_t& value_) noexcept {
        value_ = static_cast<std::uint32_t>(static_cast<int>(value_) + delta_);
    };
    switch (kind_) {
        case AnimationPathKind::path2d:
            apply(stats.path2d_count);
            break;
        case AnimationPathKind::path3d:
            apply(stats.path3d_count);
            break;
        case AnimationPathKind::none:
        default:
            break;
    }
}

void AnimationPathHost::RemoveLookupIndex(std::size_t lookup_index_) noexcept {
    EraseVectorValue(lookup, lookup_index_);
}

void AnimationPathHost::UpdateMovedRecordSlot(std::uint32_t record_index_) noexcept {
    if (record_index_ >= paths.size()) {
        return;
    }
    const std::uint32_t slot_index = paths[record_index_].handle.index;
    if (slot_index < slots.size()) {
        slots[slot_index].record_index = record_index_;
    }
}

} // namespace vr::animation

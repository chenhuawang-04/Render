#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/animation_component.hpp"
#include "vr/ecs/system/animation_path_motion_system.hpp"

#include <cstdint>

namespace vr::animation {

template<typename T>
using AnimationPathMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

enum class AnimationPathKind : std::uint8_t {
    none = 0U,
    path2d = 1U,
    path3d = 2U,
};

struct AnimationPath2DDesc final {
    std::uint32_t path_id = 0U;
    const ecs::AnimationBezierSegment<ecs::Dim2>* segments = nullptr;
    std::uint32_t segment_count = 0U;
};

struct AnimationPath3DDesc final {
    std::uint32_t path_id = 0U;
    const ecs::AnimationBezierSegment<ecs::Dim3>* segments = nullptr;
    std::uint32_t segment_count = 0U;
};

struct AnimationPathHostCreateInfo final {
    std::uint32_t reserve_path_count = 256U;
    std::uint32_t reserve_segment_count = 2048U;
};

struct AnimationPathHostStats final {
    std::uint32_t path_count = 0U;
    std::uint32_t path2d_count = 0U;
    std::uint32_t path3d_count = 0U;
    std::uint32_t added_path_count = 0U;
    std::uint32_t updated_path_count = 0U;
    std::uint32_t removed_path_count = 0U;
    std::uint32_t revision = 0U;
};

struct AnimationPathRecord final {
    std::uint32_t path_id = 0U;
    ecs::AnimationPathHandle handle = ecs::invalid_animation_path_handle;
    AnimationPathKind kind = AnimationPathKind::none;
    std::uint8_t reserved0 = 0U;
    std::uint16_t reserved1 = 0U;
    std::uint32_t revision = 0U;
    std::uint32_t segment_begin = 0U;
    std::uint32_t segment_count = 0U;
};

class AnimationPathHost final {
public:
    AnimationPathHost() = default;
    ~AnimationPathHost() = default;

    AnimationPathHost(const AnimationPathHost&) = delete;
    AnimationPathHost& operator=(const AnimationPathHost&) = delete;
    AnimationPathHost(AnimationPathHost&&) = delete;
    AnimationPathHost& operator=(AnimationPathHost&&) = delete;

    void Initialize(const AnimationPathHostCreateInfo& create_info_ = {});
    void Shutdown() noexcept;

    ecs::AnimationPathHandle UpsertPath(const AnimationPath2DDesc& desc_);
    ecs::AnimationPathHandle UpsertPath(const AnimationPath3DDesc& desc_);

    [[nodiscard]] bool RemovePath(std::uint32_t path_id_) noexcept;

    [[nodiscard]] const AnimationPathRecord* FindPathById(std::uint32_t path_id_) const noexcept;
    [[nodiscard]] const AnimationPathRecord* FindPathByHandle(ecs::AnimationPathHandle handle_) const noexcept;
    [[nodiscard]] const AnimationPathRecord* FindPath2DById(std::uint32_t path_id_) const noexcept;
    [[nodiscard]] const AnimationPathRecord* FindPath2DByHandle(ecs::AnimationPathHandle handle_) const noexcept;
    [[nodiscard]] const AnimationPathRecord* FindPath3DById(std::uint32_t path_id_) const noexcept;
    [[nodiscard]] const AnimationPathRecord* FindPath3DByHandle(ecs::AnimationPathHandle handle_) const noexcept;

    [[nodiscard]] ecs::AnimationSplineView<ecs::Dim2> BuildSplineView2D(const AnimationPathRecord& path_) const noexcept;
    [[nodiscard]] ecs::AnimationSplineView<ecs::Dim3> BuildSplineView3D(const AnimationPathRecord& path_) const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const AnimationPathHostStats& Stats() const noexcept;

private:
    struct PathLookupEntry final {
        std::uint32_t path_id = 0U;
        std::uint32_t slot_index = 0U;
    };

    struct PathSlot final {
        std::uint32_t generation = 1U;
        std::uint32_t record_index = 0U;
        std::uint8_t alive = 0U;
        std::uint8_t reserved0 = 0U;
        std::uint16_t reserved1 = 0U;
    };

    [[nodiscard]] std::size_t LowerBoundLookupIndex(std::uint32_t path_id_) const noexcept;
    [[nodiscard]] ecs::AnimationPathHandle AllocateHandle();
    void UpdateStatsByKind(AnimationPathKind kind_, int delta_) noexcept;
    void RemoveLookupIndex(std::size_t lookup_index_) noexcept;
    void UpdateMovedRecordSlot(std::uint32_t record_index_) noexcept;

private:
    AnimationPathHostCreateInfo create_info_cache{};
    AnimationPathHostStats stats{};

    AnimationPathMcVector<AnimationPathRecord> paths{};
    AnimationPathMcVector<PathLookupEntry> lookup{};
    AnimationPathMcVector<PathSlot> slots{};
    AnimationPathMcVector<std::uint32_t> free_slot_indices{};
    AnimationPathMcVector<ecs::AnimationBezierSegment<ecs::Dim2>> segments_2d{};
    AnimationPathMcVector<ecs::AnimationBezierSegment<ecs::Dim3>> segments_3d{};

    bool initialized = false;
};

} // namespace vr::animation


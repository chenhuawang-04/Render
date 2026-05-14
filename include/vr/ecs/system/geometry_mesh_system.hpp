#pragma once

#include "vr/ecs/system/geometry_system.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace vr::ecs {

class GeometryMeshSystem final {
public:
    using Geometry3D = Geometry<Dim3>;

    static void Initialize(Geometry3D& component_) noexcept {
        GeometrySystem<Dim3>::Initialize(component_);
    }

    static void SetMeshRoute(Geometry3D& component_,
                             std::uint32_t geometry_id_,
                             std::uint32_t submesh_index_,
                             std::uint16_t lod_index_) noexcept {
        GeometrySystem<Dim3>::SetGeometryId(component_, geometry_id_);
        component_.mesh.submesh_index = submesh_index_;
        component_.mesh.lod_index = lod_index_;
        BumpMeshRevision(component_);
        GeometrySystem<Dim3>::MarkDirty(component_,
                                        geometry_dirty_data_flag |
                                            geometry_dirty_runtime_flag);
    }

    static void SetSubmeshIndex(Geometry3D& component_,
                                std::uint32_t submesh_index_) noexcept {
        component_.mesh.submesh_index = submesh_index_;
        BumpMeshRevision(component_);
        GeometrySystem<Dim3>::MarkDirty(component_,
                                        geometry_dirty_data_flag |
                                            geometry_dirty_runtime_flag);
    }

    static void SetLodIndex(Geometry3D& component_, std::uint16_t lod_index_) noexcept {
        component_.mesh.lod_index = lod_index_;
        BumpMeshRevision(component_);
        GeometrySystem<Dim3>::MarkDirty(component_,
                                        geometry_dirty_data_flag |
                                            geometry_dirty_runtime_flag);
    }

    static void SetMeshFlags(Geometry3D& component_, std::uint16_t flags_) noexcept {
        if (component_.mesh.flags == flags_) {
            return;
        }
        component_.mesh.flags = flags_;
        BumpMeshRevision(component_);
        GeometrySystem<Dim3>::MarkDirty(component_,
                                        geometry_dirty_data_flag |
                                            geometry_dirty_runtime_flag);
    }

    static void EnableVertexDeformShader(Geometry3D& component_,
                                         bool enabled_) noexcept {
        const std::uint16_t next_flags = enabled_
            ? static_cast<std::uint16_t>(component_.mesh.flags | geometry_mesh_vertex_deform_shader_flag)
            : static_cast<std::uint16_t>(component_.mesh.flags & ~geometry_mesh_vertex_deform_shader_flag);
        SetMeshFlags(component_, next_flags);
    }

    static void EnableFrameSequenceSubmeshAnimation(Geometry3D& component_,
                                                    bool enabled_) noexcept {
        const std::uint16_t next_flags = enabled_
            ? static_cast<std::uint16_t>(component_.mesh.flags | geometry_mesh_frame_sequence_submesh_flag)
            : static_cast<std::uint16_t>(component_.mesh.flags & ~geometry_mesh_frame_sequence_submesh_flag);
        SetMeshFlags(component_, next_flags);
    }

    static void EnableSkeletalRootMotion(Geometry3D& component_,
                                         bool enabled_) noexcept {
        const std::uint16_t next_flags = enabled_
            ? static_cast<std::uint16_t>(component_.mesh.flags | geometry_mesh_skeletal_root_motion_flag)
            : static_cast<std::uint16_t>(component_.mesh.flags & ~geometry_mesh_skeletal_root_motion_flag);
        SetMeshFlags(component_, next_flags);
    }

    static void EnableMorphTargets(Geometry3D& component_,
                                   bool enabled_) noexcept {
        const std::uint16_t next_flags = enabled_
            ? static_cast<std::uint16_t>(component_.mesh.flags | geometry_mesh_morph_targets_flag)
            : static_cast<std::uint16_t>(component_.mesh.flags & ~geometry_mesh_morph_targets_flag);
        SetMeshFlags(component_, next_flags);
    }

    static void EnableSkeletalSkinning(Geometry3D& component_,
                                       bool enabled_) noexcept {
        const std::uint16_t next_flags = enabled_
            ? static_cast<std::uint16_t>(component_.mesh.flags | geometry_mesh_skeletal_skinning_flag)
            : static_cast<std::uint16_t>(component_.mesh.flags & ~geometry_mesh_skeletal_skinning_flag);
        SetMeshFlags(component_, next_flags);
    }

    static void SetTopology(Geometry3D& component_,
                            Geometry3DTopology topology_) noexcept {
        component_.style.topology = topology_;
        GeometrySystem<Dim3>::MarkDirty(component_,
                                        geometry_dirty_style_flag |
                                            geometry_dirty_runtime_flag);
    }

    static void SetLineWidth(Geometry3D& component_, float line_width_) noexcept {
        component_.style.line_width = std::max(0.0F, line_width_);
        GeometrySystem<Dim3>::MarkDirty(component_,
                                        geometry_dirty_style_flag |
                                            geometry_dirty_runtime_flag);
    }

    static void SetBounds(Geometry3D& component_,
                          const Float3& bounds_min_,
                          const Float3& bounds_max_) noexcept {
        GeometrySystem<Dim3>::SetBounds(component_, bounds_min_, bounds_max_);
        BumpMeshRevision(component_);
    }

private:
    static void BumpMeshRevision(Geometry3D& component_) noexcept {
        if (component_.runtime.mesh_revision == std::numeric_limits<std::uint32_t>::max()) {
            component_.runtime.mesh_revision = 1U;
            return;
        }
        ++component_.runtime.mesh_revision;
        if (component_.runtime.mesh_revision == 0U) {
            component_.runtime.mesh_revision = 1U;
        }
    }
};

} // namespace vr::ecs


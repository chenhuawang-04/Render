#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/spatial_types.hpp"

#include <cstdint>
#include <type_traits>
#include <vulkan/vulkan.h>

namespace vr::geometry {

template<typename T>
using GeometryMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct GeometryMeshVertex final {
    float position_x;
    float position_y;
    float position_z;

    float normal_x;
    float normal_y;
    float normal_z;

    float uv_u;
    float uv_v;

    float morph0_position_delta_x;
    float morph0_position_delta_y;
    float morph0_position_delta_z;

    float morph0_normal_delta_x;
    float morph0_normal_delta_y;
    float morph0_normal_delta_z;

    float morph1_position_delta_x;
    float morph1_position_delta_y;
    float morph1_position_delta_z;

    float morph1_normal_delta_x;
    float morph1_normal_delta_y;
    float morph1_normal_delta_z;

    std::uint32_t joint_index0;
    std::uint32_t joint_index1;
    std::uint32_t joint_index2;
    std::uint32_t joint_index3;

    float joint_weight0;
    float joint_weight1;
    float joint_weight2;
    float joint_weight3;
};

struct GeometrySubmeshRange final {
    std::uint32_t first_index;
    std::uint32_t index_count;
    std::int32_t vertex_offset;
    std::uint32_t reserved0;
};

struct GeometryMeshUploadInfo final {
    std::uint32_t geometry_id = 0U;
    const GeometryMeshVertex* vertices = nullptr;
    std::uint32_t vertex_count = 0U;
    const std::uint32_t* indices = nullptr;
    std::uint32_t index_count = 0U;
    const GeometrySubmeshRange* submeshes = nullptr;
    std::uint32_t submesh_count = 0U;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ecs::Float3 bounds_min{.x = 0.0F, .y = 0.0F, .z = 0.0F};
    ecs::Float3 bounds_max{.x = 0.0F, .y = 0.0F, .z = 0.0F};
};

struct GeometryUploadRange final {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0U;
    VkDeviceSize size_bytes = 0U;
    std::uint32_t element_count = 0U;
    std::uint64_t uploaded_revision = 0U;
    bool uploaded = false;
};

struct GeometrySkeletalComponentGpu final {
    std::uint32_t matrix_offset;
    std::uint32_t joint_count;
    std::uint32_t flags;
    std::uint32_t reserved0;
};

struct GeometrySkeletalMatrixGpu final {
    ecs::Matrix4x4 matrix;
};

static_assert(std::is_standard_layout_v<GeometryMeshVertex> && std::is_trivial_v<GeometryMeshVertex>);
static_assert(std::is_standard_layout_v<GeometrySubmeshRange> && std::is_trivial_v<GeometrySubmeshRange>);
static_assert(std::is_standard_layout_v<GeometrySkeletalComponentGpu> &&
              std::is_trivial_v<GeometrySkeletalComponentGpu>);
static_assert(std::is_standard_layout_v<GeometrySkeletalMatrixGpu> &&
              std::is_trivial_v<GeometrySkeletalMatrixGpu>);

} // namespace vr::geometry

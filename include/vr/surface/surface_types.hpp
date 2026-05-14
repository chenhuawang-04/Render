#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"

#include <cstdint>
#include <type_traits>
#include <vulkan/vulkan.h>

namespace vr::surface {

template<typename T>
using SurfaceMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct SurfaceUploadPatch final {
    std::uint32_t instance_begin;
    std::uint32_t instance_count;
};

struct SurfaceUploadRange final {
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceSize size_bytes;
    std::uint32_t element_count;
    std::uint64_t uploaded_revision;
    std::uint32_t patch_count;
    bool uploaded;
    bool partial;
};

static_assert(std::is_standard_layout_v<SurfaceUploadPatch>);
static_assert(std::is_trivial_v<SurfaceUploadPatch>);
static_assert(std::is_standard_layout_v<SurfaceUploadRange>);
static_assert(std::is_trivial_v<SurfaceUploadRange>);

} // namespace vr::surface


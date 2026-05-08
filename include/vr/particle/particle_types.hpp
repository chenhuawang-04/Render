#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"

#include <cstdint>
#include <type_traits>
#include <vulkan/vulkan.h>

namespace vr::particle {

template<typename T>
using ParticleMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct ParticleUploadRange final {
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceSize size_bytes;
    std::uint32_t element_count;
    std::uint64_t uploaded_revision;
    bool uploaded;
};

static_assert(std::is_standard_layout_v<ParticleUploadRange>);
static_assert(std::is_trivial_v<ParticleUploadRange>);

} // namespace vr::particle

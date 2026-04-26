#include "Center/Memory/Vulkan/Types.hpp"
#include "support/test_framework.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

namespace VulkanTypes = Center::Memory::Vulkan;

VR_TEST_CASE(VulkanTypes_align_up_checked_handles_basic_inputs, "unit;core;memory") {
    VR_CHECK(VulkanTypes::align_up_checked(0U, 1U) == 0U);
    VR_CHECK(VulkanTypes::align_up_checked(1U, 1U) == 1U);
    VR_CHECK(VulkanTypes::align_up_checked(1U, 2U) == 2U);
    VR_CHECK(VulkanTypes::align_up_checked(63U, 64U) == 64U);
    VR_CHECK(VulkanTypes::align_up_checked(64U, 64U) == 64U);
    VR_CHECK(VulkanTypes::align_up_checked(65U, 64U) == 128U);
}

VR_TEST_CASE(VulkanTypes_align_up_checked_rejects_invalid_alignment_and_overflow, "unit;core;memory") {
    constexpr std::size_t max_size = std::numeric_limits<std::size_t>::max();
    VR_CHECK(VulkanTypes::align_up_checked(7U, 0U) == 0U);
    VR_CHECK(VulkanTypes::align_up_checked(7U, 3U) == 0U);
    VR_CHECK(VulkanTypes::align_up_checked(7U, 10U) == 0U);
    VR_CHECK(VulkanTypes::align_up_checked(max_size, 2U) == 0U);
}

VR_TEST_CASE(VulkanTypes_checked_add_detects_overflow, "unit;core;memory") {
    constexpr std::size_t max_size = std::numeric_limits<std::size_t>::max();
    VR_CHECK(VulkanTypes::checked_add(0U, 0U) == 0U);
    VR_CHECK(VulkanTypes::checked_add(1U, 2U) == 3U);
    VR_CHECK(VulkanTypes::checked_add(max_size - 1U, 1U) == max_size);
    VR_CHECK(VulkanTypes::checked_add(max_size, 1U) == 0U);
    VR_CHECK(VulkanTypes::checked_add(max_size - 10U, 20U) == 0U);
}

VR_TEST_CASE(VulkanTypes_requires_granularity_separation_matches_expected_matrix, "unit;core;memory") {
    using AllocationKind = VulkanTypes::AllocationKind;

    VR_CHECK(!VulkanTypes::requires_granularity_separation(AllocationKind::unknown, AllocationKind::buffer));
    VR_CHECK(!VulkanTypes::requires_granularity_separation(AllocationKind::buffer, AllocationKind::unknown));
    VR_CHECK(!VulkanTypes::requires_granularity_separation(AllocationKind::buffer, AllocationKind::buffer));
    VR_CHECK(!VulkanTypes::requires_granularity_separation(AllocationKind::image_optimal, AllocationKind::image_optimal));
    VR_CHECK(VulkanTypes::requires_granularity_separation(AllocationKind::buffer, AllocationKind::image_optimal));
    VR_CHECK(VulkanTypes::requires_granularity_separation(AllocationKind::image_optimal, AllocationKind::buffer));
    VR_CHECK(VulkanTypes::requires_granularity_separation(AllocationKind::image_linear, AllocationKind::image_optimal));
}

VR_TEST_CASE(VulkanTypes_to_allocation_request_preserves_all_fields, "unit;core;memory") {
    VulkanTypes::ResourceRequirements requirements{};
    requirements.resource = 11U;
    requirements.size = 2048U;
    requirements.alignment = 256U;
    requirements.selector.memory_type_bits = 0x5U;
    requirements.selector.required_flags = 0x9U;
    requirements.selector.preferred_flags = 0xCU;
    requirements.persistent_map = true;
    requirements.dedicated_required = false;
    requirements.dedicated_preferred = true;
    requirements.allocation_kind = VulkanTypes::AllocationKind::image_optimal;
    requirements.lifetime_hint = VulkanTypes::LifetimeHint::long_lived;
    requirements.host_access = VulkanTypes::HostAccess::random_read_write;
    requirements.allocation_flags = VulkanTypes::allocation_flag_exportable;
    requirements.buffer_image_granularity = 512U;

    const VulkanTypes::AllocationRequest request = VulkanTypes::to_allocation_request(requirements);
    VR_CHECK(request.resource == requirements.resource);
    VR_CHECK(request.size == requirements.size);
    VR_CHECK(request.alignment == requirements.alignment);
    VR_CHECK(request.selector.memory_type_bits == requirements.selector.memory_type_bits);
    VR_CHECK(request.selector.required_flags == requirements.selector.required_flags);
    VR_CHECK(request.selector.preferred_flags == requirements.selector.preferred_flags);
    VR_CHECK(request.persistent_map == requirements.persistent_map);
    VR_CHECK(request.dedicated_required == requirements.dedicated_required);
    VR_CHECK(request.dedicated_preferred == requirements.dedicated_preferred);
    VR_CHECK(request.allocation_kind == requirements.allocation_kind);
    VR_CHECK(request.lifetime_hint == requirements.lifetime_hint);
    VR_CHECK(request.host_access == requirements.host_access);
    VR_CHECK(request.allocation_flags == requirements.allocation_flags);
    VR_CHECK(request.buffer_image_granularity == requirements.buffer_image_granularity);
}

VR_TEST_CASE(VulkanTypes_should_dedicate_allocation_follows_policy_switches, "unit;core;memory") {
    using AllocationKind = VulkanTypes::AllocationKind;
    using HostAccess = VulkanTypes::HostAccess;
    using LifetimeHint = VulkanTypes::LifetimeHint;

    VulkanTypes::AllocationRequest request{};
    request.size = 1024U;
    request.allocation_kind = AllocationKind::buffer;
    request.host_access = HostAccess::none;
    request.lifetime_hint = LifetimeHint::transient;
    request.persistent_map = false;

    VulkanTypes::AllocationPolicy policy = VulkanTypes::AllocationPolicy::throughput_first();
    constexpr std::size_t default_block_size = 64U * 1024U * 1024U;

    request.dedicated_required = true;
    VR_CHECK(VulkanTypes::should_dedicate_allocation(1024U, default_block_size, request, policy));

    request.dedicated_required = false;
    request.dedicated_preferred = false;
    VR_CHECK(!VulkanTypes::should_dedicate_allocation(1024U, default_block_size, request, policy));

    request.dedicated_preferred = true;
    request.persistent_map = true;
    request.host_access = HostAccess::random_read_write;
    VR_CHECK(VulkanTypes::should_dedicate_allocation(1024U, default_block_size, request, policy));

    request.persistent_map = false;
    request.host_access = HostAccess::none;
    request.allocation_kind = AllocationKind::image_optimal;
    request.lifetime_hint = LifetimeHint::long_lived;
    VR_CHECK(VulkanTypes::should_dedicate_allocation(1024U, default_block_size, request, policy));

    request.allocation_kind = AllocationKind::buffer;
    request.lifetime_hint = LifetimeHint::transient;
    request.dedicated_preferred = false;
    constexpr std::size_t over_hard_threshold = default_block_size / 2U + 1U;
    VR_CHECK(VulkanTypes::should_dedicate_allocation(over_hard_threshold, default_block_size, request, policy));
}

} // namespace

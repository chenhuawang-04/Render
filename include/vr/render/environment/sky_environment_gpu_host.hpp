#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/asset/texture_host.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/scene/background/sky_environment.hpp"
#include "vr/vulkan_context.hpp"

#include <array>
#include <cstdint>

namespace vr::render {

template<typename T>
using SkyEnvironmentGpuMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct alignas(16) SkyEnvironmentGpuParams final {
    ecs::Float4 tint_exposure;
    ecs::Float4 rotation_lod_flags;
    ecs::Float4 sky_intensity_ibl_intensity;
    ecs::Float4 horizon_color;
    ecs::Float4 zenith_color;
    ecs::Float4 ground_color;
    std::array<ecs::Float4, 9U> sh9;
};

static_assert((sizeof(SkyEnvironmentGpuParams) % 16U) == 0U);

struct SkyEnvironmentGpuHostCreateInfo final {
    std::uint32_t frames_in_flight = 2U;
    std::uint32_t reserve_environment_count = 16U;
};

struct SkyEnvironmentGpuHostStats final {
    std::uint32_t environment_count = 0U;
    std::uint32_t register_count = 0U;
    std::uint32_t update_count = 0U;
    std::uint32_t cache_hit_count = 0U;
    std::uint32_t prepared_frame_count = 0U;
    std::uint32_t descriptor_update_count = 0U;
    std::uint32_t revision = 0U;
};

class SkyEnvironmentGpuHost final {
public:
    void Initialize(VulkanContext& context_,
                    asset::TextureHost& texture_host_,
                    DescriptorHost& descriptor_host_,
                    resource::SamplerHost& sampler_host_,
                    const SkyEnvironmentGpuHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    [[nodiscard]] scene::SkyEnvironmentGpuHandle RegisterOrUpdate(
        const scene::SkyEnvironmentRenderState& state_,
        const SkyEnvironmentGpuPrepareView& prepare_view_);

    void PrepareFrame(const SkyEnvironmentGpuPrepareView& prepare_view_);

    [[nodiscard]] VkDescriptorSet DescriptorSet(scene::SkyEnvironmentGpuHandle handle_,
                                                std::uint32_t frame_index_) const;

    [[nodiscard]] const SkyEnvironmentGpuParams& Params(scene::SkyEnvironmentGpuHandle handle_) const;
    [[nodiscard]] const SkyEnvironmentGpuHostStats& Stats() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct EnvironmentRecord final {
        scene::SkyEnvironmentGpuHandle handle{};
        scene::SkyEnvironmentRenderState state{};
        SkyEnvironmentGpuParams params{};
    };

    [[nodiscard]] std::size_t FindEnvironmentIndex(const scene::SkyEnvironmentGpuHandle& handle_) const noexcept;
    [[nodiscard]] std::size_t FindEquivalentEnvironmentIndex(
        const scene::SkyEnvironmentRenderState& state_) const noexcept;
    [[nodiscard]] static bool EquivalentState(const scene::SkyEnvironmentRenderState& lhs_,
                                              const scene::SkyEnvironmentRenderState& rhs_) noexcept;
    [[nodiscard]] static SkyEnvironmentGpuParams BuildParams(
        const scene::SkyEnvironmentRenderState& state_) noexcept;

private:
    asset::TextureHost* texture_host = nullptr;
    DescriptorHost* descriptor_host = nullptr;
    resource::SamplerHost* sampler_host = nullptr;
    SkyEnvironmentGpuHostCreateInfo create_info_cache{};
    SkyEnvironmentGpuMcVector<EnvironmentRecord> environments{};
    SkyEnvironmentGpuMcVector<VkDescriptorSet> descriptor_set_cache{};
    SkyEnvironmentGpuHostStats stats{};
    std::uint32_t next_environment_index = 1U;
    bool initialized = false;
};

} // namespace vr::render

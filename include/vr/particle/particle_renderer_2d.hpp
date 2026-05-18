#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/particle_component.hpp"
#include "vr/ecs/component/particle_emitter_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/particle_runtime_system.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/particle/particle_upload_host.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render_graph/render_graph_types.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/resource/sampler_host.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::asset {
class TextureHost;
}

namespace vr::render {
struct ParticleRenderer2DPrepareView;
struct FrameRecordContext;
class UploadHost;
class BindlessResourceSystem;
}

namespace vr::render_graph {
class GraphCommandContext;
class RenderGraphBuilder;
}

namespace vr::resource {
class GpuMemoryHost;
}

namespace vr::particle {

template<typename T>
using ParticleRenderer2DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct ParticleRenderer2DCreateInfo final {
    Particle2DRuntimeUploadOptions runtime_upload_options{};
    std::uint32_t reserve_component_count = 4096U;
    std::uint32_t reserve_particle_count = 32768U;
    bool input_positions_pixel_space = true;
    bool pixel_space_origin_top_left = true;
    bool clear_swapchain = false;
    VkClearColorValue clear_color = {{0.0F, 0.0F, 0.0F, 0.0F}};
};

struct ParticleRenderer2DStats final {
    std::uint32_t component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint32_t emitter_count = 0U;
    std::uint32_t active_particle_count = 0U;
    std::uint32_t visible_particle_count = 0U;
    std::uint32_t draw_batch_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t indirect_draw_count = 0U;
    std::uint32_t skipped_batch_count = 0U;
    std::uint32_t uploaded_instance_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
    bool cache_reused = false;
    bool skipped_upload = true;
};

class ParticleRenderer2D final {
public:
    ParticleRenderer2D() = default;
    ~ParticleRenderer2D() = default;

    ParticleRenderer2D(const ParticleRenderer2D&) = delete;
    ParticleRenderer2D& operator=(const ParticleRenderer2D&) = delete;
    ParticleRenderer2D(ParticleRenderer2D&&) = delete;
    ParticleRenderer2D& operator=(ParticleRenderer2D&&) = delete;

    void Initialize(const ParticleRenderer2DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetHost(ParticleUploadHost* upload_host_) noexcept;
    void SetSimulationHost(ParticleSimulationHost* simulation_host_) noexcept;
    void SetTextureHost(asset::TextureHost* texture_host_) noexcept;
    void SetHosts(ParticleUploadHost* upload_host_,
                  asset::TextureHost* texture_host_) noexcept;
    void SetSceneData(ecs::Particle<ecs::Dim2>* particle_components_,
                      ecs::ParticleEmitter<ecs::Dim2>* particle_emitters_,
                      ecs::Transform<ecs::Dim2>* transforms_,
                      std::uint32_t component_count_) noexcept;
    void SetOutputTargetConfig(const render::RenderTargetColorOutputConfig& output_target_config_) noexcept;
    void ResetOutputTargetConfig() noexcept;

    void PrepareFrame(const render::ParticleRenderer2DPrepareView& prepare_view_);
    void DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                         render_graph::PassHandle pass_) const;
    void Record(const render::FrameRecordContext& record_context_);
    void RecordGraphOverlay(render_graph::GraphCommandContext& context_,
                            render_graph::ResourceHandle color_target_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const ParticleRenderer2DStats& Stats() const noexcept;

private:
    enum class BlendModeKind : std::uint8_t {
        alpha = 0U,
        additive = 1U,
        multiply = 2U,
        premultiplied_alpha = 3U,
        screen = 4U,
        count = 5U
    };

    struct PushConstants final {
        float viewport_width;
        float viewport_height;
        float inv_viewport_width_2x;
        float inv_viewport_height_2x;
        std::uint32_t params;
        std::uint32_t reserved0;
        std::uint32_t reserved1;
        std::uint32_t reserved2;
    };

    static_assert(sizeof(PushConstants) == 32U);

    [[nodiscard]] static std::size_t BlendModeIndex(BlendModeKind blend_mode_) noexcept;
    [[nodiscard]] static BlendModeKind DecodeBlendModeKind(std::uint32_t pipeline_state_) noexcept;
    [[nodiscard]] static std::uint64_t ComposeBindlessUploadRevision(
        const ecs::ParticleRuntimeBuildStats& runtime_stats_,
        std::uint32_t texture_revision_) noexcept;

    void EnsurePipelineObjects(VulkanContext& context_,
                               render::BindlessResourceSystem& bindless_resources_,
                               render::PipelineHost& pipeline_host_,
                               VkFormat color_format_);
    [[nodiscard]] render::GraphicsPipelineId EnsurePipelineForBlendMode(
        VulkanContext& context_,
        render::PipelineHost& pipeline_host_,
        VkFormat color_format_,
        BlendModeKind blend_mode_);
    void RecordGraphInternal(render_graph::GraphCommandContext& context_,
                             render_graph::ResourceHandle color_target_);
    void RecordDrawBatches(VkCommandBuffer command_buffer_,
                           VkExtent2D render_extent_,
                           VkFormat color_format_,
                           const render_graph::GraphCommandContext* graph_context_ = nullptr);
    void RemapCpuInstancesToBindless();
    [[nodiscard]] std::uint32_t ResolveTextureSlot(std::uint32_t texture_id_) const noexcept;
    [[nodiscard]] std::uint32_t ResolveSamplerSlot(std::uint32_t texture_id_) const noexcept;

private:
    ParticleRenderer2DCreateInfo create_info_cache{};
    ParticleRenderer2DStats stats{};

    ecs::Particle<ecs::Dim2>* particle_components = nullptr;
    ecs::ParticleEmitter<ecs::Dim2>* particle_emitters = nullptr;
    ecs::Transform<ecs::Dim2>* transforms = nullptr;
    std::uint32_t component_count = 0U;

    ecs::Particle2DRuntimeScratch runtime_scratch{};
    ecs::ParticleRuntimeBuildStats last_runtime_build_stats{};
    ParticleSimulationFrameResources last_simulation_resources{};
    ParticleSimulationGpuBuildResult last_gpu_build_result{};
    Particle2DRuntimeUploadResult last_upload_result{};
    bool gpu_build_active = false;

    ParticleUploadHost* particle_upload_host = nullptr;
    ParticleSimulationHost* particle_simulation_host = nullptr;
    asset::TextureHost* texture_host = nullptr;

    VulkanContext* context = nullptr;
    render::UploadHost* upload_host = nullptr;
    render::DescriptorHost* descriptor_host = nullptr;
    render::BindlessResourceSystem* bindless_resources = nullptr;
    render::PipelineHost* pipeline_host = nullptr;
    resource::SamplerHost* sampler_host = nullptr;

    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;

    render::PipelineLayoutId pipeline_layout_id{};
    render::ShaderModuleId shader_vertex_id{};
    render::ShaderModuleId shader_fragment_id{};
    std::array<render::GraphicsPipelineId, static_cast<std::size_t>(BlendModeKind::count)> pipeline_ids{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;

    render::RenderTargetColorOutputConfig output_target_config{};
    ParticleRenderer2DMcVector<std::uint8_t> image_initialized{};
    bool initialized = false;
};

} // namespace vr::particle


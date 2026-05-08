#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/asset/texture_host.hpp"
#include "vr/ecs/component/bounds_component.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/component/particle_component.hpp"
#include "vr/ecs/component/particle_emitter_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/ecs/system/culling_system.hpp"
#include "vr/ecs/system/particle_runtime_system.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/particle/particle_upload_host.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_target_pass.hpp"
#include "vr/render/scene_render_stage.hpp"
#include "vr/resource/image_host.hpp"
#include "vr/resource/sampler_host.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::render {
struct RuntimePrepareContext;
struct FrameRecordContext;
class UploadHost;
}

namespace vr::resource {
class GpuMemoryHost;
}

namespace vr::particle {

template<typename T>
using ParticleRenderer3DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct ParticleRenderer3DCreateInfo final {
    Particle3DRuntimeUploadOptions runtime_upload_options{};
    std::uint32_t reserve_component_count = 4096U;
    std::uint32_t reserve_particle_count = 32768U;
    bool enable_depth = true;
    VkFormat preferred_depth_format = VK_FORMAT_D32_SFLOAT;
    bool clear_depth = true;
    float clear_depth_value = 1.0F;
    std::uint32_t clear_stencil_value = 0U;
    bool clear_swapchain = false;
    VkClearColorValue clear_color = {{0.0F, 0.0F, 0.0F, 0.0F}};
};

struct ParticleRenderer3DStats final {
    std::uint32_t component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint32_t emitter_count = 0U;
    std::uint32_t active_particle_count = 0U;
    std::uint32_t visible_particle_count = 0U;
    std::uint32_t draw_batch_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t indirect_draw_count = 0U;
    std::uint32_t opaque_draw_call_count = 0U;
    std::uint32_t transparent_draw_call_count = 0U;
    std::uint32_t stage_filtered_batch_count = 0U;
    std::uint32_t empty_stage_pass_count = 0U;
    std::uint32_t skipped_batch_count = 0U;
    std::uint32_t depth_test_batch_count = 0U;
    std::uint32_t depth_write_batch_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t uploaded_instance_count = 0U;
    std::uint32_t culling_input_count = 0U;
    std::uint32_t culling_visible_count = 0U;
    std::uint32_t culling_culled_count = 0U;
    std::uint32_t culling_mask_reject_count = 0U;
    std::uint32_t culling_frustum_reject_count = 0U;
    std::uint32_t culling_invalid_bounds_count = 0U;
    std::uint32_t culling_plane_test_count = 0U;
    std::uint32_t lighting_mode_fallback_count = 0U;
    std::uint32_t soft_particle_disabled_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
    bool cache_reused = false;
    bool skipped_upload = true;
    bool used_bounds_culling = false;
    bool depth_interaction_enabled = false;
};

class ParticleRenderer3D final {
public:
    ParticleRenderer3D() = default;
    ~ParticleRenderer3D() = default;

    ParticleRenderer3D(const ParticleRenderer3D&) = delete;
    ParticleRenderer3D& operator=(const ParticleRenderer3D&) = delete;
    ParticleRenderer3D(ParticleRenderer3D&&) = delete;
    ParticleRenderer3D& operator=(ParticleRenderer3D&&) = delete;

    void Initialize(const ParticleRenderer3DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetHost(ParticleUploadHost* upload_host_) noexcept;
    void SetSimulationHost(ParticleSimulationHost* simulation_host_) noexcept;
    void SetTextureHost(asset::TextureHost* texture_host_) noexcept;
    void SetHosts(ParticleUploadHost* upload_host_,
                  asset::TextureHost* texture_host_) noexcept;
    void SetSceneData(ecs::Particle<ecs::Dim3>* particle_components_,
                      ecs::ParticleEmitter<ecs::Dim3>* particle_emitters_,
                      ecs::Transform<ecs::Dim3>* transforms_,
                      std::uint32_t component_count_,
                      ecs::Camera<ecs::Dim3>* camera_component_,
                      ecs::Transform<ecs::Dim3>* camera_transform_,
                      ecs::Bounds<ecs::Dim3>* bounds_components_ = nullptr) noexcept;
    void SetOutputTargetConfig(const render::RenderTargetColorOutputConfig& output_target_config_) noexcept;
    void ResetOutputTargetConfig() noexcept;
    void SetDepthTargetConfig(const render::RenderTargetDepthOutputConfig& depth_output_target_config_) noexcept;
    void ResetDepthTargetConfig() noexcept;

    void PrepareFrame(const render::RuntimePrepareContext& prepare_context_);
    void Record(const render::FrameRecordContext& record_context_);
    void RecordSceneStage(const render::FrameRecordContext& record_context_,
                          render::SceneRenderStage stage_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_);
    void OnSwapchainRecreated(std::uint32_t image_count_,
                              VkExtent2D extent_,
                              VkFormat format_,
                              std::uint64_t last_submitted_value_,
                              std::uint64_t completed_submit_value_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const ParticleRenderer3DStats& Stats() const noexcept;

private:
    enum class BlendModeKind : std::uint8_t {
        alpha = 0U,
        additive = 1U,
        multiply = 2U,
        premultiplied_alpha = 3U,
        screen = 4U,
        count = 5U
    };

    enum class DepthPipelineMode : std::uint8_t {
        no_depth = 0U,
        depth_test = 1U,
        depth_test_write = 2U,
        depth_test_reverse_z = 3U,
        depth_test_write_reverse_z = 4U,
        count = 5U
    };

    struct PushConstants final {
        ecs::Matrix4x4 view_projection;
        ecs::Float4 camera_right;
        ecs::Float4 camera_up;
        ecs::Float4 camera_forward;
        std::uint32_t params;
        std::uint32_t reserved0;
        std::uint32_t reserved1;
        std::uint32_t reserved2;
    };

    struct TextureSetEntry final {
        std::uint32_t texture_id = 0U;
        VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    };

    struct OrderedVisibleEntry final {
        std::uint32_t component_index = 0U;
        std::uint32_t pass_hint_value = 0U;
        std::uint32_t sort_mode_value = 0U;
        std::uint32_t blend_preset_value = 0U;
        float distance_sq = 0.0F;
        std::uint64_t binding_key = 0U;
    };

    struct RetiredDepthImage final {
        resource::ImageResource resource{};
        std::uint64_t retire_value = 0U;
    };

    static_assert(sizeof(PushConstants) == 128U);

    [[nodiscard]] static bool IsDepthFormatSupported(VulkanContext& context_, VkFormat format_) noexcept;
    [[nodiscard]] static bool DepthFormatHasStencil(VkFormat format_) noexcept;
    [[nodiscard]] static VkImageAspectFlags DepthImageAspectMask(VkFormat format_) noexcept;
    [[nodiscard]] static VkFormat ResolveDepthFormat(VulkanContext& context_, VkFormat preferred_format_);

    [[nodiscard]] static std::size_t BlendModeIndex(BlendModeKind blend_mode_) noexcept;
    [[nodiscard]] static std::size_t DepthPipelineModeIndex(DepthPipelineMode mode_) noexcept;
    [[nodiscard]] static BlendModeKind DecodeBlendModeKind(std::uint32_t pipeline_state_) noexcept;
    [[nodiscard]] static ecs::ParticleFacingMode DecodeFacingMode(std::uint32_t pipeline_state_) noexcept;
    [[nodiscard]] static ecs::ParticleRenderMode DecodeRenderMode(std::uint32_t pipeline_state_) noexcept;
    [[nodiscard]] static ecs::ParticleLightingMode DecodeLightingMode(std::uint32_t pipeline_state_) noexcept;
    [[nodiscard]] static std::size_t LowerBoundTextureSetIndex(
        const ParticleRenderer3DMcVector<TextureSetEntry>& entries_,
        std::uint32_t texture_id_) noexcept;
    [[nodiscard]] static DepthPipelineMode ResolveDepthPipelineMode(std::uint32_t pipeline_state_,
                                                                    bool use_depth_,
                                                                    bool reverse_z_) noexcept;

    void BuildOrderedVisibleComponentList();
    [[nodiscard]] float ResolveComponentDistanceSq(std::uint32_t component_index_) const noexcept;
    [[nodiscard]] bool RequiresDepthSorting(const ecs::Particle<ecs::Dim3>& component_) const noexcept;
    [[nodiscard]] ecs::Float3 ResolveCameraPosition() const noexcept;
    [[nodiscard]] ecs::Float3 ResolveCameraRight() const noexcept;
    [[nodiscard]] ecs::Float3 ResolveCameraUp() const noexcept;
    [[nodiscard]] ecs::Float3 ResolveCameraForward() const noexcept;

    void EnsurePipelineObjects(VulkanContext& context_,
                               render::DescriptorHost& descriptor_host_,
                               render::PipelineHost& pipeline_host_,
                               VkFormat color_format_,
                               VkFormat depth_format_);
    [[nodiscard]] render::GraphicsPipelineId EnsurePipelineForMode(
        VulkanContext& context_,
        render::PipelineHost& pipeline_host_,
        VkFormat color_format_,
        VkFormat depth_format_,
        BlendModeKind blend_mode_,
        DepthPipelineMode depth_mode_);
    void EnsureFallbackTexture(VulkanContext& context_,
                               render::UploadHost& upload_host_,
                               std::uint32_t frame_index_);
    [[nodiscard]] VkDescriptorSet AcquireTextureDescriptorSet(std::uint32_t frame_index_,
                                                              std::uint32_t texture_id_);

    void EnsureDepthResources(VulkanContext& context_,
                              std::uint32_t image_count_,
                              VkExtent2D extent_);
    void RetireDepthResources(std::uint64_t retire_value_);
    void CollectRetiredDepthResources(VulkanContext& context_,
                                      std::uint64_t completed_value_);
    void DestroyDepthResources(VulkanContext& context_);
    void DestroyRetiredDepthResources(VulkanContext& context_);

    void RecordInternal(const render::FrameRecordContext& record_context_,
                        std::uint32_t pass_bucket_,
                        bool filter_by_pass_bucket_);

private:
    ParticleRenderer3DCreateInfo create_info_cache{};
    ParticleRenderer3DStats stats{};

    ecs::Particle<ecs::Dim3>* particle_components = nullptr;
    ecs::ParticleEmitter<ecs::Dim3>* particle_emitters = nullptr;
    ecs::Transform<ecs::Dim3>* transforms = nullptr;
    std::uint32_t component_count = 0U;
    ecs::Camera<ecs::Dim3>* camera_component = nullptr;
    ecs::Transform<ecs::Dim3>* camera_transform = nullptr;
    ecs::Bounds<ecs::Dim3>* bounds_components = nullptr;

    ecs::Particle3DRuntimeScratch runtime_scratch{};
    ecs::ParticleRuntimeBuildStats last_runtime_build_stats{};
    ParticleSimulationFrameResources last_simulation_resources{};
    ParticleSimulationGpuBuildResult last_gpu_build_result{};
    ecs::CullingScratch<ecs::Dim3> culling_scratch{};
    ecs::CullingBuildStats culling_stats{};
    ParticleRenderer3DMcVector<OrderedVisibleEntry> ordered_visible_entries{};
    ParticleRenderer3DMcVector<std::uint32_t> ordered_visible_component_indices{};
    Particle3DRuntimeUploadResult last_upload_result{};
    bool gpu_build_active = false;

    ParticleUploadHost* particle_upload_host = nullptr;
    ParticleSimulationHost* particle_simulation_host = nullptr;
    asset::TextureHost* texture_host = nullptr;

    VulkanContext* context = nullptr;
    render::UploadHost* upload_host = nullptr;
    render::DescriptorHost* descriptor_host = nullptr;
    render::PipelineHost* pipeline_host = nullptr;
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    resource::SamplerHost* sampler_host = nullptr;

    render::DescriptorSetLayoutId descriptor_layout_id{};
    render::PipelineLayoutId pipeline_layout_id{};
    render::ShaderModuleId shader_vertex_id{};
    render::ShaderModuleId shader_fragment_id{};
    std::array<std::array<render::GraphicsPipelineId,
                          static_cast<std::size_t>(DepthPipelineMode::count)>,
               static_cast<std::size_t>(BlendModeKind::count)> pipeline_ids{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;
    VkFormat pipeline_depth_format = VK_FORMAT_UNDEFINED;

    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    ParticleRenderer3DMcVector<resource::ImageResource> depth_images{};
    ParticleRenderer3DMcVector<std::uint8_t> depth_image_initialized{};
    ParticleRenderer3DMcVector<RetiredDepthImage> retired_depth_images{};
    ParticleRenderer3DMcVector<std::uint8_t> image_initialized{};

    ParticleRenderer3DMcVector<ParticleRenderer3DMcVector<TextureSetEntry>> frame_texture_sets{};
    render::DescriptorMcVector<render::DescriptorBufferWrite> descriptor_buffer_write_scratch{};
    render::DescriptorMcVector<render::DescriptorImageWrite> descriptor_image_write_scratch{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> descriptor_texel_write_scratch{};

    resource::ImageResource fallback_texture{};
    VkImageLayout fallback_texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    resource::SamplerId texture_sampler_id{};

    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    render::RenderTargetColorOutputConfig output_target_config{};
    render::RenderTargetDepthOutputConfig depth_output_target_config{};
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    bool active_camera_reverse_z = false;
    bool initialized = false;
};

} // namespace vr::particle

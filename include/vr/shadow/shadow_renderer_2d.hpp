#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/bounds_component.hpp"
#include "vr/ecs/component/camera_component.hpp"
#include "vr/ecs/component/shadow_component.hpp"
#include "vr/ecs/component/transform_component.hpp"
#include "vr/render/pipeline_host.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"
#include "vr/render/shadow_frame_coordinator.hpp"
#include "vr/shadow/shadow_atlas_host.hpp"

#include <cstddef>
#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::shadow {

template<typename T>
using ShadowRenderer2DMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct ShadowRenderer2DCreateInfo final {
    ecs::ShadowRuntimeBuildConfig runtime_build{};
    ecs::ShadowCasterBuildConfig caster_build{};
    ShadowAtlasHostCreateInfo atlas{};
    std::uint32_t reserve_shadow_count = 512U;
    std::uint32_t reserve_caster_count = 4096U;
    std::uint32_t reserve_atlas_request_count = 32U;
    VkFormat preferred_depth_format = VK_FORMAT_D32_SFLOAT;
    bool clear_atlas_each_frame = true;
};

struct ShadowRenderer2DStats final {
    std::uint32_t shadow_component_count = 0U;
    std::uint32_t shadow_view_count = 0U;
    std::uint32_t shadow_runtime_updated_count = 0U;
    std::uint32_t shadow_caster_header_count = 0U;
    std::uint32_t shadow_caster_index_count = 0U;
    std::uint32_t atlas_namespace_count = 0U;
    std::uint32_t atlas_layer_draw_pass_count = 0U;
    std::uint32_t atlas_transition_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t skipped_invalid_bounds_count = 0U;
    std::uint32_t skipped_out_of_range_count = 0U;
    bool runtime_cache_reused = false;
    bool runtime_transform_only_update = false;
};

class ShadowRenderer2D final {
public:
    ShadowRenderer2D() = default;
    ~ShadowRenderer2D() = default;

    ShadowRenderer2D(const ShadowRenderer2D&) = delete;
    ShadowRenderer2D& operator=(const ShadowRenderer2D&) = delete;

    ShadowRenderer2D(ShadowRenderer2D&&) = delete;
    ShadowRenderer2D& operator=(ShadowRenderer2D&&) = delete;

    void Initialize(const ShadowRenderer2DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);

    void SetSceneData(ecs::Shadow<ecs::Dim2>* shadow_components_,
                      ecs::Transform<ecs::Dim2>* shadow_transforms_,
                      std::uint32_t shadow_component_count_,
                      ecs::Camera<ecs::Dim2>* camera_component_,
                      ecs::Bounds<ecs::Dim2>* caster_bounds_,
                      std::uint32_t caster_count_) noexcept;
    void SetShadowDirtyHint(const std::uint32_t* dirty_component_indices_,
                            std::uint32_t dirty_component_count_) noexcept;
    void SetTransformDirtyHint(const std::uint32_t* dirty_component_indices_,
                               std::uint32_t dirty_component_count_) noexcept;

    void PrepareFrame(const render::ShadowRenderer2DPrepareView& prepare_view_);
    void Record(const render::FrameRecordContext& record_context_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const ShadowRenderer2DStats& Stats() const noexcept;
    [[nodiscard]] const ShadowAtlasHost& AtlasHost() const noexcept;
    [[nodiscard]] ShadowAtlasHost& AtlasHostMutable() noexcept;
    [[nodiscard]] const render::ShadowFrameCoordinator<ecs::Dim2>& FrameCoordinator() const noexcept;
    [[nodiscard]] render::ShadowFrameCoordinator<ecs::Dim2>& FrameCoordinatorMutable() noexcept;

private:
    struct PushConstants final {
        ecs::Matrix4x4 view_projection{};
        float rect_min_x = 0.0F;
        float rect_min_y = 0.0F;
        float rect_max_x = 0.0F;
        float rect_max_y = 0.0F;
    };
    static_assert(sizeof(PushConstants) == 80U);

    struct AtlasRequestAggregate final {
        std::uint32_t namespace_id = 0U;
        std::uint16_t width = 1U;
        std::uint16_t height = 1U;
        std::uint16_t layer_count = 1U;
    };

    [[nodiscard]] static bool IsDepthFormatSupported(VulkanContext& context_,
                                                     VkFormat format_) noexcept;
    [[nodiscard]] static bool DepthFormatHasStencil(VkFormat format_) noexcept;
    [[nodiscard]] static VkImageAspectFlags DepthAspectMask(VkFormat format_) noexcept;
    [[nodiscard]] static VkFormat ResolveDepthFormat(VulkanContext& context_,
                                                     VkFormat preferred_format_);
    [[nodiscard]] static std::size_t LowerBoundAtlasRequestIndex(
        const ShadowRenderer2DMcVector<AtlasRequestAggregate>& entries_,
        std::uint32_t namespace_id_) noexcept;
    static bool HasValidBounds(const ecs::Bounds<ecs::Dim2>& bounds_) noexcept;

    void EnsurePipelineObjects(VulkanContext& context_,
                               render::PipelineHost& pipeline_host_,
                               VkFormat depth_format_);
    [[nodiscard]] render::GraphicsPipelineId EnsureGraphicsPipeline(VulkanContext& context_,
                                                                    render::PipelineHost& pipeline_host_,
                                                                    VkFormat depth_format_);
    void BuildAtlasRequests();
    void RecordAtlasTransition(VkCommandBuffer command_buffer_,
                               const ShadowAtlasHost::AtlasRecord& atlas_record_,
                               VkImageLayout old_layout_,
                               VkImageLayout new_layout_);
    void RecordOneAtlas(const render::FrameRecordContext& record_context_,
                        ShadowAtlasHost::AtlasRecord& atlas_record_);

private:
    ShadowRenderer2DCreateInfo create_info_cache{};
    ShadowRenderer2DStats stats{};

    ecs::Shadow<ecs::Dim2>* shadow_components = nullptr;
    ecs::Transform<ecs::Dim2>* shadow_transforms = nullptr;
    std::uint32_t shadow_component_count = 0U;
    ecs::Camera<ecs::Dim2>* camera_component = nullptr;
    ecs::Bounds<ecs::Dim2>* caster_bounds = nullptr;
    std::uint32_t caster_count = 0U;

    VulkanContext* context = nullptr;
    render::PipelineHost* pipeline_host = nullptr;
    resource::GpuMemoryHost* gpu_memory_host = nullptr;

    render::ShadowFrameCoordinator<ecs::Dim2> frame_coordinator{};
    render::ShadowPrepareStageResult<ecs::Dim2> last_prepare_result{};
    ShadowAtlasHost atlas_host{};
    ShadowRenderer2DMcVector<AtlasRequestAggregate> atlas_requests{};

    render::PipelineLayoutId pipeline_layout_id{};
    render::ShaderModuleId shader_vertex_id{};
    render::GraphicsPipelineId pipeline_id{};
    VkFormat pipeline_depth_format = VK_FORMAT_UNDEFINED;
    VkFormat resolved_depth_format = VK_FORMAT_UNDEFINED;

    bool initialized = false;
};

} // namespace vr::shadow


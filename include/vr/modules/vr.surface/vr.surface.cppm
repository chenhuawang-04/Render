module;
// Global module fragment
#include "vr/detail/vr_module_fwd.hpp"
#include "Center/Memory/Container/Vector/McVector.hpp"
#include <array>
#include <cstdint>

export module vr.surface;
import vr.types;
import vr.context;
import vr.resource;
import vr.render;
import vr.ecs;

export {
namespace vr::surface {

// --- surface_image_host.hpp --------------------------------------------------

struct SurfaceImageHostCreateInfo final {
    std::uint32_t reserve_image_count = 128U;
    std::uint32_t reserve_retired_image_count = 128U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
};

struct SurfaceImageUploadInfo final {
    std::uint32_t image_id = 0U;
    const void* pixels = nullptr;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    std::uint32_t bytes_per_pixel = 4U;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageLayout shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    bool force_recreate = false;
};

struct SurfaceImageHostStats final {
    std::uint32_t image_count = 0U;
    std::uint32_t uploaded_image_count = 0U;
    std::uint32_t updated_image_count = 0U;
    std::uint32_t removed_image_count = 0U;
    std::uint32_t retired_image_count = 0U;
    std::uint32_t barrier_count = 0U;
    std::uint32_t revision = 0U;
    std::uint64_t uploaded_bytes = 0U;
};

class SurfaceImageHost final {
public:
    struct ImageRecord final {
        std::uint32_t image_id = 0U;
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkImageUsageFlags usage = 0U;
        VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkImageAspectFlags aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
        resource::ImageResource resource{};
        std::uint32_t revision = 0U;
    };

    SurfaceImageHost() = default;
    ~SurfaceImageHost() = default;
    SurfaceImageHost(const SurfaceImageHost&) = delete;
    SurfaceImageHost& operator=(const SurfaceImageHost&) = delete;
    SurfaceImageHost(SurfaceImageHost&&) = delete;
    SurfaceImageHost& operator=(SurfaceImageHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const SurfaceImageHostCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);
    void BeginFrame(VulkanContext& context_, std::uint64_t completed_submit_value_);

    void UploadImage(VulkanContext& context_,
                     render::UploadHost& upload_host_,
                     std::uint32_t frame_index_,
                     std::uint64_t last_submitted_value_,
                     std::uint64_t completed_submit_value_,
                     const SurfaceImageUploadInfo& upload_info_);

    [[nodiscard]] bool RemoveImage(VulkanContext& context_,
                                   std::uint32_t image_id_,
                                   std::uint64_t last_submitted_value_,
                                   std::uint64_t completed_submit_value_);
    [[nodiscard]] const ImageRecord* FindImage(std::uint32_t image_id_) const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const SurfaceImageHostStats& Stats() const noexcept;

private:
    struct RetiredImage final {
        resource::ImageResource resource{};
        std::uint64_t retire_value = 0U;
    };

    [[nodiscard]] std::size_t LowerBoundImageIndex(std::uint32_t image_id_) const noexcept;
    void RetireImage(ImageRecord& record_, std::uint64_t retire_value_);
    void CollectRetiredImages(VulkanContext& context_, std::uint64_t completed_submit_value_);
    void DestroyRetiredImages(VulkanContext& context_) noexcept;

    [[nodiscard]] static resource::ImageResource CreateImageResource(
        VulkanContext& context_, resource::GpuMemoryHost& gpu_memory_host_,
        const SurfaceImageHostCreateInfo& create_info_, const SurfaceImageUploadInfo& upload_info_);

    static void RecordImageBarrier(render::UploadHost& upload_host_, std::uint32_t frame_index_,
                                   VkImage image_, VkImageLayout old_layout_, VkImageLayout new_layout_,
                                   VkImageAspectFlags aspect_mask_,
                                   VkPipelineStageFlags2 src_stage_mask_, VkAccessFlags2 src_access_mask_,
                                   VkPipelineStageFlags2 dst_stage_mask_, VkAccessFlags2 dst_access_mask_);

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    SurfaceImageHostCreateInfo create_info_cache{};
    vr::McVector<ImageRecord> images{};
    vr::McVector<RetiredImage> retired_images{};
    SurfaceImageHostStats stats{};
    bool initialized = false;
};

// --- surface_upload_host.hpp --------------------------------------------------

struct SurfaceUploadHostCreateInfo {
    std::uint32_t frames_in_flight = 2U;
    VkDeviceSize initial_2d_instance_buffer_bytes = 2U * 1024U * 1024U;
    VkDeviceSize initial_3d_instance_buffer_bytes = 4U * 1024U * 1024U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    bool allow_growth = true;
    std::uint32_t patch_merge_gap_bytes = 0U;
    std::uint32_t patch_fallback_coverage_percent = 70U;
    std::uint32_t patch_fallback_copy_count = 96U;
};

struct SurfaceUploadHostStats {
    std::uint64_t uploaded_bytes = 0U;
    std::uint32_t upload_count = 0U;
    std::uint32_t partial_upload_count = 0U;
    std::uint32_t patch_copy_count = 0U;
    std::uint32_t patch_input_count = 0U;
    std::uint32_t patch_merged_count = 0U;
    std::uint32_t patch_dropped_count = 0U;
    std::uint32_t patch_fallback_full_upload_count = 0U;
    std::uint32_t reuse_hit_count = 0U;
    std::uint32_t resized_buffer_count = 0U;
    std::uint32_t barrier_count = 0U;
};

struct Surface2DRuntimeUploadOptions final {
    ecs::Surface2DRuntimeBuildConfig runtime_build{};
    ecs::SurfaceUploadPlanBuildOptions plan_build =
        ecs::SurfaceUploadPlanSystem<ecs::Dim2>::DefaultBuildOptions();
    bool enable_partial_upload = true;
    bool require_dirty_hint_for_partial = true;
    std::uint32_t min_partial_dirty_component_count = 1U;
};

struct Surface3DRuntimeUploadOptions final {
    ecs::Surface3DRuntimeBuildConfig runtime_build{};
    ecs::SurfaceUploadPlanBuildOptions plan_build =
        ecs::SurfaceUploadPlanSystem<ecs::Dim3>::DefaultBuildOptions();
    bool enable_partial_upload = true;
    bool require_dirty_hint_for_partial = true;
    std::uint32_t min_partial_dirty_component_count = 1U;
};

struct Surface2DRuntimeUploadResult final {
    ecs::Surface2DRuntimeBuildStats runtime{};
    ecs::SurfaceUploadPlanStats plan{};
    SurfaceUploadRange upload{};
    bool used_partial_upload = false;
    bool skipped_upload = true;
};

struct Surface3DRuntimeUploadResult final {
    ecs::Surface3DRuntimeBuildStats runtime{};
    ecs::SurfaceUploadPlanStats plan{};
    SurfaceUploadRange upload{};
    bool used_partial_upload = false;
    bool skipped_upload = true;
};

class SurfaceUploadHost final {
public:
    SurfaceUploadHost() = default;
    ~SurfaceUploadHost() = default;
    SurfaceUploadHost(const SurfaceUploadHost&) = delete;
    SurfaceUploadHost& operator=(const SurfaceUploadHost&) = delete;
    SurfaceUploadHost(SurfaceUploadHost&&) = delete;
    SurfaceUploadHost& operator=(SurfaceUploadHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const SurfaceUploadHostCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);
    void BeginFrame(VulkanContext& context_, std::uint32_t frame_index_);

    [[nodiscard]] SurfaceUploadRange Upload2DInstances(VulkanContext& context_,
                                                       render::UploadHost& upload_host_,
                                                       std::uint32_t frame_index_,
                                                       const ecs::Surface2DGpuInstance* instances_,
                                                       std::uint32_t instance_count_,
                                                       std::uint64_t revision_);
    [[nodiscard]] SurfaceUploadRange Upload3DInstances(VulkanContext& context_,
                                                       render::UploadHost& upload_host_,
                                                       std::uint32_t frame_index_,
                                                       const ecs::Surface3DGpuInstance* instances_,
                                                       std::uint32_t instance_count_,
                                                       std::uint64_t revision_);
    [[nodiscard]] SurfaceUploadRange Upload2DInstancePatches(VulkanContext& context_,
                                                             render::UploadHost& upload_host_,
                                                             std::uint32_t frame_index_,
                                                             const ecs::Surface2DGpuInstance* instances_,
                                                             std::uint32_t instance_count_,
                                                             const SurfaceUploadPatch* patches_,
                                                             std::uint32_t patch_count_,
                                                             std::uint64_t revision_);
    [[nodiscard]] SurfaceUploadRange Upload3DInstancePatches(VulkanContext& context_,
                                                             render::UploadHost& upload_host_,
                                                             std::uint32_t frame_index_,
                                                             const ecs::Surface3DGpuInstance* instances_,
                                                             std::uint32_t instance_count_,
                                                             const SurfaceUploadPatch* patches_,
                                                             std::uint32_t patch_count_,
                                                             std::uint64_t revision_);

    [[nodiscard]] Surface2DRuntimeUploadResult PrepareRuntimeAndUpload2D(
        VulkanContext& context_, render::UploadHost& upload_host_, std::uint32_t frame_index_,
        const ecs::Surface<ecs::Dim2>* components_,
        const ecs::Transform<ecs::Dim2>* transforms_,
        std::uint32_t component_count_,
        ecs::Surface2DRuntimeScratch& runtime_scratch_,
        ecs::SurfaceUploadPlanScratch<ecs::Dim2>& plan_scratch_,
        const ecs::Surface2DRuntimeBuildHint& runtime_build_hint_ = {},
        const Surface2DRuntimeUploadOptions& options_ = {});

    [[nodiscard]] Surface3DRuntimeUploadResult PrepareRuntimeAndUpload3D(
        VulkanContext& context_, render::UploadHost& upload_host_, std::uint32_t frame_index_,
        const ecs::Surface<ecs::Dim3>* components_,
        const ecs::Transform<ecs::Dim3>* transforms_,
        std::uint32_t component_count_,
        ecs::Surface3DRuntimeScratch& runtime_scratch_,
        ecs::SurfaceUploadPlanScratch<ecs::Dim3>& plan_scratch_,
        const ecs::Surface3DRuntimeBuildHint& runtime_build_hint_ = {},
        const Surface3DRuntimeUploadOptions& options_ = {});

    [[nodiscard]] static bool ShouldAttemptPartialUpload(
        const ecs::Surface2DRuntimeBuildStats& runtime_stats_,
        const ecs::Surface2DRuntimeBuildHint& runtime_build_hint_,
        const Surface2DRuntimeUploadOptions& options_) noexcept;
    [[nodiscard]] static bool ShouldAttemptPartialUpload(
        const ecs::Surface3DRuntimeBuildStats& runtime_stats_,
        const ecs::Surface3DRuntimeBuildHint& runtime_build_hint_,
        const Surface3DRuntimeUploadOptions& options_) noexcept;
    [[nodiscard]] static std::uint64_t ComposeUploadRevision(
        std::uint64_t surface_signature_, std::uint64_t transform_signature_) noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] std::uint32_t FramesInFlight() const noexcept;
    [[nodiscard]] const SurfaceUploadHostStats& Stats() const noexcept;

private:
    struct BytePatchRange final {
        VkDeviceSize dst_offset = 0U;
        VkDeviceSize size_bytes = 0U;
    };
    struct StreamBuffer final {
        resource::BufferResource buffer{};
        VkDeviceSize capacity_bytes = 0U;
        VkDeviceSize uploaded_size_bytes = 0U;
        std::uint32_t element_count = 0U;
        std::uint64_t uploaded_revision = 0U;
    };
    struct FrameState final {
        StreamBuffer instances_2d{};
        StreamBuffer instances_3d{};
    };

    [[nodiscard]] static VkDeviceSize NextPow2(VkDeviceSize value_) noexcept;
    static void DestroyStreamBuffer(VulkanContext& context_, StreamBuffer& stream_);
    void EnsureStreamCapacity(VulkanContext& context_, StreamBuffer& stream_,
                              VkDeviceSize required_bytes_, VkBufferUsageFlags usage_,
                              VkDeviceSize minimum_capacity_bytes_);
    [[nodiscard]] SurfaceUploadRange UploadFullBuffer(VulkanContext& context_, render::UploadHost& upload_host_,
                                                      std::uint32_t frame_index_, StreamBuffer& stream_,
                                                      const void* src_data_, VkDeviceSize size_bytes_,
                                                      std::uint32_t element_count_, std::uint64_t revision_,
                                                      VkDeviceSize minimum_capacity_bytes_);
    [[nodiscard]] SurfaceUploadRange UploadBufferPatches(VulkanContext& context_, render::UploadHost& upload_host_,
                                                         std::uint32_t frame_index_, StreamBuffer& stream_,
                                                         const void* src_data_, std::uint32_t element_count_,
                                                         std::size_t element_size_bytes_,
                                                         const SurfaceUploadPatch* patches_,
                                                         std::uint32_t patch_count_, std::uint64_t revision_,
                                                         VkDeviceSize minimum_capacity_bytes_);
    [[nodiscard]] bool ShouldFallbackToFullUpload(VkDeviceSize full_size_bytes_,
                                                  VkDeviceSize merged_patch_bytes_,
                                                  std::uint32_t merged_patch_count_) const noexcept;
    static void HashCombineRevision(std::uint64_t& hash_, std::uint64_t value_) noexcept;
    void BuildMergedByteRanges(std::uint32_t element_count_, std::size_t element_size_bytes_,
                               const SurfaceUploadPatch* patches_, std::uint32_t patch_count_,
                               std::uint32_t& out_dropped_patch_count_,
                               std::uint64_t& out_merged_patch_bytes_);
    [[nodiscard]] FrameState& FrameAt(std::uint32_t frame_index_);
    [[nodiscard]] const FrameState& FrameAt(std::uint32_t frame_index_) const;

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    SurfaceUploadHostCreateInfo create_info_cache{};
    vr::McVector<FrameState> frames{};
    vr::McVector<SurfaceUploadPatch> patch_scratch{};
    vr::McVector<BytePatchRange> merged_patch_scratch{};
    SurfaceUploadHostStats stats{};
    bool initialized = false;
};

// --- surface_renderer_2d.hpp --------------------------------------------------

struct SurfaceRenderer2DCreateInfo final {
    Surface2DRuntimeUploadOptions runtime_upload_options{};
    std::uint32_t reserve_component_count = 4096U;
    std::uint32_t reserve_instance_count = 16384U;
    std::uint32_t reserve_dirty_component_count = 512U;
    bool input_positions_pixel_space = true;
    bool pixel_space_origin_top_left = true;
    bool clear_swapchain = false;
    VkClearColorValue clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
};

struct SurfaceRenderer2DStats final {
    std::uint32_t component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint32_t appearance_component_count = 0U;
    std::uint32_t appearance_visible_count = 0U;
    std::uint32_t appearance_updated_record_count = 0U;
    std::uint32_t appearance_link_scanned_count = 0U;
    std::uint32_t appearance_link_updated_count = 0U;
    std::uint32_t instance_count = 0U;
    std::uint32_t draw_batch_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t skipped_batch_count = 0U;
    std::uint32_t uploaded_instance_count = 0U;
    std::uint32_t uploaded_patch_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
    bool cache_reused = false;
    bool appearance_cache_reused = false;
    bool transform_only_update = false;
    bool used_partial_upload = false;
    bool skipped_upload = true;
};

class SurfaceRenderer2D final {
public:
    SurfaceRenderer2D() = default;
    ~SurfaceRenderer2D() = default;
    SurfaceRenderer2D(const SurfaceRenderer2D&) = delete;
    SurfaceRenderer2D& operator=(const SurfaceRenderer2D&) = delete;
    SurfaceRenderer2D(SurfaceRenderer2D&&) = delete;
    SurfaceRenderer2D& operator=(SurfaceRenderer2D&&) = delete;

    void Initialize(const SurfaceRenderer2DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);
    void SetHost(SurfaceUploadHost* upload_host_) noexcept;
    void SetImageHost(SurfaceImageHost* image_host_) noexcept;
    void SetHosts(SurfaceUploadHost* upload_host_, SurfaceImageHost* image_host_) noexcept;
    void SetSceneData(ecs::Surface<ecs::Dim2>* surface_components_,
                      ecs::Transform<ecs::Dim2>* transforms_,
                      std::uint32_t component_count_) noexcept;
    void SetAppearanceData(ecs::Appearance<ecs::Dim2>* appearance_components_,
                           std::uint32_t appearance_component_count_) noexcept;
    void SetTransformDirtyHint(const std::uint32_t* dirty_component_indices_,
                               std::uint32_t dirty_component_count_) noexcept;
    void SetAppearanceDirtyHint(const std::uint32_t* dirty_component_indices_,
                                std::uint32_t dirty_component_count_) noexcept;
    void PrepareFrame(const render::RuntimePrepareContext& prepare_context_);
    void Record(const render::FrameRecordContext& record_context_);
    void OnSwapchainRecreated(std::uint32_t image_count_, VkExtent2D extent_, VkFormat format_);
    void OnSwapchainRecreated(std::uint32_t image_count_, VkExtent2D extent_, VkFormat format_,
                              std::uint64_t last_submitted_value_, std::uint64_t completed_submit_value_);
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const SurfaceRenderer2DStats& Stats() const noexcept;

private:
    enum class BlendModeKind : std::uint8_t { alpha = 0U, additive = 1U, multiply = 2U, screen = 3U, count = 4U };
    struct PushConstants final {
        float viewport_width; float viewport_height;
        float inv_viewport_width_2x; float inv_viewport_height_2x;
        std::uint32_t params; std::uint32_t reserved0; std::uint32_t reserved1; std::uint32_t reserved2;
    };
    struct TextureSetEntry final { std::uint32_t image_id = 0U; VkDescriptorSet descriptor_set = VK_NULL_HANDLE; };
    static_assert(sizeof(PushConstants) == 32U);

    [[nodiscard]] static std::size_t BlendModeIndex(BlendModeKind mode_) noexcept;
    [[nodiscard]] static BlendModeKind ResolveBlendModeFromBatchParams(std::uint32_t params_) noexcept;
    [[nodiscard]] static std::size_t LowerBoundTextureSetIndex(
        const vr::McVector<TextureSetEntry>& entries_, std::uint32_t image_id_) noexcept;
    void EnsurePipelineObjects(VulkanContext& context_, render::DescriptorHost& descriptor_host_,
                               render::PipelineHost& pipeline_host_, VkFormat color_format_);
    [[nodiscard]] render::GraphicsPipelineId EnsurePipelineForBlendMode(
        VulkanContext& context_, render::PipelineHost& pipeline_host_, VkFormat color_format_, BlendModeKind blend_mode_);
    void EnsureFallbackTexture(VulkanContext& context_, render::UploadHost& upload_host_, std::uint32_t frame_index_);
    [[nodiscard]] VkDescriptorSet AcquireTextureDescriptorSet(std::uint32_t frame_index_, std::uint32_t image_id_);
    void RecordImageTransitionToColorAttachment(const render::FrameRecordContext& record_context_, bool has_previous_content_) const;
    void RecordImageTransitionToPresent(const render::FrameRecordContext& record_context_) const;

private:
    SurfaceRenderer2DCreateInfo create_info_cache{};
    SurfaceRenderer2DStats stats{};
    ecs::Surface<ecs::Dim2>* surface_components = nullptr;
    ecs::Transform<ecs::Dim2>* transforms = nullptr;
    std::uint32_t component_count = 0U;
    ecs::Appearance<ecs::Dim2>* appearance_components = nullptr;
    std::uint32_t appearance_component_count = 0U;
    ecs::Surface2DRuntimeScratch runtime_scratch{};
    ecs::SurfaceUploadPlanScratch<ecs::Dim2> plan_scratch{};
    ecs::AppearanceRuntimeScratch<ecs::Dim2> appearance_runtime_scratch{};
    ecs::AppearanceRuntimeBuildStats appearance_runtime_stats{};
    ecs::AppearanceLinkStats appearance_link_stats{};
    Surface2DRuntimeUploadResult last_upload_result{};
    SurfaceUploadHost* surface_upload_host = nullptr;
    SurfaceImageHost* surface_image_host = nullptr;
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
    std::array<render::GraphicsPipelineId, static_cast<std::size_t>(BlendModeKind::count)> pipeline_ids{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;
    vr::McVector<vr::McVector<TextureSetEntry>> frame_texture_sets{};
    render::DescriptorMcVector<render::DescriptorImageWrite> descriptor_image_write_scratch{};
    render::DescriptorMcVector<render::DescriptorBufferWrite> descriptor_buffer_write_scratch{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> descriptor_texel_write_scratch{};
    resource::ImageResource fallback_texture{};
    resource::SamplerId fallback_sampler_id{};
    VkImageLayout fallback_texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vr::McVector<std::uint8_t> image_initialized{};
    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    const std::uint32_t* pending_dirty_component_indices = nullptr;
    std::uint32_t pending_dirty_component_count = 0U;
    const std::uint32_t* pending_appearance_dirty_component_indices = nullptr;
    std::uint32_t pending_appearance_dirty_component_count = 0U;
    bool initialized = false;
};

// --- surface_renderer_3d.hpp --------------------------------------------------

struct SurfaceRenderer3DCreateInfo final {
    Surface3DRuntimeUploadOptions runtime_upload_options{};
    std::uint32_t reserve_component_count = 4096U;
    std::uint32_t reserve_instance_count = 8192U;
    std::uint32_t reserve_dirty_component_count = 512U;
    bool enable_depth = true;
    VkFormat preferred_depth_format = VK_FORMAT_D32_SFLOAT;
    bool clear_depth = true;
    float clear_depth_value = 1.0F;
    std::uint32_t clear_stencil_value = 0U;
    bool clear_swapchain = false;
    VkClearColorValue clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
};

struct SurfaceRenderer3DStats final {
    std::uint32_t component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint32_t appearance_component_count = 0U;
    std::uint32_t appearance_visible_count = 0U;
    std::uint32_t appearance_updated_record_count = 0U;
    std::uint32_t appearance_link_scanned_count = 0U;
    std::uint32_t appearance_link_updated_count = 0U;
    std::uint32_t instance_count = 0U;
    std::uint32_t draw_batch_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t skipped_batch_count = 0U;
    std::uint32_t depth_test_batch_count = 0U;
    std::uint32_t depth_write_batch_count = 0U;
    std::uint32_t uploaded_instance_count = 0U;
    std::uint32_t uploaded_patch_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t culling_input_count = 0U;
    std::uint32_t culling_visible_count = 0U;
    std::uint32_t culling_culled_count = 0U;
    std::uint32_t culling_mask_reject_count = 0U;
    std::uint32_t culling_frustum_reject_count = 0U;
    std::uint32_t culling_invalid_bounds_count = 0U;
    std::uint32_t culling_plane_test_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
    bool cache_reused = false;
    bool appearance_cache_reused = false;
    bool transform_only_update = false;
    bool used_partial_upload = false;
    bool skipped_upload = true;
    bool used_bounds_culling = false;
};

class SurfaceRenderer3D final {
public:
    SurfaceRenderer3D() = default;
    ~SurfaceRenderer3D() = default;
    SurfaceRenderer3D(const SurfaceRenderer3D&) = delete;
    SurfaceRenderer3D& operator=(const SurfaceRenderer3D&) = delete;
    SurfaceRenderer3D(SurfaceRenderer3D&&) = delete;
    SurfaceRenderer3D& operator=(SurfaceRenderer3D&&) = delete;

    void Initialize(const SurfaceRenderer3DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);
    void SetHost(SurfaceUploadHost* upload_host_) noexcept;
    void SetImageHost(SurfaceImageHost* image_host_) noexcept;
    void SetHosts(SurfaceUploadHost* upload_host_, SurfaceImageHost* image_host_) noexcept;
    void SetSceneData(ecs::Surface<ecs::Dim3>* surface_components_,
                      ecs::Transform<ecs::Dim3>* transforms_,
                      std::uint32_t component_count_,
                      ecs::Camera<ecs::Dim3>* camera_component_,
                      ecs::Transform<ecs::Dim3>* camera_transform_,
                      ecs::Bounds<ecs::Dim3>* bounds_components_ = nullptr) noexcept;
    void SetAppearanceData(ecs::Appearance<ecs::Dim3>* appearance_components_,
                           std::uint32_t appearance_component_count_) noexcept;
    void SetTransformDirtyHint(const std::uint32_t* dirty_component_indices_,
                               std::uint32_t dirty_component_count_) noexcept;
    void SetAppearanceDirtyHint(const std::uint32_t* dirty_component_indices_,
                                std::uint32_t dirty_component_count_) noexcept;
    void PrepareFrame(const render::RuntimePrepareContext& prepare_context_);
    void Record(const render::FrameRecordContext& record_context_);
    void OnSwapchainRecreated(std::uint32_t image_count_, VkExtent2D extent_, VkFormat format_);
    void OnSwapchainRecreated(std::uint32_t image_count_, VkExtent2D extent_, VkFormat format_,
                              std::uint64_t last_submitted_value_, std::uint64_t completed_submit_value_);
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const SurfaceRenderer3DStats& Stats() const noexcept;

private:
    struct PushConstants final {
        ecs::Matrix4x4 view_projection;
        std::uint32_t params; std::uint32_t reserved0; std::uint32_t reserved1; std::uint32_t reserved2;
    };
    enum class PipelineMode : std::uint8_t { no_depth = 0U, depth_read = 1U, depth_read_write = 2U, count = 3U };
    enum class CullMode : std::uint8_t { back = 0U, none = 1U, count = 2U };
    struct RetiredDepthImage final { resource::ImageResource resource{}; std::uint64_t retire_value = 0U; };
    struct TextureSetEntry final { std::uint64_t binding_key = 0U; VkDescriptorSet descriptor_set = VK_NULL_HANDLE; };
    static_assert(sizeof(PushConstants) == 80U);

    [[nodiscard]] static bool IsDepthFormatSupported(VulkanContext& context_, VkFormat format_) noexcept;
    [[nodiscard]] static bool DepthFormatHasStencil(VkFormat format_) noexcept;
    [[nodiscard]] static VkImageAspectFlags DepthImageAspectMask(VkFormat format_) noexcept;
    [[nodiscard]] static VkFormat ResolveDepthFormat(VulkanContext& context_, VkFormat preferred_format_);
    [[nodiscard]] static std::size_t PipelineModeIndex(PipelineMode mode_) noexcept;
    [[nodiscard]] static std::size_t CullModeIndex(CullMode mode_) noexcept;
    [[nodiscard]] static std::size_t LowerBoundTextureSetIndex(
        const vr::McVector<TextureSetEntry>& entries_, std::uint64_t binding_key_) noexcept;
    [[nodiscard]] static PipelineMode ResolvePipelineMode(std::uint32_t batch_params_, bool use_depth_) noexcept;
    [[nodiscard]] static CullMode ResolveCullMode(std::uint32_t batch_params_) noexcept;
    void EnsurePipelineObjects(VulkanContext& context_, render::DescriptorHost& descriptor_host_,
                               render::PipelineHost& pipeline_host_, VkFormat color_format_, VkFormat depth_format_);
    [[nodiscard]] render::GraphicsPipelineId EnsurePipelineForMode(
        VulkanContext& context_, render::PipelineHost& pipeline_host_,
        VkFormat color_format_, VkFormat depth_format_, PipelineMode mode_, CullMode cull_mode_);
    void EnsureFallbackTexture(VulkanContext& context_, render::UploadHost& upload_host_, std::uint32_t frame_index_);
    [[nodiscard]] VkDescriptorSet AcquireTextureDescriptorSet(std::uint32_t frame_index_,
                                                              std::uint32_t texture_id_, std::uint32_t sampler_id_);
    void EnsureDepthResources(VulkanContext& context_, std::uint32_t image_count_, VkExtent2D extent_);
    void RetireDepthResources(std::uint64_t retire_value_);
    void CollectRetiredDepthResources(VulkanContext& context_, std::uint64_t completed_value_);
    void DestroyDepthResources(VulkanContext& context_);
    void DestroyRetiredDepthResources(VulkanContext& context_);
    void RecordImageTransitionToColorAttachment(const render::FrameRecordContext& record_context_, bool has_previous_content_) const;
    void RecordImageTransitionToPresent(const render::FrameRecordContext& record_context_) const;
    void RecordDepthTransitionToAttachment(VkCommandBuffer command_buffer_,
                                           const resource::ImageResource& depth_resource_, bool initialized_) const;

private:
    SurfaceRenderer3DCreateInfo create_info_cache{};
    SurfaceRenderer3DStats stats{};
    ecs::Surface<ecs::Dim3>* surface_components = nullptr;
    ecs::Transform<ecs::Dim3>* transforms = nullptr;
    std::uint32_t component_count = 0U;
    ecs::Appearance<ecs::Dim3>* appearance_components = nullptr;
    std::uint32_t appearance_component_count = 0U;
    ecs::Camera<ecs::Dim3>* camera_component = nullptr;
    ecs::Transform<ecs::Dim3>* camera_transform = nullptr;
    ecs::Bounds<ecs::Dim3>* bounds_components = nullptr;
    ecs::Surface3DRuntimeScratch runtime_scratch{};
    ecs::SurfaceUploadPlanScratch<ecs::Dim3> plan_scratch{};
    ecs::AppearanceRuntimeScratch<ecs::Dim3> appearance_runtime_scratch{};
    ecs::AppearanceRuntimeBuildStats appearance_runtime_stats{};
    ecs::AppearanceLinkStats appearance_link_stats{};
    ecs::CullingScratch<ecs::Dim3> culling_scratch{};
    ecs::CullingBuildStats culling_stats{};
    Surface3DRuntimeUploadResult last_upload_result{};
    SurfaceUploadHost* surface_upload_host = nullptr;
    SurfaceImageHost* surface_image_host = nullptr;
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
    std::array<std::array<render::GraphicsPipelineId, static_cast<std::size_t>(CullMode::count)>,
               static_cast<std::size_t>(PipelineMode::count)> pipeline_ids{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;
    VkFormat pipeline_depth_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    vr::McVector<resource::ImageResource> depth_images{};
    vr::McVector<std::uint8_t> depth_image_initialized{};
    vr::McVector<RetiredDepthImage> retired_depth_images{};
    vr::McVector<vr::McVector<TextureSetEntry>> frame_texture_sets{};
    render::DescriptorMcVector<render::DescriptorImageWrite> descriptor_image_write_scratch{};
    render::DescriptorMcVector<render::DescriptorBufferWrite> descriptor_buffer_write_scratch{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> descriptor_texel_write_scratch{};
    resource::ImageResource fallback_texture{};
    resource::SamplerId fallback_sampler_id{};
    VkImageLayout fallback_texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vr::McVector<std::uint8_t> image_initialized{};
    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    const std::uint32_t* pending_dirty_component_indices = nullptr;
    std::uint32_t pending_dirty_component_count = 0U;
    const std::uint32_t* pending_appearance_dirty_component_indices = nullptr;
    std::uint32_t pending_appearance_dirty_component_count = 0U;
    bool initialized = false;
};

} // namespace vr::surface
} // export

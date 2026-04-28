module;
// Global module fragment
#include "vr/detail/vr_module_fwd.hpp"
#include "Center/Memory/Container/Vector/McVector.hpp"
#include <array>
#include <cstdint>

export module vr.geometry;
import vr.types;
import vr.context;
import vr.resource;
import vr.render;
import vr.ecs;

export {
namespace vr::geometry {

// --- geometry_resource_host.hpp -----------------------------------------------

struct GeometryResourceHostCreateInfo {
    std::uint32_t reserve_mesh_count = 256U;
    std::uint32_t reserve_submesh_count = 1024U;
    std::uint32_t reserve_reusable_buffer_count = 256U;
    std::uint32_t max_reusable_vertex_buffer_count = 512U;
    std::uint32_t max_reusable_index_buffer_count = 512U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
};

struct GeometryResourceHostStats {
    std::uint32_t mesh_count = 0U;
    std::uint32_t uploaded_mesh_count = 0U;
    std::uint32_t updated_mesh_count = 0U;
    std::uint32_t removed_mesh_count = 0U;
    std::uint32_t retired_buffer_pair_count = 0U;
    std::uint32_t reusable_vertex_buffer_count = 0U;
    std::uint32_t reusable_index_buffer_count = 0U;
    std::uint32_t created_vertex_buffer_count = 0U;
    std::uint32_t created_index_buffer_count = 0U;
    std::uint32_t reused_vertex_buffer_count = 0U;
    std::uint32_t reused_index_buffer_count = 0U;
    std::uint32_t destroyed_vertex_buffer_count = 0U;
    std::uint32_t destroyed_index_buffer_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
};

class GeometryResourceHost final {
public:
    struct MeshRecord final {
        std::uint32_t geometry_id = 0U;
        VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        std::uint32_t vertex_count = 0U;
        std::uint32_t index_count = 0U;
        std::uint32_t vertex_stride = 0U;
        ecs::Float3 bounds_min{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        ecs::Float3 bounds_max{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        resource::BufferResource vertex_buffer{};
        resource::BufferResource index_buffer{};
        vr::McVector<GeometrySubmeshRange> submeshes{};
        std::uint32_t revision = 0U;
    };

    GeometryResourceHost() = default;
    ~GeometryResourceHost() = default;

    GeometryResourceHost(const GeometryResourceHost&) = delete;
    GeometryResourceHost& operator=(const GeometryResourceHost&) = delete;
    GeometryResourceHost(GeometryResourceHost&&) = delete;
    GeometryResourceHost& operator=(GeometryResourceHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const GeometryResourceHostCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);
    void BeginFrame(VulkanContext& context_, std::uint64_t completed_submit_value_);

    void UploadMesh(VulkanContext& context_,
                    render::UploadHost& upload_host_,
                    std::uint32_t frame_index_,
                    std::uint64_t last_submitted_value_,
                    std::uint64_t completed_submit_value_,
                    const GeometryMeshUploadInfo& upload_info_);

    [[nodiscard]] bool RemoveMesh(VulkanContext& context_,
                                  std::uint32_t geometry_id_,
                                  std::uint64_t last_submitted_value_,
                                  std::uint64_t completed_submit_value_);
    [[nodiscard]] const MeshRecord* FindMesh(std::uint32_t geometry_id_) const noexcept;
    [[nodiscard]] std::uint32_t MeshCount() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GeometryResourceHostStats& Stats() const noexcept;

private:
    struct RetiredBuffers final {
        resource::BufferResource vertex_buffer{};
        resource::BufferResource index_buffer{};
        std::uint64_t retire_value = 0U;
    };

    [[nodiscard]] static std::size_t LowerBoundReusableBufferIndex(
        const vr::McVector<resource::BufferResource>& buffers_, VkDeviceSize required_size_) noexcept;
    [[nodiscard]] std::size_t LowerBoundMeshIndex(std::uint32_t geometry_id_) const noexcept;
    void RetireMeshBuffers(MeshRecord& mesh_, std::uint64_t retire_value_);
    void DestroyMeshBuffers(VulkanContext& context_, MeshRecord& mesh_) noexcept;
    void DestroyRetiredBuffers(VulkanContext& context_) noexcept;
    void DestroyReusableBuffers(VulkanContext& context_) noexcept;
    void CollectRetiredBuffers(VulkanContext& context_, std::uint64_t completed_submit_value_);
    void RecycleOrDestroyBuffer(VulkanContext& context_, resource::BufferResource& buffer_,
                                vr::McVector<resource::BufferResource>& reusable_buffers_,
                                std::uint32_t max_reusable_count_, bool is_vertex_buffer_);
    [[nodiscard]] resource::BufferResource AcquireBuffer(VulkanContext& context_, VkDeviceSize required_size_bytes_,
                                                         VkBufferUsageFlags usage_,
                                                         vr::McVector<resource::BufferResource>& reusable_buffers_,
                                                         bool is_vertex_buffer_);
    [[nodiscard]] MeshRecord BuildMeshRecord(VulkanContext& context_, render::UploadHost& upload_host_,
                                             std::uint32_t frame_index_, const GeometryMeshUploadInfo& upload_info_);

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    GeometryResourceHostCreateInfo create_info_cache{};
    vr::McVector<MeshRecord> meshes{};
    vr::McVector<RetiredBuffers> retired_buffers{};
    vr::McVector<resource::BufferResource> reusable_vertex_buffers{};
    vr::McVector<resource::BufferResource> reusable_index_buffers{};
    GeometryResourceHostStats stats{};
    bool initialized = false;
};

// --- geometry_material_host.hpp -----------------------------------------------

struct GeometryMaterialHostCreateInfo {
    std::uint32_t reserve_material_count = 256U;
};

enum GeometryMaterialFlags : std::uint32_t {
    geometry_material_flag_alpha_test = 1U << 0U,
};

struct GeometryMaterialDesc {
    std::uint32_t material_id = 0U;
    std::uint32_t image_id = 0U;
    resource::SamplerId sampler_id{};
    std::uint32_t flags = 0U;
    float uv_scale_u = 1.0F;
    float uv_scale_v = 1.0F;
    float uv_bias_u = 0.0F;
    float uv_bias_v = 0.0F;
    float alpha_cutoff = 0.0F;
};

struct GeometryMaterialHostStats {
    std::uint32_t material_count = 0U;
    std::uint32_t added_material_count = 0U;
    std::uint32_t updated_material_count = 0U;
    std::uint32_t removed_material_count = 0U;
    std::uint32_t revision = 0U;
};

class GeometryMaterialHost final {
public:
    struct MaterialRecord final {
        GeometryMaterialDesc desc{};
        std::uint32_t revision = 0U;
    };

    GeometryMaterialHost() = default;
    ~GeometryMaterialHost() = default;
    GeometryMaterialHost(const GeometryMaterialHost&) = delete;
    GeometryMaterialHost& operator=(const GeometryMaterialHost&) = delete;
    GeometryMaterialHost(GeometryMaterialHost&&) = delete;
    GeometryMaterialHost& operator=(GeometryMaterialHost&&) = delete;

    void Initialize(const GeometryMaterialHostCreateInfo& create_info_ = {});
    void Shutdown() noexcept;
    void UpsertMaterial(const GeometryMaterialDesc& desc_);
    [[nodiscard]] bool RemoveMaterial(std::uint32_t material_id_) noexcept;
    [[nodiscard]] const MaterialRecord* FindMaterial(std::uint32_t material_id_) const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GeometryMaterialHostStats& Stats() const noexcept;

private:
    [[nodiscard]] std::size_t LowerBoundMaterialIndex(std::uint32_t material_id_) const noexcept;

private:
    GeometryMaterialHostCreateInfo create_info_cache{};
    vr::McVector<MaterialRecord> materials{};
    GeometryMaterialHostStats stats{};
    bool initialized = false;
};

// --- geometry_image_host.hpp --------------------------------------------------

struct GeometryImageHostCreateInfo {
    std::uint32_t reserve_image_count = 128U;
    std::uint32_t reserve_retired_image_count = 128U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
};

struct GeometryImageUploadInfo {
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

struct GeometryImageHostStats {
    std::uint32_t image_count = 0U;
    std::uint32_t uploaded_image_count = 0U;
    std::uint32_t updated_image_count = 0U;
    std::uint32_t removed_image_count = 0U;
    std::uint32_t retired_image_count = 0U;
    std::uint32_t barrier_count = 0U;
    std::uint32_t revision = 0U;
    std::uint64_t uploaded_bytes = 0U;
};

class GeometryImageHost final {
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

    GeometryImageHost() = default;
    ~GeometryImageHost() = default;
    GeometryImageHost(const GeometryImageHost&) = delete;
    GeometryImageHost& operator=(const GeometryImageHost&) = delete;
    GeometryImageHost(GeometryImageHost&&) = delete;
    GeometryImageHost& operator=(GeometryImageHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const GeometryImageHostCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);
    void BeginFrame(VulkanContext& context_, std::uint64_t completed_submit_value_);

    void UploadImage(VulkanContext& context_,
                     render::UploadHost& upload_host_,
                     std::uint32_t frame_index_,
                     std::uint64_t last_submitted_value_,
                     std::uint64_t completed_submit_value_,
                     const GeometryImageUploadInfo& upload_info_);

    [[nodiscard]] bool RemoveImage(VulkanContext& context_,
                                   std::uint32_t image_id_,
                                   std::uint64_t last_submitted_value_,
                                   std::uint64_t completed_submit_value_);
    [[nodiscard]] const ImageRecord* FindImage(std::uint32_t image_id_) const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GeometryImageHostStats& Stats() const noexcept;

private:
    struct RetiredImage final {
        resource::ImageResource resource{};
        std::uint64_t retire_value = 0U;
    };

    [[nodiscard]] std::size_t LowerBoundImageIndex(std::uint32_t image_id_) const noexcept;
    void RetireImage(ImageRecord& record_, std::uint64_t retire_value_);
    void CollectRetiredImages(VulkanContext& context_, std::uint64_t completed_submit_value_);
    void DestroyRetiredImages(VulkanContext& context_) noexcept;
    [[nodiscard]] static resource::ImageResource CreateImageResource(VulkanContext& context_,
                                                                     resource::GpuMemoryHost& gpu_memory_host_,
                                                                     const GeometryImageHostCreateInfo& create_info_,
                                                                     const GeometryImageUploadInfo& upload_info_);
    static void RecordImageBarrier(render::UploadHost& upload_host_, std::uint32_t frame_index_,
                                   VkImage image_, VkImageLayout old_layout_, VkImageLayout new_layout_,
                                   VkImageAspectFlags aspect_mask_,
                                   VkPipelineStageFlags2 src_stage_mask_, VkAccessFlags2 src_access_mask_,
                                   VkPipelineStageFlags2 dst_stage_mask_, VkAccessFlags2 dst_access_mask_);

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    GeometryImageHostCreateInfo create_info_cache{};
    vr::McVector<ImageRecord> images{};
    vr::McVector<RetiredImage> retired_images{};
    GeometryImageHostStats stats{};
    bool initialized = false;
};

// --- geometry_upload_host.hpp -------------------------------------------------

struct GeometryUploadHostCreateInfo {
    std::uint32_t frames_in_flight = 2U;
    VkDeviceSize initial_2d_primitive_buffer_bytes = 2U * 1024U * 1024U;
    VkDeviceSize initial_3d_instance_buffer_bytes = 4U * 1024U * 1024U;
    VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    bool allow_growth = true;
};

struct GeometryUploadHostStats {
    std::uint64_t uploaded_bytes = 0U;
    std::uint32_t upload_count = 0U;
    std::uint32_t reuse_hit_count = 0U;
    std::uint32_t resized_buffer_count = 0U;
};

class GeometryUploadHost final {
public:
    GeometryUploadHost() = default;
    ~GeometryUploadHost() = default;
    GeometryUploadHost(const GeometryUploadHost&) = delete;
    GeometryUploadHost& operator=(const GeometryUploadHost&) = delete;
    GeometryUploadHost(GeometryUploadHost&&) = delete;
    GeometryUploadHost& operator=(GeometryUploadHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    resource::GpuMemoryHost& gpu_memory_host_,
                    const GeometryUploadHostCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);
    void BeginFrame(VulkanContext& context_, std::uint32_t frame_index_);

    [[nodiscard]] GeometryUploadRange Upload2DPrimitives(VulkanContext& context_,
                                                         render::UploadHost& upload_host_,
                                                         std::uint32_t frame_index_,
                                                         const ecs::Geometry2DPathPrimitive* primitives_,
                                                         std::uint32_t primitive_count_,
                                                         std::uint64_t revision_);
    [[nodiscard]] GeometryUploadRange Upload3DInstances(VulkanContext& context_,
                                                        render::UploadHost& upload_host_,
                                                        std::uint32_t frame_index_,
                                                        const ecs::Geometry3DGpuInstance* instances_,
                                                        std::uint32_t instance_count_,
                                                        std::uint64_t revision_);
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] std::uint32_t FramesInFlight() const noexcept;
    [[nodiscard]] const GeometryUploadHostStats& Stats() const noexcept;

private:
    struct StreamBuffer final {
        resource::BufferResource buffer{};
        VkDeviceSize capacity_bytes = 0U;
        VkDeviceSize uploaded_size_bytes = 0U;
        std::uint32_t element_count = 0U;
        std::uint64_t uploaded_revision = 0U;
    };

    struct FrameState final {
        StreamBuffer primitives_2d{};
        StreamBuffer instances_3d{};
    };

    [[nodiscard]] static VkDeviceSize NextPow2(VkDeviceSize value_) noexcept;
    static void DestroyStreamBuffer(VulkanContext& context_, StreamBuffer& stream_);
    void EnsureStreamCapacity(VulkanContext& context_, StreamBuffer& stream_,
                              VkDeviceSize required_bytes_, VkBufferUsageFlags usage_,
                              VkDeviceSize minimum_capacity_bytes_);
    [[nodiscard]] GeometryUploadRange UploadToStream(VulkanContext& context_, render::UploadHost& upload_host_,
                                                     std::uint32_t frame_index_, StreamBuffer& stream_,
                                                     const void* src_data_, VkDeviceSize size_bytes_,
                                                     std::uint32_t element_count_, std::uint64_t revision_,
                                                     VkDeviceSize minimum_capacity_bytes_);
    [[nodiscard]] FrameState& FrameAt(std::uint32_t frame_index_);
    [[nodiscard]] const FrameState& FrameAt(std::uint32_t frame_index_) const;

private:
    resource::GpuMemoryHost* gpu_memory_host = nullptr;
    GeometryUploadHostCreateInfo create_info_cache{};
    vr::McVector<FrameState> frames{};
    GeometryUploadHostStats stats{};
    bool initialized = false;
};

// --- geometry_renderer_2d.hpp -------------------------------------------------

struct GeometryRenderer2DCreateInfo {
    ecs::Geometry2DRuntimeBuildConfig runtime_build{};
    std::uint32_t reserve_component_count = 4096U;
    std::uint32_t reserve_primitive_count = 16384U;
    bool input_positions_pixel_space = false;
    bool pixel_space_origin_top_left = true;
    bool clear_swapchain = false;
    VkClearColorValue clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
};

struct GeometryRenderer2DStats {
    std::uint32_t component_count = 0U;
    std::uint32_t visible_component_count = 0U;
    std::uint32_t appearance_component_count = 0U;
    std::uint32_t appearance_visible_count = 0U;
    std::uint32_t appearance_updated_record_count = 0U;
    std::uint32_t appearance_link_scanned_count = 0U;
    std::uint32_t appearance_link_updated_count = 0U;
    std::uint32_t primitive_count = 0U;
    std::uint32_t draw_batch_count = 0U;
    std::uint32_t draw_call_count = 0U;
    std::uint32_t skipped_batch_count = 0U;
    std::uint32_t uploaded_primitive_count = 0U;
    std::uint64_t uploaded_bytes = 0U;
    bool cache_reused = false;
    bool appearance_cache_reused = false;
};

class GeometryRenderer2D final {
public:
    GeometryRenderer2D() = default;
    ~GeometryRenderer2D() = default;
    GeometryRenderer2D(const GeometryRenderer2D&) = delete;
    GeometryRenderer2D& operator=(const GeometryRenderer2D&) = delete;
    GeometryRenderer2D(GeometryRenderer2D&&) = delete;
    GeometryRenderer2D& operator=(GeometryRenderer2D&&) = delete;

    void Initialize(const GeometryRenderer2DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);
    void SetHost(GeometryUploadHost* upload_host_) noexcept;
    void SetSceneData(ecs::Geometry<ecs::Dim2>* geometry_components_,
                      std::uint32_t component_count_) noexcept;
    void SetAppearanceData(ecs::Appearance<ecs::Dim2>* appearance_components_,
                           std::uint32_t appearance_component_count_) noexcept;
    void SetAppearanceDirtyHint(const std::uint32_t* dirty_component_indices_,
                                std::uint32_t dirty_component_count_) noexcept;
    void PrepareFrame(const render::RuntimePrepareContext& prepare_context_);
    void Record(const render::FrameRecordContext& record_context_);
    void OnSwapchainRecreated(std::uint32_t image_count_, VkExtent2D extent_, VkFormat format_);
    void OnSwapchainRecreated(std::uint32_t image_count_, VkExtent2D extent_, VkFormat format_,
                              std::uint64_t last_submitted_value_, std::uint64_t completed_submit_value_);
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GeometryRenderer2DStats& Stats() const noexcept;

private:
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

    void EnsurePipelineObjects(VulkanContext& context_, render::PipelineHost& pipeline_host_, VkFormat color_format_);
    void RecordImageTransitionToColorAttachment(const render::FrameRecordContext& record_context_, bool has_previous_content_) const;
    void RecordImageTransitionToPresent(const render::FrameRecordContext& record_context_) const;

private:
    GeometryRenderer2DCreateInfo create_info_cache{};
    GeometryRenderer2DStats stats{};
    ecs::Geometry<ecs::Dim2>* geometry_components = nullptr;
    std::uint32_t component_count = 0U;
    ecs::Appearance<ecs::Dim2>* appearance_components = nullptr;
    std::uint32_t appearance_component_count = 0U;
    ecs::Geometry2DRuntimeScratch runtime_scratch{};
    ecs::Geometry2DRuntimeBuildStats runtime_stats{};
    ecs::AppearanceRuntimeScratch<ecs::Dim2> appearance_runtime_scratch{};
    ecs::AppearanceRuntimeBuildStats appearance_runtime_stats{};
    ecs::AppearanceLinkStats appearance_link_stats{};
    GeometryUploadHost* geometry_upload_host = nullptr;
    VulkanContext* context = nullptr;
    render::UploadHost* upload_host = nullptr;
    render::PipelineHost* pipeline_host = nullptr;
    render::PipelineLayoutId pipeline_layout_id{};
    render::ShaderModuleId shader_vertex_id{};
    render::ShaderModuleId shader_fragment_id{};
    render::GraphicsPipelineId pipeline_id{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;
    vr::McVector<std::uint8_t> image_initialized{};
    GeometryUploadRange primitive_range{};
    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    const std::uint32_t* pending_appearance_dirty_component_indices = nullptr;
    std::uint32_t pending_appearance_dirty_component_count = 0U;
    bool initialized = false;
};

// --- geometry_renderer_3d.hpp -------------------------------------------------

struct GeometryRenderer3DCreateInfo {
    ecs::Geometry3DRuntimeBuildConfig runtime_build{};
    std::uint32_t reserve_component_count = 4096U;
    std::uint32_t reserve_instance_count = 8192U;
    std::uint32_t reserve_material_set_count = 512U;
    bool enable_depth = true;
    VkFormat preferred_depth_format = VK_FORMAT_D32_SFLOAT;
    bool clear_depth = true;
    float clear_depth_value = 1.0F;
    std::uint32_t clear_stencil_value = 0U;
    bool clear_swapchain = false;
    VkClearColorValue clear_color = {{0.07F, 0.08F, 0.11F, 1.0F}};
    float directional_light_x = 0.5F;
    float directional_light_y = -1.0F;
    float directional_light_z = 0.35F;
    float directional_light_intensity = 1.0F;
    bool compile_required_pipelines_in_prepare = true;
    bool prewarm_common_pipelines = true;
    bool prewarm_depth_read_variant = true;
    bool prewarm_double_sided_variant = true;
    bool prewarm_line_and_point_variants = false;
};

struct GeometryRenderer3DStats {
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
    std::uint32_t shadow_cast_batch_count = 0U;
    std::uint32_t uploaded_instance_count = 0U;
    std::uint32_t descriptor_set_bind_count = 0U;
    std::uint32_t descriptor_set_update_count = 0U;
    std::uint32_t material_push_constant_update_count = 0U;
    std::uint32_t material_resolve_cache_hit_count = 0U;
    std::uint32_t material_resolve_cache_miss_count = 0U;
    std::uint32_t material_set_count = 0U;
    std::uint32_t material_resolve_cache_entry_count = 0U;
    std::uint32_t prewarmed_pipeline_count = 0U;
    std::uint32_t prepare_compiled_pipeline_count = 0U;
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
    bool used_bounds_culling = false;
};

class GeometryRenderer3D final {
public:
    GeometryRenderer3D() = default;
    ~GeometryRenderer3D() = default;
    GeometryRenderer3D(const GeometryRenderer3D&) = delete;
    GeometryRenderer3D& operator=(const GeometryRenderer3D&) = delete;
    GeometryRenderer3D(GeometryRenderer3D&&) = delete;
    GeometryRenderer3D& operator=(GeometryRenderer3D&&) = delete;

    void Initialize(const GeometryRenderer3DCreateInfo& create_info_ = {});
    void Shutdown(VulkanContext& context_);
    void SetHosts(GeometryResourceHost* resource_host_, GeometryUploadHost* upload_host_) noexcept;
    void SetMaterialHosts(GeometryMaterialHost* material_host_, GeometryImageHost* image_host_) noexcept;
    void SetSceneData(ecs::Geometry<ecs::Dim3>* geometry_components_,
                      ecs::Transform<ecs::Dim3>* transforms_,
                      std::uint32_t component_count_,
                      ecs::Camera<ecs::Dim3>* camera_component_,
                      ecs::Transform<ecs::Dim3>* camera_transform_,
                      ecs::Bounds<ecs::Dim3>* bounds_components_ = nullptr) noexcept;
    void SetAppearanceData(ecs::Appearance<ecs::Dim3>* appearance_components_,
                           std::uint32_t appearance_component_count_) noexcept;
    void SetAppearanceDirtyHint(const std::uint32_t* dirty_component_indices_,
                                std::uint32_t dirty_component_count_) noexcept;
    void PrepareFrame(const render::RuntimePrepareContext& prepare_context_);
    void Record(const render::FrameRecordContext& record_context_);
    void OnSwapchainRecreated(std::uint32_t image_count_, VkExtent2D extent_, VkFormat format_);
    void OnSwapchainRecreated(std::uint32_t image_count_, VkExtent2D extent_, VkFormat format_,
                              std::uint64_t last_submitted_value_, std::uint64_t completed_submit_value_);
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GeometryRenderer3DStats& Stats() const noexcept;

private:
    struct FramePushConstants final {
        ecs::Matrix4x4 view_projection;
        float directional_light_x;
        float directional_light_y;
        float directional_light_z;
        float directional_light_intensity;
    };
    struct MaterialPushConstants final {
        float uv_scale_u; float uv_scale_v; float uv_bias_u; float uv_bias_v;
        std::uint32_t flags; float alpha_cutoff; float reserved0; float reserved1;
    };
    struct PushConstants final { FramePushConstants frame{}; MaterialPushConstants material{}; };
    static_assert(sizeof(FramePushConstants) == 80U);
    static_assert(sizeof(MaterialPushConstants) == 32U);
    static_assert(sizeof(PushConstants) == 112U);

    enum class PipelineMode : std::uint8_t { no_depth = 0U, depth_read = 1U, depth_read_write = 2U, count = 3U };
    enum class TopologyMode : std::uint8_t { triangles = 0U, lines = 1U, points = 2U, count = 3U };
    enum class CullMode : std::uint8_t { back = 0U, none = 1U, count = 2U };

    struct RetiredDepthImage final { resource::ImageResource resource{}; std::uint64_t retire_value = 0U; };
    struct MaterialSetEntry final { std::uint32_t material_id = 0U; VkDescriptorSet descriptor_set = VK_NULL_HANDLE; MaterialPushConstants material_push_constants{}; };
    struct ResolvedMaterialEntry final { std::uint32_t material_id = 0U; std::uint32_t material_revision = 0U; std::uint32_t image_id = 0U; std::uint32_t image_revision = 0U; VkImageView image_view = VK_NULL_HANDLE; VkImageLayout image_layout = VK_IMAGE_LAYOUT_UNDEFINED; VkSampler sampler = VK_NULL_HANDLE; MaterialPushConstants material_push_constants{}; };

    [[nodiscard]] static bool IsDepthFormatSupported(VulkanContext& context_, VkFormat format_) noexcept;
    [[nodiscard]] static bool DepthFormatHasStencil(VkFormat format_) noexcept;
    [[nodiscard]] static VkImageAspectFlags DepthImageAspectMask(VkFormat format_) noexcept;
    [[nodiscard]] static VkFormat ResolveDepthFormat(VulkanContext& context_, VkFormat preferred_format_);
    [[nodiscard]] static std::size_t PipelineModeIndex(PipelineMode mode_) noexcept;
    [[nodiscard]] static std::size_t TopologyModeIndex(TopologyMode mode_) noexcept;
    [[nodiscard]] static std::size_t CullModeIndex(CullMode mode_) noexcept;
    [[nodiscard]] static std::size_t LowerBoundMaterialSetIndex(const vr::McVector<MaterialSetEntry>& entries_, std::uint32_t material_id_) noexcept;
    [[nodiscard]] static std::size_t LowerBoundResolvedMaterialIndex(const vr::McVector<ResolvedMaterialEntry>& entries_, std::uint32_t material_id_) noexcept;
    [[nodiscard]] static PipelineMode ResolvePipelineMode(const ecs::Geometry3DDrawBatch& batch_, bool use_depth_) noexcept;
    [[nodiscard]] static TopologyMode ResolveTopologyMode(VkPrimitiveTopology mesh_topology_, const ecs::Geometry3DDrawBatch& batch_) noexcept;
    [[nodiscard]] static CullMode ResolveCullMode(const ecs::Geometry3DDrawBatch& batch_) noexcept;

    void EnsurePipelineObjects(VulkanContext& context_, render::PipelineHost& pipeline_host_, VkFormat color_format_, VkFormat depth_format_);
    [[nodiscard]] render::GraphicsPipelineId EnsurePipelineForMode(VulkanContext& context_, render::PipelineHost& pipeline_host_, VkFormat color_format_, VkFormat depth_format_, PipelineMode mode_, TopologyMode topology_mode_, CullMode cull_mode_);
    void PrewarmCommonPipelines(VulkanContext& context_, render::PipelineHost& pipeline_host_, VkFormat color_format_, VkFormat depth_format_);
    void CompileRequiredPipelinesForCurrentFrame(VulkanContext& context_, render::PipelineHost& pipeline_host_, VkFormat color_format_, VkFormat depth_format_);
    void EnsureMaterialPipelineObjects(VulkanContext& context_, render::DescriptorHost& descriptor_host_);
    void EnsureFallbackMaterialResources(VulkanContext& context_);
    [[nodiscard]] static MaterialPushConstants BuildMaterialPushConstants(const GeometryMaterialDesc* material_desc_) noexcept;
    [[nodiscard]] bool ResolveMaterialBinding(std::uint32_t material_id_, VkSampler& out_sampler_, VkImageView& out_image_view_, VkImageLayout& out_image_layout_, MaterialPushConstants& out_material_push_constants_);
    [[nodiscard]] VkDescriptorSet AcquireMaterialDescriptorSet(std::uint32_t frame_index_, std::uint32_t material_id_, MaterialPushConstants* out_material_push_constants_ = nullptr);
    void EnsureDepthResources(VulkanContext& context_, std::uint32_t image_count_, VkExtent2D extent_);
    void RetireDepthResources(std::uint64_t retire_value_);
    void CollectRetiredDepthResources(VulkanContext& context_, std::uint64_t completed_value_);
    void DestroyDepthResources(VulkanContext& context_);
    void DestroyRetiredDepthResources(VulkanContext& context_);
    void RecordImageTransitionToColorAttachment(const render::FrameRecordContext& record_context_, bool has_previous_content_) const;
    void RecordImageTransitionToPresent(const render::FrameRecordContext& record_context_) const;
    void RecordDepthTransitionToAttachment(VkCommandBuffer command_buffer_, const resource::ImageResource& depth_resource_, bool initialized_) const;

private:
    GeometryRenderer3DCreateInfo create_info_cache{};
    GeometryRenderer3DStats stats{};
    ecs::Geometry<ecs::Dim3>* geometry_components = nullptr;
    ecs::Transform<ecs::Dim3>* transforms = nullptr;
    std::uint32_t component_count = 0U;
    ecs::Appearance<ecs::Dim3>* appearance_components = nullptr;
    std::uint32_t appearance_component_count = 0U;
    ecs::Camera<ecs::Dim3>* camera_component = nullptr;
    ecs::Transform<ecs::Dim3>* camera_transform = nullptr;
    ecs::Bounds<ecs::Dim3>* bounds_components = nullptr;
    ecs::Geometry3DRuntimeScratch runtime_scratch{};
    ecs::Geometry3DRuntimeBuildStats runtime_stats{};
    ecs::AppearanceRuntimeScratch<ecs::Dim3> appearance_runtime_scratch{};
    ecs::AppearanceRuntimeBuildStats appearance_runtime_stats{};
    ecs::AppearanceLinkStats appearance_link_stats{};
    ecs::CullingScratch<ecs::Dim3> culling_scratch{};
    ecs::CullingBuildStats culling_stats{};
    GeometryResourceHost* geometry_resource_host = nullptr;
    GeometryUploadHost* geometry_upload_host = nullptr;
    GeometryMaterialHost* geometry_material_host = nullptr;
    GeometryImageHost* geometry_image_host = nullptr;
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
    std::array<std::array<std::array<render::GraphicsPipelineId, static_cast<std::size_t>(CullMode::count)>, static_cast<std::size_t>(TopologyMode::count)>, static_cast<std::size_t>(PipelineMode::count)> pipeline_ids{};
    VkFormat pipeline_color_format = VK_FORMAT_UNDEFINED;
    VkFormat pipeline_depth_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    vr::McVector<resource::ImageResource> depth_images{};
    vr::McVector<std::uint8_t> depth_image_initialized{};
    vr::McVector<RetiredDepthImage> retired_depth_images{};
    vr::McVector<std::uint8_t> image_initialized{};
    vr::McVector<vr::McVector<MaterialSetEntry>> frame_material_sets{};
    vr::McVector<ResolvedMaterialEntry> resolved_materials{};
    render::DescriptorMcVector<render::DescriptorImageWrite> descriptor_image_write_scratch{};
    render::DescriptorMcVector<render::DescriptorBufferWrite> descriptor_buffer_write_scratch{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> descriptor_texel_write_scratch{};
    resource::ImageResource fallback_material_image{};
    resource::SamplerId fallback_material_sampler_id{};
    VkImageLayout fallback_material_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::uint32_t active_frame_index = 0U;
    VkExtent2D swapchain_extent{};
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    GeometryUploadRange instance_range{};
    std::uint64_t last_submitted_value_seen = 0U;
    std::uint64_t completed_submit_value_seen = 0U;
    const std::uint32_t* pending_appearance_dirty_component_indices = nullptr;
    std::uint32_t pending_appearance_dirty_component_count = 0U;
    std::uint32_t material_host_revision_seen = 0U;
    std::uint32_t image_host_revision_seen = 0U;
    bool initialized = false;
};

} // namespace vr::geometry
} // export

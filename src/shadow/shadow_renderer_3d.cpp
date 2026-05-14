#include "vr/shadow/shadow_renderer_3d.hpp"

#include "vr/ecs/system/spatial_math.hpp"
#include "vr/shadow/generated/shadow_depth_3d_vert_spv.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace vr::shadow {

namespace {

struct ShadowMorphWeights final {
    float weight0 = 0.0F;
    float weight1 = 0.0F;
    bool enabled = false;
};

struct ShadowVertexDeformParams final {
    ecs::Float4 params0{};
    ecs::Float4 params1{};
    bool enabled = false;
};

[[nodiscard]] ShadowMorphWeights ResolveMorphWeights(
    const ecs::Geometry<ecs::Dim3>& geometry_component_,
    std::uint32_t component_index_,
    const ecs::MorphWeightOutputState* morph_outputs_,
    std::uint32_t morph_output_count_) noexcept {
    ShadowMorphWeights result{};
    if ((geometry_component_.mesh.flags & vr::ecs::geometry_mesh_morph_targets_flag) == 0U ||
        morph_outputs_ == nullptr ||
        component_index_ >= morph_output_count_) {
        return result;
    }

    const ecs::MorphWeightOutputState& output = morph_outputs_[component_index_];
    if (output.weights == nullptr || output.sampled_weight_count == 0U || output.weight_count == 0U) {
        return result;
    }

    result.weight0 = output.weights[0U];
    if (output.sampled_weight_count > 1U && output.weight_count > 1U) {
        result.weight1 = output.weights[1U];
    }
    result.enabled = (result.weight0 != 0.0F) || (result.weight1 != 0.0F);
    return result;
}

[[nodiscard]] ShadowVertexDeformParams ResolveVertexDeformParams(
    const ecs::Geometry<ecs::Dim3>& geometry_component_,
    std::uint32_t component_index_,
    const ecs::VertexDeformOutputState* vertex_deform_outputs_,
    std::uint32_t vertex_deform_output_count_) noexcept {
    ShadowVertexDeformParams result{};
    if ((geometry_component_.mesh.flags & vr::ecs::geometry_mesh_vertex_deform_shader_flag) == 0U ||
        vertex_deform_outputs_ == nullptr ||
        component_index_ >= vertex_deform_output_count_) {
        return result;
    }

    const ecs::VertexDeformOutputState& output = vertex_deform_outputs_[component_index_];
    if (output.parameters == nullptr || output.sampled_parameter_count == 0U || output.parameter_count == 0U) {
        return result;
    }

    result.params0 = output.parameters[0U];
    if (output.sampled_parameter_count > 1U && output.parameter_count > 1U) {
        result.params1 = output.parameters[1U];
    }
    result.enabled =
        result.params0.x != 0.0F || result.params0.y != 0.0F ||
        result.params0.z != 0.0F || result.params0.w != 0.0F ||
        result.params1.x != 0.0F || result.params1.y != 0.0F ||
        result.params1.z != 0.0F || result.params1.w != 0.0F;
    return result;
}

void WriteVertexDeformGpu(ShadowDeformComponentGpu& out_gpu_,
                          const ShadowVertexDeformParams& params_) noexcept {
    out_gpu_.deform_param0[0] = params_.params0.x;
    out_gpu_.deform_param0[1] = params_.params0.y;
    out_gpu_.deform_param0[2] = params_.params0.z;
    out_gpu_.deform_param0[3] = params_.params0.w;
    out_gpu_.deform_param1[0] = params_.params1.x;
    out_gpu_.deform_param1[1] = params_.params1.y;
    out_gpu_.deform_param1[2] = params_.params1.z;
    out_gpu_.deform_param1[3] = params_.params1.w;
}

void WriteAffineWorldMatrix(const ecs::Matrix4x4& world_matrix_,
                            float (&out_affine_)[12]) noexcept {
    out_affine_[0] = world_matrix_.m[0];
    out_affine_[1] = world_matrix_.m[1];
    out_affine_[2] = world_matrix_.m[2];
    out_affine_[3] = world_matrix_.m[4];
    out_affine_[4] = world_matrix_.m[5];
    out_affine_[5] = world_matrix_.m[6];
    out_affine_[6] = world_matrix_.m[8];
    out_affine_[7] = world_matrix_.m[9];
    out_affine_[8] = world_matrix_.m[10];
    out_affine_[9] = world_matrix_.m[12];
    out_affine_[10] = world_matrix_.m[13];
    out_affine_[11] = world_matrix_.m[14];
}

} // namespace

bool ShadowRenderer3D::IsDepthFormatSupported(VulkanContext& context_,
                                              VkFormat format_) noexcept {
    if (format_ == VK_FORMAT_UNDEFINED || context_.PhysicalDevice() == VK_NULL_HANDLE) {
        return false;
    }
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(context_.PhysicalDevice(), format_, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U;
}

bool ShadowRenderer3D::DepthFormatHasStencil(VkFormat format_) noexcept {
    return format_ == VK_FORMAT_D24_UNORM_S8_UINT ||
           format_ == VK_FORMAT_D32_SFLOAT_S8_UINT ||
           format_ == VK_FORMAT_D16_UNORM_S8_UINT;
}

VkImageAspectFlags ShadowRenderer3D::DepthAspectMask(VkFormat format_) noexcept {
    VkImageAspectFlags flags = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (DepthFormatHasStencil(format_)) {
        flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    return flags;
}

VkFormat ShadowRenderer3D::ResolveDepthFormat(VulkanContext& context_,
                                              VkFormat preferred_format_) {
    if (IsDepthFormatSupported(context_, preferred_format_)) {
        return preferred_format_;
    }

    constexpr std::array<VkFormat, 4U> fallback_formats{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };
    for (VkFormat format : fallback_formats) {
        if (IsDepthFormatSupported(context_, format)) {
            return format;
        }
    }
    throw std::runtime_error("ShadowRenderer3D failed to resolve usable depth format");
}

std::size_t ShadowRenderer3D::TopologyModeIndex(TopologyMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

std::size_t ShadowRenderer3D::CullModeIndex(CullMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

std::size_t ShadowRenderer3D::DepthModeIndex(DepthMode mode_) noexcept {
    return static_cast<std::size_t>(mode_);
}

ShadowRenderer3D::TopologyMode ShadowRenderer3D::ResolveTopologyMode(ecs::Geometry3DTopology topology_) noexcept {
    switch (topology_) {
    case ecs::Geometry3DTopology::lines:
        return TopologyMode::lines;
    case ecs::Geometry3DTopology::points:
        return TopologyMode::points;
    case ecs::Geometry3DTopology::triangles:
    default:
        return TopologyMode::triangles;
    }
}

ShadowRenderer3D::CullMode ShadowRenderer3D::ResolveCullMode(const ecs::Geometry<ecs::Dim3>& geometry_component_) noexcept {
    return ecs::IsAppearanceRuntimeBridge3DDoubleSided(
        ecs::ReadAppearanceRuntimeBridge3D(geometry_component_.runtime))
        ? CullMode::none
        : CullMode::back;
}

ShadowRenderer3D::DepthMode ShadowRenderer3D::ResolveDepthMode(
    const ecs::ShadowViewGpuRecord& view_record_) noexcept {
    return ((view_record_.flags & (1U << 1U)) != 0U)
        ? DepthMode::reverse_z
        : DepthMode::forward;
}

ecs::Matrix4x4 ShadowRenderer3D::ComposeEffectiveWorldMatrix(
    const ecs::Matrix4x4& base_world_matrix_,
    const ecs::Geometry<ecs::Dim3>& geometry_component_,
    std::uint32_t component_index_,
    const ecs::SkeletalPoseOutputState<ecs::Dim3>* skeletal_outputs_,
    std::uint32_t skeletal_output_count_) noexcept {
    if ((geometry_component_.mesh.flags & vr::ecs::geometry_mesh_skeletal_root_motion_flag) == 0U ||
        skeletal_outputs_ == nullptr ||
        component_index_ >= skeletal_output_count_) {
        return base_world_matrix_;
    }

    const ecs::SkeletalPoseOutputState<ecs::Dim3>& output = skeletal_outputs_[component_index_];
    if (output.joints == nullptr || output.sampled_joint_count == 0U || output.joint_count == 0U) {
        return base_world_matrix_;
    }

    const ecs::SkeletalJointPose<ecs::Dim3>& root = output.joints[0U];
    const ecs::Matrix4x4 root_motion = ecs::spatial_math::ComposeMatrix4x4Trs(root.position,
                                                                               root.rotation,
                                                                               root.scale);
    return ecs::spatial_math::MultiplyMatrix4x4(base_world_matrix_, root_motion);
}

std::uint32_t ShadowRenderer3D::ResolveAnimatedSubmeshIndex(
    const ecs::Geometry<ecs::Dim3>& geometry_component_,
    std::uint32_t component_index_,
    const ecs::FrameSequenceOutputState* frame_sequence_outputs_,
    std::uint32_t frame_sequence_output_count_) noexcept {
    if ((geometry_component_.mesh.flags & vr::ecs::geometry_mesh_frame_sequence_submesh_flag) == 0U ||
        frame_sequence_outputs_ == nullptr ||
        component_index_ >= frame_sequence_output_count_) {
        return geometry_component_.mesh.submesh_index;
    }

    const ecs::FrameSequenceOutputState& output = frame_sequence_outputs_[component_index_];
    if (output.frame_count == 0U) {
        return geometry_component_.mesh.submesh_index;
    }
    return geometry_component_.mesh.submesh_index + output.frame_index_a;
}

std::size_t ShadowRenderer3D::LowerBoundAtlasRequestIndex(
    const ShadowRenderer3DMcVector<AtlasRequestAggregate>& entries_,
    std::uint32_t namespace_id_) noexcept {
    std::size_t first = 0U;
    std::size_t count = entries_.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (entries_[it].namespace_id < namespace_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

void ShadowRenderer3D::Initialize(const ShadowRenderer3DCreateInfo& create_info_) {
    create_info_cache = create_info_;
    if (create_info_cache.runtime_build.atlas_width == 0U) {
        create_info_cache.runtime_build.atlas_width = 4096U;
    }
    if (create_info_cache.runtime_build.atlas_height == 0U) {
        create_info_cache.runtime_build.atlas_height = 4096U;
    }
    if (create_info_cache.runtime_build.atlas_layer_count == 0U) {
        create_info_cache.runtime_build.atlas_layer_count = 8U;
    }

    create_info_cache.atlas.depth_format = create_info_cache.preferred_depth_format;

    shadow_components = nullptr;
    shadow_transforms = nullptr;
    shadow_component_count = 0U;
    camera_component = nullptr;
    caster_bounds = nullptr;
    geometry_components = nullptr;
    geometry_transforms = nullptr;
    geometry_component_count = 0U;
    skeletal_outputs = nullptr;
    skeletal_output_count = 0U;
    vertex_deform_outputs = nullptr;
    vertex_deform_output_count = 0U;
    frame_sequence_outputs = nullptr;
    frame_sequence_output_count = 0U;
    geometry_resource_host = nullptr;
    morph_outputs = nullptr;
    morph_output_count = 0U;

    context = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;

    std::destroy_at(&frame_coordinator);
    std::construct_at(&frame_coordinator);
    frame_coordinator.Reserve(create_info_cache.reserve_shadow_count,
                              create_info_cache.reserve_caster_count);
    if (create_info_cache.reserve_atlas_request_count > 0U) {
        atlas_requests.reserve(create_info_cache.reserve_atlas_request_count);
    }

    last_prepare_result = {};
    atlas_requests.clear();
    frame_animation_resources.clear();
    skeletal_component_scratch.clear();
    skeletal_matrix_scratch.clear();
    deform_component_scratch.clear();

    pipeline_layout_id = {};
    descriptor_layout_id = {};
    shader_vertex_id = {};
    for (auto& per_depth : pipeline_ids) {
        for (auto& per_topology : per_depth) {
            for (auto& pipeline_id : per_topology) {
                pipeline_id = {};
            }
        }
    }
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    resolved_depth_format = VK_FORMAT_UNDEFINED;
    stats = {};
    initialized = true;
}

void ShadowRenderer3D::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    atlas_host.Shutdown(context_);

    shadow_components = nullptr;
    shadow_transforms = nullptr;
    shadow_component_count = 0U;
    camera_component = nullptr;
    caster_bounds = nullptr;
    geometry_components = nullptr;
    geometry_transforms = nullptr;
    geometry_component_count = 0U;
    skeletal_outputs = nullptr;
    skeletal_output_count = 0U;
    vertex_deform_outputs = nullptr;
    vertex_deform_output_count = 0U;
    frame_sequence_outputs = nullptr;
    frame_sequence_output_count = 0U;
    geometry_resource_host = nullptr;
    morph_outputs = nullptr;
    morph_output_count = 0U;

    context = nullptr;
    descriptor_host = nullptr;
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;

    std::destroy_at(&frame_coordinator);
    std::construct_at(&frame_coordinator);
    last_prepare_result = {};
    atlas_requests.clear();
    for (auto& frame_resources : frame_animation_resources) {
        resource::BufferHost::DestroyBuffer(context_, frame_resources.skeletal_components);
        resource::BufferHost::DestroyBuffer(context_, frame_resources.skeletal_matrices);
        resource::BufferHost::DestroyBuffer(context_, frame_resources.deform_components);
    }
    frame_animation_resources.clear();
    skeletal_component_scratch.clear();
    skeletal_matrix_scratch.clear();
    deform_component_scratch.clear();

    pipeline_layout_id = {};
    descriptor_layout_id = {};
    shader_vertex_id = {};
    for (auto& per_depth : pipeline_ids) {
        for (auto& per_topology : per_depth) {
            for (auto& pipeline_id : per_topology) {
                pipeline_id = {};
            }
        }
    }
    pipeline_depth_format = VK_FORMAT_UNDEFINED;
    resolved_depth_format = VK_FORMAT_UNDEFINED;
    stats = {};
    initialized = false;
}

void ShadowRenderer3D::SetHosts(geometry::GeometryResourceHost* geometry_resource_host_) noexcept {
    geometry_resource_host = geometry_resource_host_;
}

void ShadowRenderer3D::SetSceneData(ecs::Shadow<ecs::Dim3>* shadow_components_,
                                    ecs::Transform<ecs::Dim3>* shadow_transforms_,
                                    std::uint32_t shadow_component_count_,
                                    ecs::Camera<ecs::Dim3>* camera_component_,
                                    ecs::Bounds<ecs::Dim3>* caster_bounds_) noexcept {
    shadow_components = shadow_components_;
    shadow_transforms = shadow_transforms_;
    shadow_component_count = shadow_component_count_;
    camera_component = camera_component_;
    caster_bounds = caster_bounds_;

    frame_coordinator.SetShadowData(shadow_components, shadow_transforms, shadow_component_count);
    frame_coordinator.SetCamera(camera_component);
    frame_coordinator.SetCasterBounds(caster_bounds, geometry_component_count);
}

void ShadowRenderer3D::SetGeometryData(ecs::Geometry<ecs::Dim3>* geometry_components_,
                                       ecs::Transform<ecs::Dim3>* geometry_transforms_,
                                       std::uint32_t geometry_component_count_) noexcept {
    geometry_components = geometry_components_;
    geometry_transforms = geometry_transforms_;
    geometry_component_count = geometry_component_count_;
    frame_coordinator.SetCasterBounds(caster_bounds, geometry_component_count);
}

void ShadowRenderer3D::SetAnimationOutputs(
    const ecs::SkeletalPoseOutputState<ecs::Dim3>* skeletal_outputs_,
    std::uint32_t skeletal_output_count_,
    const ecs::VertexDeformOutputState* vertex_deform_outputs_,
    std::uint32_t vertex_deform_output_count_,
    const ecs::MorphWeightOutputState* morph_outputs_,
    std::uint32_t morph_output_count_,
    const ecs::FrameSequenceOutputState* frame_sequence_outputs_,
    std::uint32_t frame_sequence_output_count_) noexcept {
    skeletal_outputs = skeletal_outputs_;
    skeletal_output_count = skeletal_output_count_;
    vertex_deform_outputs = vertex_deform_outputs_;
    vertex_deform_output_count = vertex_deform_output_count_;
    morph_outputs = morph_outputs_;
    morph_output_count = morph_output_count_;
    frame_sequence_outputs = frame_sequence_outputs_;
    frame_sequence_output_count = frame_sequence_output_count_;
}

void ShadowRenderer3D::SetShadowDirtyHint(const std::uint32_t* dirty_component_indices_,
                                          std::uint32_t dirty_component_count_) noexcept {
    frame_coordinator.SetShadowDirtyHint(dirty_component_indices_, dirty_component_count_);
}

void ShadowRenderer3D::SetTransformDirtyHint(const std::uint32_t* dirty_component_indices_,
                                             std::uint32_t dirty_component_count_) noexcept {
    frame_coordinator.SetTransformDirtyHint(dirty_component_indices_, dirty_component_count_);
}

void ShadowRenderer3D::PrepareFrame(const render::ShadowRenderer3DPrepareView& prepare_view_) {
    if (!initialized) {
        return;
    }
    context = &prepare_view_.device;
    descriptor_host = &prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    gpu_memory_host = &prepare_view_.gpu_memory;
    active_frame_index = prepare_view_.frame.frame_index;
    resolved_depth_format = ResolveDepthFormat(*context, create_info_cache.preferred_depth_format);
    create_info_cache.atlas.depth_format = resolved_depth_format;

    if (!atlas_host.IsInitialized()) {
        atlas_host.Initialize(*context, *gpu_memory_host, create_info_cache.atlas);
    }
    atlas_host.BeginFrame(*context, prepare_view_.progress.completed_submit_value);

    frame_coordinator.SetShadowData(shadow_components, shadow_transforms, shadow_component_count);
    frame_coordinator.SetCamera(camera_component);
    frame_coordinator.SetCasterBounds(caster_bounds, geometry_component_count);
    frame_coordinator.Reserve(shadow_component_count, geometry_component_count);

    last_prepare_result = frame_coordinator.PrepareFrame(prepare_view_.frame.frame_index,
                                                         create_info_cache.runtime_build,
                                                         create_info_cache.caster_build);

    BuildAtlasRequests();
    if (!atlas_requests.empty()) {
        ShadowRenderer3DMcVector<ShadowAtlasRequest> requests{};
        requests.resize(atlas_requests.size());
        for (std::size_t i = 0U; i < atlas_requests.size(); ++i) {
            requests[i] = ShadowAtlasRequest{
                .namespace_id = atlas_requests[i].namespace_id,
                .width = atlas_requests[i].width,
                .height = atlas_requests[i].height,
                .layer_count = atlas_requests[i].layer_count,
            };
        }
        atlas_host.EnsureAtlases(*context,
                                 prepare_view_.progress.last_submitted_value,
                                 prepare_view_.progress.completed_submit_value,
                                 requests.data(),
                                 static_cast<std::uint32_t>(requests.size()));
    }

    stats.shadow_component_count = shadow_component_count;
    stats.geometry_component_count = geometry_component_count;
    stats.shadow_view_count = last_prepare_result.runtime_stats.generated_view_count;
    stats.shadow_runtime_updated_count = last_prepare_result.runtime_stats.updated_record_count;
    stats.shadow_caster_header_count = ecs::ShadowCasterSystem<ecs::Dim3>::HeaderCount(frame_coordinator.CasterScratch());
    stats.shadow_caster_index_count = ecs::ShadowCasterSystem<ecs::Dim3>::CasterIndexCount(frame_coordinator.CasterScratch());
    stats.atlas_namespace_count = static_cast<std::uint32_t>(atlas_requests.size());
    stats.runtime_cache_reused = last_prepare_result.runtime_stats.cache_reused;
    stats.runtime_transform_only_update = last_prepare_result.runtime_stats.transform_only_update;
    stats.skeletal_palette_component_count = 0U;
    stats.skeletal_palette_matrix_count = 0U;

    if (active_frame_index >= frame_animation_resources.size()) {
        frame_animation_resources.resize(active_frame_index + 1U);
    }
    {
        FrameAnimationResources& frame_resources = frame_animation_resources[active_frame_index];
        frame_resources.descriptor_set = VK_NULL_HANDLE;
        frame_resources.descriptor_signature = 0U;
    }
    PrepareAnimationResourcesForFrame();

    if (pipeline_host != nullptr && create_info_cache.compile_required_pipelines_in_prepare) {
        EnsurePipelineObjects(*context, *pipeline_host, resolved_depth_format);
        if (create_info_cache.prewarm_common_pipelines) {
            PrewarmCommonPipelines(*context, *pipeline_host, resolved_depth_format);
        } else {
            CompileRequiredPipelinesForCurrentFrame(*context, *pipeline_host, resolved_depth_format);
        }
    }
}

void ShadowRenderer3D::Record(const render::FrameRecordContext& record_context_) {
    if (!initialized ||
        context == nullptr ||
        descriptor_host == nullptr ||
        pipeline_host == nullptr ||
        geometry_resource_host == nullptr ||
        record_context_.command_buffer == VK_NULL_HANDLE) {
        return;
    }
    if (shadow_components == nullptr ||
        geometry_components == nullptr ||
        geometry_transforms == nullptr ||
        shadow_component_count == 0U ||
        geometry_component_count == 0U) {
        return;
    }

    stats.draw_call_count = 0U;
    stats.draw_indexed_call_count = 0U;
    stats.skipped_no_mesh_count = 0U;
    stats.skipped_invalid_submesh_count = 0U;
    stats.skipped_no_shadow_flag_count = 0U;
    stats.skipped_out_of_range_count = 0U;
    stats.pipeline_bind_count = 0U;
    stats.descriptor_set_bind_count = 0U;
    stats.atlas_layer_draw_pass_count = 0U;
    stats.atlas_transition_count = 0U;

    EnsurePipelineObjects(*context, *pipeline_host, resolved_depth_format);

    for (const AtlasRequestAggregate& request : atlas_requests) {
        ShadowAtlasHost::AtlasRecord* atlas_record = atlas_host.FindAtlas(request.namespace_id);
        if (atlas_record == nullptr || atlas_record->resource.image == VK_NULL_HANDLE) {
            continue;
        }
        RecordOneAtlas(record_context_, *atlas_record);
    }
}

bool ShadowRenderer3D::IsInitialized() const noexcept {
    return initialized;
}

const ShadowRenderer3DStats& ShadowRenderer3D::Stats() const noexcept {
    return stats;
}

const ShadowAtlasHost& ShadowRenderer3D::AtlasHost() const noexcept {
    return atlas_host;
}

ShadowAtlasHost& ShadowRenderer3D::AtlasHostMutable() noexcept {
    return atlas_host;
}

const render::ShadowFrameCoordinator<ecs::Dim3>& ShadowRenderer3D::FrameCoordinator() const noexcept {
    return frame_coordinator;
}

render::ShadowFrameCoordinator<ecs::Dim3>& ShadowRenderer3D::FrameCoordinatorMutable() noexcept {
    return frame_coordinator;
}

void ShadowRenderer3D::EnsurePipelineObjects(VulkanContext& context_,
                                             render::PipelineHost& pipeline_host_,
                                             VkFormat depth_format_) {
    if (descriptor_host == nullptr) {
        throw std::runtime_error("ShadowRenderer3D requires DescriptorHost");
    }
    EnsureDescriptorObjects(context_, *descriptor_host);
    if (!pipeline_layout_id.IsValid()) {
        render::PipelineLayoutDesc layout_desc{};
        layout_desc.set_layouts.push_back(descriptor_host->GetLayout(descriptor_layout_id));
        render::PushConstantRangeDesc push_constant_range{};
        push_constant_range.stage_flags = VK_SHADER_STAGE_VERTEX_BIT;
        push_constant_range.offset = 0U;
        push_constant_range.size = sizeof(PushConstants);
        layout_desc.push_constant_ranges.push_back(push_constant_range);
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, layout_desc);
    }

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create{};
        shader_create.code_words = generated::k_shadow_depth_3d_vert_spv;
        shader_create.word_count = std::size(generated::k_shadow_depth_3d_vert_spv);
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create);
    }

    if (pipeline_depth_format != depth_format_) {
        for (auto& per_depth : pipeline_ids) {
            for (auto& per_topology : per_depth) {
                for (auto& pipeline_id : per_topology) {
                    pipeline_id = {};
                }
            }
        }
        pipeline_depth_format = depth_format_;
    }
}

void ShadowRenderer3D::EnsureDescriptorObjects(VulkanContext& context_,
                                               render::DescriptorHost& descriptor_host_) {
    if (descriptor_layout_id.IsValid()) {
        return;
    }

    render::DescriptorSetLayoutDesc layout_desc{};
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0U;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1U;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layout_desc.bindings.push_back(binding);

    binding.binding = 1U;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1U;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layout_desc.bindings.push_back(binding);

    binding.binding = 2U;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1U;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    layout_desc.bindings.push_back(binding);

    descriptor_layout_id = descriptor_host_.RegisterLayout(context_, layout_desc);
}

render::GraphicsPipelineId ShadowRenderer3D::EnsureGraphicsPipeline(VulkanContext& context_,
                                                                    render::PipelineHost& pipeline_host_,
                                                                    VkFormat depth_format_,
                                                                    TopologyMode topology_mode_,
                                                                    CullMode cull_mode_,
                                                                    DepthMode depth_mode_) {
    const std::size_t depth_index = DepthModeIndex(depth_mode_);
    const std::size_t topology_index = TopologyModeIndex(topology_mode_);
    const std::size_t cull_index = CullModeIndex(cull_mode_);
    render::GraphicsPipelineId& pipeline_id = pipeline_ids[depth_index][topology_index][cull_index];
    if (pipeline_id.IsValid()) {
        ++stats.reused_pipeline_count;
        return pipeline_id;
    }

    if (depth_format_ == VK_FORMAT_UNDEFINED) {
        return {};
    }

    const VkPipelineLayout pipeline_layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    const VkShaderModule shader_module = pipeline_host_.GetShaderModule(shader_vertex_id);
    if (pipeline_layout == VK_NULL_HANDLE || shader_module == VK_NULL_HANDLE) {
        return {};
    }

    render::GraphicsPipelineDesc desc{};
    desc.layout = pipeline_layout;

    render::PipelineShaderStageDesc vertex_stage{};
    vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertex_stage.module = shader_module;
    vertex_stage.entry_name = "main";
    desc.shader_stages.push_back(vertex_stage);

    VkVertexInputBindingDescription binding_desc{};
    binding_desc.binding = 0U;
    binding_desc.stride = sizeof(geometry::GeometryMeshVertex);
    binding_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    desc.vertex_input.bindings.push_back(binding_desc);

    VkVertexInputAttributeDescription attribute_desc{};
    attribute_desc.location = 0U;
    attribute_desc.binding = 0U;
    attribute_desc.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_desc.offset = offsetof(geometry::GeometryMeshVertex, position_x);
    desc.vertex_input.attributes.push_back(attribute_desc);

    attribute_desc.location = 1U;
    attribute_desc.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_desc.offset = offsetof(geometry::GeometryMeshVertex, normal_x);
    desc.vertex_input.attributes.push_back(attribute_desc);

    attribute_desc.location = 12U;
    attribute_desc.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_desc.offset = offsetof(geometry::GeometryMeshVertex, morph0_position_delta_x);
    desc.vertex_input.attributes.push_back(attribute_desc);

    attribute_desc.location = 13U;
    attribute_desc.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_desc.offset = offsetof(geometry::GeometryMeshVertex, morph0_normal_delta_x);
    desc.vertex_input.attributes.push_back(attribute_desc);

    attribute_desc.location = 14U;
    attribute_desc.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_desc.offset = offsetof(geometry::GeometryMeshVertex, morph1_position_delta_x);
    desc.vertex_input.attributes.push_back(attribute_desc);

    attribute_desc.location = 15U;
    attribute_desc.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_desc.offset = offsetof(geometry::GeometryMeshVertex, morph1_normal_delta_x);
    desc.vertex_input.attributes.push_back(attribute_desc);

    attribute_desc.location = 17U;
    attribute_desc.format = VK_FORMAT_R32G32B32A32_UINT;
    attribute_desc.offset = offsetof(geometry::GeometryMeshVertex, joint_index0);
    desc.vertex_input.attributes.push_back(attribute_desc);

    attribute_desc.location = 18U;
    attribute_desc.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribute_desc.offset = offsetof(geometry::GeometryMeshVertex, joint_weight0);
    desc.vertex_input.attributes.push_back(attribute_desc);

    switch (topology_mode_) {
    case TopologyMode::lines:
        desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        break;
    case TopologyMode::points:
        desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        break;
    case TopologyMode::triangles:
    default:
        desc.input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        break;
    }
    desc.input_assembly.primitive_restart_enable = false;

    desc.viewport.viewport_count = 1U;
    desc.viewport.scissor_count = 1U;

    desc.rasterization.depth_clamp_enable = false;
    desc.rasterization.rasterizer_discard_enable = false;
    desc.rasterization.polygon_mode = VK_POLYGON_MODE_FILL;
    desc.rasterization.cull_mode = (cull_mode_ == CullMode::none)
        ? VK_CULL_MODE_NONE
        : VK_CULL_MODE_BACK_BIT;
    desc.rasterization.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    desc.rasterization.depth_bias_enable = true;
    desc.rasterization.depth_bias_constant_factor = 0.0F;
    desc.rasterization.depth_bias_clamp = 0.0F;
    desc.rasterization.depth_bias_slope_factor = 0.0F;
    desc.rasterization.line_width = 1.0F;

    desc.multisample.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;
    desc.multisample.sample_shading_enable = false;

    desc.depth_stencil.depth_test_enable = true;
    desc.depth_stencil.depth_write_enable = true;
    desc.depth_stencil.depth_compare_op = (depth_mode_ == DepthMode::reverse_z)
        ? VK_COMPARE_OP_GREATER_OR_EQUAL
        : VK_COMPARE_OP_LESS_OR_EQUAL;
    desc.depth_stencil.depth_bounds_test_enable = false;
    desc.depth_stencil.stencil_test_enable = false;

    desc.color_blend.attachments.clear();

    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_SCISSOR);
    desc.dynamic.states.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);

    desc.use_dynamic_rendering = true;
    desc.rendering.depth_attachment_format = depth_format_;
    desc.rendering.stencil_attachment_format = DepthFormatHasStencil(depth_format_)
        ? depth_format_
        : VK_FORMAT_UNDEFINED;

    pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, desc);
    if (pipeline_id.IsValid()) {
        ++stats.pipeline_compile_count;
    }
    return pipeline_id;
}

void ShadowRenderer3D::PrewarmCommonPipelines(VulkanContext& context_,
                                              render::PipelineHost& pipeline_host_,
                                              VkFormat depth_format_) {
    (void)EnsureGraphicsPipeline(context_,
                                 pipeline_host_,
                                 depth_format_,
                                 TopologyMode::triangles,
                                 CullMode::back,
                                 DepthMode::forward);
    (void)EnsureGraphicsPipeline(context_,
                                 pipeline_host_,
                                 depth_format_,
                                 TopologyMode::triangles,
                                 CullMode::back,
                                 DepthMode::reverse_z);
    (void)EnsureGraphicsPipeline(context_,
                                 pipeline_host_,
                                 depth_format_,
                                 TopologyMode::triangles,
                                 CullMode::none,
                                 DepthMode::forward);
    (void)EnsureGraphicsPipeline(context_,
                                 pipeline_host_,
                                 depth_format_,
                                 TopologyMode::triangles,
                                 CullMode::none,
                                 DepthMode::reverse_z);
    (void)EnsureGraphicsPipeline(context_,
                                 pipeline_host_,
                                 depth_format_,
                                 TopologyMode::lines,
                                 CullMode::none,
                                 DepthMode::forward);
    (void)EnsureGraphicsPipeline(context_,
                                 pipeline_host_,
                                 depth_format_,
                                 TopologyMode::points,
                                 CullMode::none,
                                 DepthMode::forward);
}

void ShadowRenderer3D::CompileRequiredPipelinesForCurrentFrame(VulkanContext& context_,
                                                               render::PipelineHost& pipeline_host_,
                                                               VkFormat depth_format_) {
    const ecs::ShadowViewGpuRecord* view_records = ecs::ShadowRuntimeSystem<ecs::Dim3>::ViewRecords(
        frame_coordinator.RuntimeScratch());
    const std::uint32_t view_count = ecs::ShadowRuntimeSystem<ecs::Dim3>::ViewRecordCount(
        frame_coordinator.RuntimeScratch());
    const ecs::ShadowCasterHeader* headers = ecs::ShadowCasterSystem<ecs::Dim3>::Headers(
        frame_coordinator.CasterScratch());
    const std::uint32_t header_count = ecs::ShadowCasterSystem<ecs::Dim3>::HeaderCount(
        frame_coordinator.CasterScratch());
    const std::uint32_t* caster_indices = ecs::ShadowCasterSystem<ecs::Dim3>::CasterIndices(
        frame_coordinator.CasterScratch());
    const std::uint32_t caster_index_count = ecs::ShadowCasterSystem<ecs::Dim3>::CasterIndexCount(
        frame_coordinator.CasterScratch());
    if (view_records == nullptr ||
        headers == nullptr ||
        caster_indices == nullptr ||
        geometry_components == nullptr) {
        return;
    }

    for (std::uint32_t header_index = 0U; header_index < header_count; ++header_index) {
        const ecs::ShadowCasterHeader& header = headers[header_index];
        if (header.view_index >= view_count) {
            continue;
        }
        if (header.offset + header.count > caster_index_count) {
            continue;
        }
        const ecs::ShadowViewGpuRecord& view_record = view_records[header.view_index];
        const DepthMode depth_mode = ResolveDepthMode(view_record);
        for (std::uint32_t local_index = 0U; local_index < header.count; ++local_index) {
            const std::uint32_t caster_index = caster_indices[header.offset + local_index];
            if (caster_index >= geometry_component_count) {
                continue;
            }
            const ecs::Geometry<ecs::Dim3>& geometry_component = geometry_components[caster_index];
            if (!ecs::IsAppearanceRuntimeBridge3DCastShadowEnabled(
                    ecs::ReadAppearanceRuntimeBridge3D(geometry_component.runtime))) {
                continue;
            }
            const CullMode cull_mode = ResolveCullMode(geometry_component);
            const TopologyMode topology_mode = ResolveTopologyMode(geometry_component.style.topology);
            (void)EnsureGraphicsPipeline(context_,
                                         pipeline_host_,
                                         depth_format_,
                                         topology_mode,
                                         cull_mode,
                                         depth_mode);
        }
    }
}

void ShadowRenderer3D::EnsureStorageBufferCapacity(resource::BufferResource& buffer_,
                                                   VkDeviceSize required_bytes_) {
    if (context == nullptr || gpu_memory_host == nullptr) {
        throw std::runtime_error("ShadowRenderer3D::EnsureStorageBufferCapacity missing runtime hosts");
    }
    if (required_bytes_ == 0U) {
        return;
    }
    if (buffer_.buffer != VK_NULL_HANDLE && buffer_.size >= required_bytes_) {
        return;
    }

    if (buffer_.buffer != VK_NULL_HANDLE) {
        resource::BufferHost::DestroyBuffer(*context, buffer_);
    }

    VkDeviceSize capacity = 256U;
    while (capacity < required_bytes_) {
        capacity <<= 1U;
    }

    resource::BufferCreateInfo create_info{};
    create_info.size = capacity;
    create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    create_info.memory_properties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    create_info.persistently_mapped = true;
    buffer_ = resource::BufferHost::CreateBuffer(*context, create_info, *gpu_memory_host);
}

void ShadowRenderer3D::PrepareAnimationResourcesForFrame() {
    if (context == nullptr || descriptor_host == nullptr) {
        return;
    }

    FrameAnimationResources& frame_resources = frame_animation_resources[active_frame_index];
    const geometry::GeometrySkeletalPaletteBuildStats build_stats =
        geometry::GeometrySkeletalPaletteBuilder::Build(geometry_components,
                                                       geometry_component_count,
                                                       skeletal_outputs,
                                                       skeletal_output_count,
                                                       skeletal_component_scratch,
                                                       skeletal_matrix_scratch);
    if (skeletal_component_scratch.empty()) {
        skeletal_component_scratch.resize(1U);
        skeletal_component_scratch[0U] = {};
    }
    if (skeletal_matrix_scratch.empty()) {
        skeletal_matrix_scratch.resize(1U);
        skeletal_matrix_scratch[0U].matrix = ecs::spatial_math::IdentityMatrix4x4();
    }

    deform_component_scratch.clear();
    if (geometry_component_count == 0U) {
        deform_component_scratch.resize(1U);
        deform_component_scratch[0U] = {};
    } else {
        deform_component_scratch.resize(geometry_component_count);
        for (std::uint32_t component_index = 0U; component_index < geometry_component_count; ++component_index) {
            deform_component_scratch[component_index] = {};
            const ShadowVertexDeformParams deform_params =
                ResolveVertexDeformParams(geometry_components[component_index],
                                          component_index,
                                          vertex_deform_outputs,
                                          vertex_deform_output_count);
            if (!deform_params.enabled) {
                continue;
            }
            WriteVertexDeformGpu(deform_component_scratch[component_index], deform_params);
        }
    }

    const std::uint32_t component_upload_count =
        static_cast<std::uint32_t>(skeletal_component_scratch.size());
    const std::uint32_t matrix_upload_count =
        static_cast<std::uint32_t>(skeletal_matrix_scratch.size());
    const std::uint32_t deform_upload_count =
        static_cast<std::uint32_t>(deform_component_scratch.size());
    const VkDeviceSize component_bytes =
        static_cast<VkDeviceSize>(component_upload_count) * sizeof(geometry::GeometrySkeletalComponentGpu);
    const VkDeviceSize matrix_bytes =
        static_cast<VkDeviceSize>(matrix_upload_count) * sizeof(geometry::GeometrySkeletalMatrixGpu);
    const VkDeviceSize deform_bytes =
        static_cast<VkDeviceSize>(deform_upload_count) * sizeof(ShadowDeformComponentGpu);

    EnsureStorageBufferCapacity(frame_resources.skeletal_components, component_bytes);
    EnsureStorageBufferCapacity(frame_resources.skeletal_matrices, matrix_bytes);
    EnsureStorageBufferCapacity(frame_resources.deform_components, deform_bytes);
    std::memcpy(frame_resources.skeletal_components.mapped_ptr,
                skeletal_component_scratch.data(),
                static_cast<std::size_t>(component_bytes));
    std::memcpy(frame_resources.skeletal_matrices.mapped_ptr,
                skeletal_matrix_scratch.data(),
                static_cast<std::size_t>(matrix_bytes));
    std::memcpy(frame_resources.deform_components.mapped_ptr,
                deform_component_scratch.data(),
                static_cast<std::size_t>(deform_bytes));
    frame_resources.skeletal_component_count = component_upload_count;
    frame_resources.skeletal_matrix_count = matrix_upload_count;
    frame_resources.deform_component_count = deform_upload_count;
    frame_resources.skeletal_signature = build_stats.signature;
    stats.skeletal_palette_component_count = build_stats.skinned_component_count;
    stats.skeletal_palette_matrix_count = build_stats.matrix_count;

    EnsureDescriptorObjects(*context, *descriptor_host);
    if (frame_resources.descriptor_set == VK_NULL_HANDLE) {
        frame_resources.descriptor_set = descriptor_host->AllocateSet(*context,
                                                                      active_frame_index,
                                                                      descriptor_layout_id);
        frame_resources.descriptor_signature = 0U;
    }
    if (frame_resources.descriptor_set == VK_NULL_HANDLE) {
        return;
    }

    std::uint64_t descriptor_signature = 14695981039346656037ULL;
    descriptor_signature ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(frame_resources.skeletal_components.buffer));
    descriptor_signature *= 1099511628211ULL;
    descriptor_signature ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(frame_resources.skeletal_matrices.buffer));
    descriptor_signature *= 1099511628211ULL;
    descriptor_signature ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(frame_resources.deform_components.buffer));
    descriptor_signature *= 1099511628211ULL;
    descriptor_signature ^= frame_resources.skeletal_signature;
    descriptor_signature *= 1099511628211ULL;
    descriptor_signature ^= static_cast<std::uint64_t>(component_bytes);
    descriptor_signature *= 1099511628211ULL;
    descriptor_signature ^= static_cast<std::uint64_t>(matrix_bytes);
    descriptor_signature *= 1099511628211ULL;
    descriptor_signature ^= static_cast<std::uint64_t>(deform_bytes);
    descriptor_signature *= 1099511628211ULL;
    if (frame_resources.descriptor_signature == descriptor_signature) {
        return;
    }

    render::DescriptorMcVector<render::DescriptorBufferWrite> buffer_writes{};
    buffer_writes.reserve(3U);
    buffer_writes.push_back({
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.skeletal_components.buffer,
        .offset = 0U,
        .range = component_bytes
    });
    buffer_writes.push_back({
        .binding = 1U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.skeletal_matrices.buffer,
        .offset = 0U,
        .range = matrix_bytes
    });
    buffer_writes.push_back({
        .binding = 2U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffer = frame_resources.deform_components.buffer,
        .offset = 0U,
        .range = deform_bytes
    });
    render::DescriptorMcVector<render::DescriptorImageWrite> image_writes{};
    render::DescriptorMcVector<render::DescriptorTexelBufferWrite> texel_writes{};
    descriptor_host->UpdateSet(*context,
                               frame_resources.descriptor_set,
                               buffer_writes,
                               image_writes,
                               texel_writes);
    frame_resources.descriptor_signature = descriptor_signature;
    ++stats.descriptor_set_update_count;
}

void ShadowRenderer3D::BuildAtlasRequests() {
    atlas_requests.clear();
    if (shadow_components == nullptr || shadow_component_count == 0U) {
        return;
    }

    const std::uint16_t default_layer_count = std::max<std::uint16_t>(create_info_cache.runtime_build.atlas_layer_count, 1U);
    for (std::uint32_t shadow_index = 0U; shadow_index < shadow_component_count; ++shadow_index) {
        const ecs::Shadow<ecs::Dim3>& component = shadow_components[shadow_index];
        if (!ecs::ShadowSystem<ecs::Dim3>::IsEnabledForBuild(component)) {
            continue;
        }

        const std::uint32_t namespace_id = component.binding.atlas_namespace_id;
        if (namespace_id == 0U) {
            continue;
        }

        const std::uint16_t width = std::max<std::uint16_t>(component.style.map_width, 1U);
        const std::uint16_t height = std::max<std::uint16_t>(component.style.map_height, 1U);
        const std::uint16_t layer_count = default_layer_count;

        const std::size_t insert_index = LowerBoundAtlasRequestIndex(atlas_requests, namespace_id);
        if (insert_index < atlas_requests.size() && atlas_requests[insert_index].namespace_id == namespace_id) {
            AtlasRequestAggregate& existing = atlas_requests[insert_index];
            existing.width = std::max(existing.width, width);
            existing.height = std::max(existing.height, height);
            existing.layer_count = std::max(existing.layer_count, layer_count);
            continue;
        }

        AtlasRequestAggregate aggregate{};
        aggregate.namespace_id = namespace_id;
        aggregate.width = width;
        aggregate.height = height;
        aggregate.layer_count = layer_count;
        const std::size_t old_size = atlas_requests.size();
        atlas_requests.resize(old_size + 1U);
        for (std::size_t move_index = old_size; move_index > insert_index; --move_index) {
            atlas_requests[move_index] = std::move(atlas_requests[move_index - 1U]);
        }
        atlas_requests[insert_index] = aggregate;
    }
}

void ShadowRenderer3D::RecordAtlasTransition(VkCommandBuffer command_buffer_,
                                             const ShadowAtlasHost::AtlasRecord& atlas_record_,
                                             VkImageLayout old_layout_,
                                             VkImageLayout new_layout_) {
    if (command_buffer_ == VK_NULL_HANDLE || atlas_record_.resource.image == VK_NULL_HANDLE) {
        return;
    }
    if (old_layout_ == new_layout_) {
        return;
    }

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.oldLayout = old_layout_;
    barrier.newLayout = new_layout_;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = atlas_record_.resource.image;
    barrier.subresourceRange.aspectMask = DepthAspectMask(atlas_record_.format);
    barrier.subresourceRange.baseMipLevel = 0U;
    barrier.subresourceRange.levelCount = 1U;
    barrier.subresourceRange.baseArrayLayer = 0U;
    barrier.subresourceRange.layerCount = atlas_record_.layer_count;

    VkDependencyInfo dependency_info{};
    dependency_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency_info.imageMemoryBarrierCount = 1U;
    dependency_info.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(command_buffer_, &dependency_info);
    ++stats.atlas_transition_count;
}

void ShadowRenderer3D::RecordOneAtlas(const render::FrameRecordContext& record_context_,
                                      ShadowAtlasHost::AtlasRecord& atlas_record_) {
    const ecs::ShadowViewGpuRecord* view_records = ecs::ShadowRuntimeSystem<ecs::Dim3>::ViewRecords(
        frame_coordinator.RuntimeScratch());
    const std::uint32_t view_count = ecs::ShadowRuntimeSystem<ecs::Dim3>::ViewRecordCount(
        frame_coordinator.RuntimeScratch());
    const ecs::ShadowCasterHeader* headers = ecs::ShadowCasterSystem<ecs::Dim3>::Headers(
        frame_coordinator.CasterScratch());
    const std::uint32_t header_count = ecs::ShadowCasterSystem<ecs::Dim3>::HeaderCount(
        frame_coordinator.CasterScratch());
    const std::uint32_t* caster_indices = ecs::ShadowCasterSystem<ecs::Dim3>::CasterIndices(
        frame_coordinator.CasterScratch());
    const std::uint32_t caster_index_count = ecs::ShadowCasterSystem<ecs::Dim3>::CasterIndexCount(
        frame_coordinator.CasterScratch());
    if (view_records == nullptr ||
        headers == nullptr ||
        caster_indices == nullptr ||
        atlas_record_.layer_views.empty()) {
        return;
    }

    VkCommandBuffer command_buffer = record_context_.command_buffer;
    const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);
    if (pipeline_layout == VK_NULL_HANDLE) {
        return;
    }
    const VkDescriptorSet skeletal_descriptor_set =
        (active_frame_index < frame_animation_resources.size())
            ? frame_animation_resources[active_frame_index].descriptor_set
            : VK_NULL_HANDLE;
    if (skeletal_descriptor_set == VK_NULL_HANDLE) {
        return;
    }

    RecordAtlasTransition(command_buffer,
                          atlas_record_,
                          atlas_record_.current_layout == VK_IMAGE_LAYOUT_UNDEFINED
                              ? VK_IMAGE_LAYOUT_UNDEFINED
                              : atlas_record_.current_layout,
                          VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    atlas_record_.current_layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;

    ShadowRenderer3DMcVector<std::uint8_t> layer_cleared{};
    layer_cleared.resize(atlas_record_.layer_count, 0U);

    VkPipeline last_pipeline = VK_NULL_HANDLE;
    VkDescriptorSet last_descriptor_set = VK_NULL_HANDLE;
    VkBuffer last_vertex_buffer = VK_NULL_HANDLE;
    VkBuffer last_index_buffer = VK_NULL_HANDLE;

    for (std::uint32_t header_index = 0U; header_index < header_count; ++header_index) {
        const ecs::ShadowCasterHeader& header = headers[header_index];
        if (header.view_index >= view_count) {
            ++stats.skipped_out_of_range_count;
            continue;
        }
        if (header.offset + header.count > caster_index_count) {
            ++stats.skipped_out_of_range_count;
            continue;
        }

        const ecs::ShadowViewGpuRecord& view_record = view_records[header.view_index];
        if (view_record.atlas_namespace_id != atlas_record_.namespace_id) {
            continue;
        }
        if (view_record.atlas_layer >= atlas_record_.layer_count) {
            ++stats.skipped_out_of_range_count;
            continue;
        }
        if (view_record.atlas_width == 0U || view_record.atlas_height == 0U) {
            continue;
        }

        const std::uint32_t layer_index = view_record.atlas_layer;
        const VkImageView layer_view = atlas_record_.layer_views[layer_index];
        if (layer_view == VK_NULL_HANDLE) {
            ++stats.skipped_out_of_range_count;
            continue;
        }

        const bool clear_layer = create_info_cache.clear_atlas_each_frame && layer_cleared[layer_index] == 0U;
        layer_cleared[layer_index] = 1U;

        VkRenderingAttachmentInfo depth_attachment{};
        depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_attachment.imageView = layer_view;
        depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_attachment.resolveMode = VK_RESOLVE_MODE_NONE;
        depth_attachment.resolveImageView = VK_NULL_HANDLE;
        depth_attachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_attachment.loadOp = clear_layer ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_attachment.clearValue.depthStencil.depth =
            ((view_record.flags & (1U << 1U)) != 0U) ? 0.0F : 1.0F;
        depth_attachment.clearValue.depthStencil.stencil = 0U;

        VkRenderingInfo rendering_info{};
        rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering_info.renderArea.offset = VkOffset2D{0, 0};
        rendering_info.renderArea.extent = VkExtent2D{
            .width = atlas_record_.width,
            .height = atlas_record_.height,
        };
        rendering_info.layerCount = 1U;
        rendering_info.viewMask = 0U;
        rendering_info.colorAttachmentCount = 0U;
        rendering_info.pColorAttachments = nullptr;
        rendering_info.pDepthAttachment = &depth_attachment;
        rendering_info.pStencilAttachment = DepthFormatHasStencil(atlas_record_.format)
            ? &depth_attachment
            : nullptr;

        vkCmdBeginRendering(command_buffer, &rendering_info);
        ++stats.atlas_layer_draw_pass_count;

        const VkViewport viewport{
            .x = static_cast<float>(view_record.atlas_x),
            .y = static_cast<float>(view_record.atlas_y),
            .width = static_cast<float>(view_record.atlas_width),
            .height = static_cast<float>(view_record.atlas_height),
            .minDepth = 0.0F,
            .maxDepth = 1.0F,
        };
        const VkRect2D scissor{
            .offset = VkOffset2D{
                static_cast<std::int32_t>(view_record.atlas_x),
                static_cast<std::int32_t>(view_record.atlas_y),
            },
            .extent = VkExtent2D{
                .width = view_record.atlas_width,
                .height = view_record.atlas_height,
            },
        };
        vkCmdSetViewport(command_buffer, 0U, 1U, &viewport);
        vkCmdSetScissor(command_buffer, 0U, 1U, &scissor);
        vkCmdSetDepthBias(command_buffer,
                          view_record.depth_bias,
                          0.0F,
                          view_record.slope_scaled_bias);

        for (std::uint32_t local_index = 0U; local_index < header.count; ++local_index) {
            const std::uint32_t caster_index = caster_indices[header.offset + local_index];
            if (caster_index >= geometry_component_count) {
                ++stats.skipped_out_of_range_count;
                continue;
            }

            const ecs::Geometry<ecs::Dim3>& geometry_component = geometry_components[caster_index];
            if (!ecs::IsAppearanceRuntimeBridge3DCastShadowEnabled(
                    ecs::ReadAppearanceRuntimeBridge3D(geometry_component.runtime))) {
                ++stats.skipped_no_shadow_flag_count;
                continue;
            }

            const geometry::GeometryResourceHost::MeshRecord* mesh_record =
                geometry_resource_host->FindMesh(geometry_component.runtime.route.geometry_id);
            if (mesh_record == nullptr ||
                mesh_record->vertex_buffer.buffer == VK_NULL_HANDLE ||
                mesh_record->index_buffer.buffer == VK_NULL_HANDLE) {
                ++stats.skipped_no_mesh_count;
                continue;
            }

            const std::uint32_t submesh_index = ResolveAnimatedSubmeshIndex(geometry_component,
                                                                            caster_index,
                                                                            frame_sequence_outputs,
                                                                            frame_sequence_output_count);
            if (submesh_index >= mesh_record->submeshes.size()) {
                ++stats.skipped_invalid_submesh_count;
                continue;
            }
            const geometry::GeometrySubmeshRange& submesh = mesh_record->submeshes[submesh_index];
            if (submesh.index_count == 0U) {
                continue;
            }

            TopologyMode topology_mode = ResolveTopologyMode(geometry_component.style.topology);
            if (mesh_record->topology == VK_PRIMITIVE_TOPOLOGY_LINE_LIST ||
                mesh_record->topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP) {
                topology_mode = TopologyMode::lines;
            } else if (mesh_record->topology == VK_PRIMITIVE_TOPOLOGY_POINT_LIST) {
                topology_mode = TopologyMode::points;
            } else {
                topology_mode = TopologyMode::triangles;
            }

            const PipelineSelection selection{
                .topology = topology_mode,
                .cull = ResolveCullMode(geometry_component),
                .depth = ResolveDepthMode(view_record),
            };
            const render::GraphicsPipelineId pipeline_id = EnsureGraphicsPipeline(*context,
                                                                                  *pipeline_host,
                                                                                  resolved_depth_format,
                                                                                  selection.topology,
                                                                                  selection.cull,
                                                                                  selection.depth);
            if (!pipeline_id.IsValid()) {
                continue;
            }
            const VkPipeline pipeline = pipeline_host->GetGraphicsPipeline(pipeline_id);
            if (pipeline == VK_NULL_HANDLE) {
                continue;
            }
            if (pipeline != last_pipeline) {
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                last_pipeline = pipeline;
                ++stats.pipeline_bind_count;
            }
            if (skeletal_descriptor_set != VK_NULL_HANDLE &&
                skeletal_descriptor_set != last_descriptor_set) {
                vkCmdBindDescriptorSets(command_buffer,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeline_layout,
                                        0U,
                                        1U,
                                        &skeletal_descriptor_set,
                                        0U,
                                        nullptr);
                last_descriptor_set = skeletal_descriptor_set;
                ++stats.descriptor_set_bind_count;
            }

            const VkBuffer vertex_buffer = mesh_record->vertex_buffer.buffer;
            if (vertex_buffer != last_vertex_buffer) {
                const VkDeviceSize vertex_offset = 0U;
                vkCmdBindVertexBuffers(command_buffer, 0U, 1U, &vertex_buffer, &vertex_offset);
                last_vertex_buffer = vertex_buffer;
            }

            const VkBuffer index_buffer = mesh_record->index_buffer.buffer;
            if (index_buffer != last_index_buffer) {
                vkCmdBindIndexBuffer(command_buffer,
                                     index_buffer,
                                     0U,
                                     VK_INDEX_TYPE_UINT32);
                last_index_buffer = index_buffer;
            }

            PushConstants push_constants{};
            push_constants.view_projection = view_record.view_projection_matrix;
            const ecs::Matrix4x4 effective_world =
                ComposeEffectiveWorldMatrix(geometry_transforms[caster_index].runtime.world_matrix,
                                            geometry_component,
                                            caster_index,
                                            skeletal_outputs,
                                            skeletal_output_count);
            WriteAffineWorldMatrix(effective_world, push_constants.world_affine);
            const ShadowMorphWeights morph_weights =
                ResolveMorphWeights(geometry_component,
                                    caster_index,
                                    morph_outputs,
                                    morph_output_count);
            push_constants.morph_weights[0] = morph_weights.weight0;
            push_constants.morph_weights[1] = morph_weights.weight1;
            push_constants.morph_weights[2] = static_cast<float>(caster_index);
            push_constants.morph_weights[3] = 0.0F;
            if (morph_weights.enabled) {
                ++stats.morph_animated_draw_call_count;
            }
            vkCmdPushConstants(command_buffer,
                               pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT,
                               0U,
                               sizeof(PushConstants),
                               &push_constants);

            vkCmdDrawIndexed(command_buffer,
                             submesh.index_count,
                             1U,
                             submesh.first_index,
                             submesh.vertex_offset,
                             0U);
            ++stats.draw_call_count;
            ++stats.draw_indexed_call_count;
        }

        vkCmdEndRendering(command_buffer);
    }

    RecordAtlasTransition(command_buffer,
                          atlas_record_,
                          VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
    atlas_record_.current_layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
}

} // namespace vr::shadow


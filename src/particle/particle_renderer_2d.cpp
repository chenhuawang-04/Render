#include "vr/particle/particle_renderer_2d.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/render_graph/graph_command_context.hpp"
#include "vr/render_graph/render_graph_builder.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/particle/generated/particle_2d_frag_spv.hpp"
#include "vr/particle/generated/particle_2d_vert_spv.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/render/bindless_resource_system.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/runtime/services/render_graph_runtime_service.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/vulkan_context.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vr::particle {

namespace {

constexpr VkPrimitiveTopology k_particle_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
constexpr VkBufferUsageFlags k_graph_particle_draw_instances_imported_usage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT;
constexpr VkBufferUsageFlags k_graph_particle_indirect_commands_imported_usage =
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
    VK_BUFFER_USAGE_TRANSFER_DST_BIT;

[[nodiscard]] render::BindlessTableId ResolveSampledImageTableId(
    const render::BindlessResourceSystem* bindless_resources_) noexcept {
    if (bindless_resources_ != nullptr) {
        const auto table_id = bindless_resources_->SampledImageTable();
        if (table_id.IsValid()) {
            return table_id;
        }
    }
    return render::BindlessResourceSystem::SampledImageTableContractId();
}

[[nodiscard]] render::BindlessTableId ResolveSamplerTableId(
    const render::BindlessResourceSystem* bindless_resources_) noexcept {
    if (bindless_resources_ != nullptr) {
        const auto table_id = bindless_resources_->SamplerTable();
        if (table_id.IsValid()) {
            return table_id;
        }
    }
    return render::BindlessResourceSystem::SamplerTableContractId();
}

[[nodiscard]] ecs::ParticleSimulationMode AccumulateRequestedSimulationMode(
    ecs::ParticleSimulationMode current_,
    ecs::ParticleSimulationMode candidate_) noexcept {
    if (candidate_ == ecs::ParticleSimulationMode::gpu ||
        current_ == ecs::ParticleSimulationMode::gpu) {
        return ecs::ParticleSimulationMode::gpu;
    }
    if (candidate_ == ecs::ParticleSimulationMode::hybrid_gpu ||
        current_ == ecs::ParticleSimulationMode::hybrid_gpu) {
        return ecs::ParticleSimulationMode::hybrid_gpu;
    }
    return ecs::ParticleSimulationMode::cpu;
}

[[nodiscard]] std::uint32_t SaturatingAdd(std::uint32_t lhs_,
                                          std::uint32_t rhs_) noexcept {
    const std::uint64_t sum = static_cast<std::uint64_t>(lhs_) + rhs_;
    return sum > static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)())
        ? (std::numeric_limits<std::uint32_t>::max)()
        : static_cast<std::uint32_t>(sum);
}

[[nodiscard]] ParticleSimulationPrepareDesc BuildSimulationPrepareDesc2D(
    const ecs::Particle<ecs::Dim2>* particle_components_,
    std::uint32_t component_count_) noexcept {
    ParticleSimulationPrepareDesc prepare_desc{};
    prepare_desc.requested_mode = ecs::ParticleSimulationMode::cpu;
    for (std::uint32_t index = 0U; index < component_count_; ++index) {
        const auto& component = particle_components_[index];

        prepare_desc.requested_mode = AccumulateRequestedSimulationMode(prepare_desc.requested_mode,
                                                                        component.style.simulation_mode);
        prepare_desc.particle_capacity = SaturatingAdd(prepare_desc.particle_capacity,
                                                       component.style.max_particles);
        prepare_desc.visible_particle_capacity = SaturatingAdd(prepare_desc.visible_particle_capacity,
                                                               component.style.max_particles);
        prepare_desc.spawn_packet_capacity = SaturatingAdd(prepare_desc.spawn_packet_capacity, 1U);
        prepare_desc.indirect_command_capacity = SaturatingAdd(prepare_desc.indirect_command_capacity, 1U);
        if (component.style.sort_mode == ecs::ParticleSortMode::gpu_radix) {
            prepare_desc.require_sort_buffers = true;
            prepare_desc.sort_key_capacity = SaturatingAdd(prepare_desc.sort_key_capacity,
                                                           component.style.max_particles);
        }
    }

    if (prepare_desc.requested_mode != ecs::ParticleSimulationMode::cpu) {
        prepare_desc.require_draw_instance_buffer = true;
        prepare_desc.draw_instance_stride_bytes = sizeof(ecs::Particle2DGpuInstance);
    }
    return prepare_desc;
}

} // namespace

std::size_t ParticleRenderer2D::BlendModeIndex(BlendModeKind blend_mode_) noexcept {
    return static_cast<std::size_t>(blend_mode_);
}

ParticleRenderer2D::BlendModeKind ParticleRenderer2D::DecodeBlendModeKind(
    std::uint32_t pipeline_state_) noexcept {
    switch (static_cast<ecs::RuntimeBlendPreset>(pipeline_state_ & 0xFFU)) {
    case ecs::RuntimeBlendPreset::additive:
        return BlendModeKind::additive;
    case ecs::RuntimeBlendPreset::multiply:
        return BlendModeKind::multiply;
    case ecs::RuntimeBlendPreset::premultiplied_alpha:
        return BlendModeKind::premultiplied_alpha;
    case ecs::RuntimeBlendPreset::screen:
        return BlendModeKind::screen;
    case ecs::RuntimeBlendPreset::alpha:
    default:
        return BlendModeKind::alpha;
    }
}

std::uint64_t ParticleRenderer2D::ComposeBindlessUploadRevision(
    const ecs::ParticleRuntimeBuildStats& runtime_stats_,
    std::uint32_t texture_revision_) noexcept {
    std::uint64_t revision = ParticleUploadHost::ComposeUploadRevision(
        runtime_stats_.component_signature,
        runtime_stats_.transform_signature,
        runtime_stats_.visible_signature,
        runtime_stats_.runtime_state_signature);
    revision ^= static_cast<std::uint64_t>(texture_revision_) + 0x9e3779b97f4a7c15ULL +
                (revision << 6U) + (revision >> 2U);
    return revision;
}

void ParticleRenderer2D::Initialize(const ParticleRenderer2DCreateInfo& create_info_) {
    create_info_cache = create_info_;
    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_particle_count > 0U) {
        ecs::ParticleRuntimeSystem<ecs::Dim2>::Reserve(runtime_scratch,
                                                       create_info_cache.reserve_component_count,
                                                       create_info_cache.reserve_particle_count);
    }

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& pipeline_id : pipeline_ids) {
        pipeline_id = {};
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;

    image_initialized.clear();
    last_runtime_build_stats = {};
    last_simulation_resources = {};
    last_gpu_build_result = {};
    last_upload_result = {};
    stats = {};

    particle_components = nullptr;
    particle_emitters = nullptr;
    transforms = nullptr;
    component_count = 0U;
    particle_upload_host = nullptr;
    particle_simulation_host = nullptr;
    texture_host = nullptr;
    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    bindless_resources = nullptr;
    pipeline_host = nullptr;
    sampler_host = nullptr;
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    gpu_build_active = false;
    graph_compute_pass_owned = false;
    graph_compute_pass_scheduled = false;
    graph_draw_instances_resource = render_graph::invalid_resource_handle;
    graph_draw_instances_version = render_graph::invalid_resource_version;
    graph_indirect_commands_resource = render_graph::invalid_resource_handle;
    graph_indirect_commands_version = render_graph::invalid_resource_version;
    initialized = true;
}

void ParticleRenderer2D::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(context_.Device());
    }

    particle_components = nullptr;
    particle_emitters = nullptr;
    transforms = nullptr;
    component_count = 0U;
    particle_upload_host = nullptr;
    particle_simulation_host = nullptr;
    texture_host = nullptr;
    context = nullptr;
    upload_host = nullptr;
    descriptor_host = nullptr;
    bindless_resources = nullptr;
    pipeline_host = nullptr;
    sampler_host = nullptr;

    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& pipeline_id : pipeline_ids) {
        pipeline_id = {};
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;

    image_initialized.clear();

    runtime_scratch.instances.clear();
    runtime_scratch.draw_batches.clear();
    runtime_scratch.build_component_indices.clear();
    runtime_scratch.cache = {};

    last_runtime_build_stats = {};
    last_simulation_resources = {};
    last_gpu_build_result = {};
    last_upload_result = {};
    stats = {};
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    gpu_build_active = false;
    graph_compute_pass_owned = false;
    graph_compute_pass_scheduled = false;
    graph_draw_instances_resource = render_graph::invalid_resource_handle;
    graph_draw_instances_version = render_graph::invalid_resource_version;
    graph_indirect_commands_resource = render_graph::invalid_resource_handle;
    graph_indirect_commands_version = render_graph::invalid_resource_version;
    initialized = false;
}

void ParticleRenderer2D::SetHost(ParticleUploadHost* upload_host_) noexcept {
    particle_upload_host = upload_host_;
}

void ParticleRenderer2D::SetSimulationHost(ParticleSimulationHost* simulation_host_) noexcept {
    particle_simulation_host = simulation_host_;
}

void ParticleRenderer2D::SetTextureHost(asset::TextureHost* texture_host_) noexcept {
    texture_host = texture_host_;
}

void ParticleRenderer2D::SetHosts(ParticleUploadHost* upload_host_,
                                  asset::TextureHost* texture_host_) noexcept {
    particle_upload_host = upload_host_;
    texture_host = texture_host_;
}

void ParticleRenderer2D::SetSceneData(ecs::Particle<ecs::Dim2>* particle_components_,
                                      ecs::ParticleEmitter<ecs::Dim2>* particle_emitters_,
                                      ecs::Transform<ecs::Dim2>* transforms_,
                                      std::uint32_t component_count_) noexcept {
    particle_components = particle_components_;
    particle_emitters = particle_emitters_;
    transforms = transforms_;
    component_count = component_count_;
}

void ParticleRenderer2D::PrepareFrame(const render::ParticleRenderer2DPrepareView& prepare_view_) {
    if (!initialized) {
        throw std::runtime_error("ParticleRenderer2D::PrepareFrame called before Initialize");
    }
    if (prepare_view_.device.EnabledVulkan13Features().dynamicRendering != VK_TRUE ||
        prepare_view_.device.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("ParticleRenderer2D requires Vulkan 1.3 dynamicRendering + synchronization2");
    }
    if (particle_upload_host == nullptr && prepare_view_.particle_upload != nullptr) {
        particle_upload_host = prepare_view_.particle_upload;
    }
    if (particle_simulation_host == nullptr && prepare_view_.particle_simulation != nullptr) {
        particle_simulation_host = prepare_view_.particle_simulation;
    }
    if (particle_upload_host == nullptr || !particle_upload_host->IsInitialized()) {
        throw std::runtime_error("ParticleRenderer2D::PrepareFrame requires initialized ParticleUploadHost");
    }
    if (particle_simulation_host != nullptr && !particle_simulation_host->IsInitialized()) {
        throw std::runtime_error("ParticleRenderer2D::PrepareFrame received non-initialized ParticleSimulationHost");
    }

    context = &prepare_view_.device;
    upload_host = &prepare_view_.upload;
    descriptor_host = &prepare_view_.descriptor;
    pipeline_host = &prepare_view_.pipeline;
    sampler_host = &prepare_view_.sampler;
    if (prepare_view_.texture != nullptr) {
        texture_host = prepare_view_.texture;
    }
    if (prepare_view_.bindless != nullptr) {
        bindless_resources = prepare_view_.bindless;
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "ParticleRenderer2D::PrepareFrame requires initialized BindlessResourceSystem");
    }

    active_frame_index = prepare_view_.frame.frame_index;
    swapchain_extent = prepare_view_.frame.swapchain_extent;
    swapchain_format = prepare_view_.frame.swapchain_format;
    last_submitted_value_seen = prepare_view_.progress.last_submitted_value;
    completed_submit_value_seen = prepare_view_.progress.completed_submit_value;

    stats = {};
    stats.component_count = component_count;
    last_runtime_build_stats = {};
    last_simulation_resources = {};
    last_gpu_build_result = {};
    last_upload_result = {};
    gpu_build_active = false;
    graph_compute_pass_owned = false;
    graph_compute_pass_scheduled = false;
    graph_draw_instances_resource = render_graph::invalid_resource_handle;
    graph_draw_instances_version = render_graph::invalid_resource_version;
    graph_indirect_commands_resource = render_graph::invalid_resource_handle;
    graph_indirect_commands_version = render_graph::invalid_resource_version;

    if (particle_components == nullptr || particle_emitters == nullptr || transforms == nullptr || component_count == 0U) {
        runtime_scratch.instances.clear();
        runtime_scratch.draw_batches.clear();
        runtime_scratch.build_component_indices.clear();
        return;
    }

    if (particle_simulation_host != nullptr) {
        particle_simulation_host->BeginFrame(*context,
                                             active_frame_index,
                                             prepare_view_.progress.last_submitted_value,
                                             prepare_view_.progress.completed_submit_value);
        const ParticleSimulationPrepareDesc prepare_desc =
            BuildSimulationPrepareDesc2D(particle_components,
                                         component_count);
        last_simulation_resources = particle_simulation_host->PrepareFrameResources(*context,
                                                                                    active_frame_index,
                                                                                    prepare_desc);
        if (last_simulation_resources.resolved_path != ParticleSimulationResolvedPath::cpu) {
            ecs::ParticleRuntimeBuildConfig build_config = create_info_cache.runtime_upload_options.runtime_build;
            build_config.build_instances = false;
            bool cpu_seeded_this_frame =
                last_simulation_resources.resolved_path != ParticleSimulationResolvedPath::gpu;
            if (last_simulation_resources.resolved_path == ParticleSimulationResolvedPath::gpu) {
                build_config.simulate = false;
                build_config.emit_new_particles = false;
            }
            last_runtime_build_stats = ecs::ParticleRuntimeSystem<ecs::Dim2>::Build(
                particle_components,
                particle_emitters,
                transforms,
                component_count,
                runtime_scratch,
                build_config,
                {});
            if (last_simulation_resources.resolved_path == ParticleSimulationResolvedPath::gpu &&
                !particle_simulation_host->HasPersistentState2D() &&
                particle_simulation_host->NeedsCpuSeed2D(active_frame_index)) {
                build_config = create_info_cache.runtime_upload_options.runtime_build;
                build_config.build_instances = false;
                cpu_seeded_this_frame = true;
                last_runtime_build_stats = ecs::ParticleRuntimeSystem<ecs::Dim2>::Build(
                    particle_components,
                    particle_emitters,
                    transforms,
                    component_count,
                    runtime_scratch,
                    build_config,
                    {});
            }
            last_gpu_build_result = particle_simulation_host->PrepareGpuBuild2D(
                *context,
                *upload_host,
                *descriptor_host,
                *pipeline_host,
                active_frame_index,
                last_simulation_resources,
                particle_components,
                particle_emitters,
                transforms,
                component_count,
                build_config,
                cpu_seeded_this_frame,
                runtime_scratch,
                last_runtime_build_stats,
                texture_host,
                *bindless_resources);
            gpu_build_active = last_gpu_build_result.used_gpu_build;
            graph_compute_pass_owned =
                gpu_build_active && prepare_view_.render_graph_compute_active;
        }
    }

    if (gpu_build_active) {
        stats.visible_component_count = last_runtime_build_stats.candidate_emitter_count;
        stats.emitter_count = last_runtime_build_stats.emitter_count;
        stats.active_particle_count = last_runtime_build_stats.active_particle_count;
        stats.visible_particle_count = last_runtime_build_stats.visible_particle_count;
        stats.draw_batch_count = last_runtime_build_stats.emitted_batch_count;
        stats.uploaded_instance_count = last_gpu_build_result.state_record_count;
        stats.uploaded_bytes =
            (last_gpu_build_result.state_upload.uploaded ? last_gpu_build_result.state_upload.size_bytes : 0U) +
            (last_gpu_build_result.spawn_upload.uploaded ? last_gpu_build_result.spawn_upload.size_bytes : 0U) +
            (last_gpu_build_result.indirect_upload.uploaded ? last_gpu_build_result.indirect_upload.size_bytes : 0U);
        stats.cache_reused = last_gpu_build_result.cache_reused &&
                             last_runtime_build_stats.visible_particle_count > 0U;
        stats.skipped_upload = !last_gpu_build_result.state_upload.uploaded &&
                               !last_gpu_build_result.indirect_upload.uploaded;
        return;
    }

    particle_upload_host->BeginFrame(*context,
                                     active_frame_index,
                                     prepare_view_.progress.last_submitted_value,
                                     prepare_view_.progress.completed_submit_value);

    last_upload_result.runtime = ecs::ParticleRuntimeSystem<ecs::Dim2>::Build(
        particle_components,
        particle_emitters,
        transforms,
        component_count,
        runtime_scratch,
        create_info_cache.runtime_upload_options.runtime_build,
        {});

    if (!runtime_scratch.instances.empty() &&
        last_upload_result.runtime.emitted_instance_count > 0U) {
        RemapCpuInstancesToBindless();
        last_upload_result.upload = particle_upload_host->Upload2DInstances(
            *context,
            *upload_host,
            active_frame_index,
            runtime_scratch.instances.data(),
            static_cast<std::uint32_t>(runtime_scratch.instances.size()),
            ComposeBindlessUploadRevision(
                last_upload_result.runtime,
                texture_host != nullptr ? texture_host->Stats().revision : 0U));
    }
    last_upload_result.skipped_upload = !last_upload_result.upload.uploaded;

    stats.visible_component_count = last_upload_result.runtime.candidate_emitter_count;
    stats.emitter_count = last_upload_result.runtime.emitter_count;
    stats.active_particle_count = last_upload_result.runtime.active_particle_count;
    stats.visible_particle_count = last_upload_result.runtime.visible_particle_count;
    stats.draw_batch_count = last_upload_result.runtime.emitted_batch_count;
    stats.uploaded_instance_count = last_upload_result.upload.element_count;
    stats.uploaded_bytes = last_upload_result.upload.uploaded ? last_upload_result.upload.size_bytes : 0U;
    stats.cache_reused = !last_upload_result.upload.uploaded &&
                         last_upload_result.runtime.emitted_instance_count > 0U;
    stats.skipped_upload = last_upload_result.skipped_upload;
}

void ParticleRenderer2D::BuildDirectRuntimeGraph(
    const render::RuntimeDirectGraphBuildView& graph_view_) {
    if (!initialized) {
        throw std::runtime_error(
            "ParticleRenderer2D::BuildDirectRuntimeGraph called before Initialize");
    }

    const auto pass = graph_view_.builder.AddPass("particle_renderer_2d_direct");
    graph_view_.present_ready_version = graph_view_.builder.Write(
        pass,
        graph_view_.present_target,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::color_attachment_write,
        });
    graph_view_.builder.SetRasterPassDesc(
        pass,
        render_graph::RasterPassDesc{
            .color_attachments = {
                render_graph::RasterColorAttachmentDesc{
                    .target = graph_view_.present_target,
                    .load_op = create_info_cache.clear_swapchain
                        ? render_graph::AttachmentLoadOp::clear
                        : render_graph::AttachmentLoadOp::load,
                    .store_op = render_graph::AttachmentStoreOp::store,
                    .clear_value = {
                        .red = create_info_cache.clear_color.float32[0],
                        .green = create_info_cache.clear_color.float32[1],
                        .blue = create_info_cache.clear_color.float32[2],
                        .alpha = create_info_cache.clear_color.float32[3],
                    },
                },
            },
        });
    DescribeGraphDescriptorBindings(graph_view_.builder, pass);
    graph_view_.builder.SetExecuteCallback(
        pass,
        [this, color_target = graph_view_.present_target](render_graph::GraphCommandContext& context_) {
            RecordGraphOverlay(context_, color_target);
        });
}

void ParticleRenderer2D::DescribeGraphDescriptorBindings(render_graph::RenderGraphBuilder& builder_,
                                                         const render_graph::PassHandle pass_) const {
    if (!initialized) {
        throw std::runtime_error(
            "ParticleRenderer2D::DescribeGraphDescriptorBindings called before Initialize");
    }

    const_cast<ParticleRenderer2D*>(this)->ScheduleGraphComputeBuild(builder_, pass_);

    const auto sampled_image_table = ResolveSampledImageTableId(bindless_resources);
    const auto sampler_table = ResolveSamplerTableId(bindless_resources);
    builder_.SetPassShaderContract(
        pass_,
        render_graph::MakeSharedBindlessFragmentShaderContract("particle_2d.frag"));
    builder_.AddBindlessTableBinding(pass_,
                                     0U,
                                     render_graph::DescriptorBindingKind::sampled_image_table,
                                     sampled_image_table.value,
                                     render_graph::shader_stage_fragment_flag);
    builder_.AddBindlessTableBinding(pass_,
                                     1U,
                                     render_graph::DescriptorBindingKind::sampler_table,
                                     sampler_table.value,
                                     render_graph::shader_stage_fragment_flag);
}

void ParticleRenderer2D::RegisterGraphImportedResources(
    runtime::services::RenderGraphRuntimeService& graph_runtime_service_) const {
    if (!graph_compute_pass_owned ||
        !render_graph::IsValidResourceHandle(graph_draw_instances_resource) ||
        !render_graph::IsValidResourceHandle(graph_indirect_commands_resource)) {
        return;
    }

    if (last_gpu_build_result.resources.draw_instances.buffer != VK_NULL_HANDLE &&
        last_gpu_build_result.resources.draw_instances.size_bytes != 0U) {
        graph_runtime_service_.RegisterDirectImportedBuffer(
            graph_draw_instances_resource,
            render_graph::ImportedBufferBinding{
                .buffer = last_gpu_build_result.resources.draw_instances.buffer,
                .size_bytes = last_gpu_build_result.resources.draw_instances.size_bytes,
                .usage = k_graph_particle_draw_instances_imported_usage,
            });
    }
    if (last_gpu_build_result.resources.indirect_commands.buffer != VK_NULL_HANDLE &&
        last_gpu_build_result.resources.indirect_commands.size_bytes != 0U) {
        graph_runtime_service_.RegisterDirectImportedBuffer(
            graph_indirect_commands_resource,
            render_graph::ImportedBufferBinding{
                .buffer = last_gpu_build_result.resources.indirect_commands.buffer,
                .size_bytes = last_gpu_build_result.resources.indirect_commands.size_bytes,
                .usage = k_graph_particle_indirect_commands_imported_usage,
            });
    }
}

void ParticleRenderer2D::ScheduleGraphComputeBuild(render_graph::RenderGraphBuilder& builder_,
                                                   const render_graph::PassHandle pass_) {
    if (!graph_compute_pass_owned) {
        return;
    }
    const auto append_overlay_reads = [&](const VkDeviceSize draw_instances_size_,
                                          const VkDeviceSize indirect_commands_size_) {
        (void)builder_.Read(
            pass_,
            graph_draw_instances_version,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::vertex_buffer_read,
                .buffer_range = {
                    .offset_bytes = 0U,
                    .size_bytes = draw_instances_size_,
                },
            });
        (void)builder_.Read(
            pass_,
            graph_indirect_commands_version,
            render_graph::AccessDesc{
                .access = render_graph::AccessKind::indirect_command_read,
                .buffer_range = {
                    .offset_bytes = 0U,
                    .size_bytes = indirect_commands_size_,
                },
            });
    };
    if (graph_compute_pass_scheduled) {
        if (render_graph::IsValidResourceVersionHandle(graph_draw_instances_version) &&
            render_graph::IsValidResourceVersionHandle(graph_indirect_commands_version)) {
            append_overlay_reads(last_gpu_build_result.resources.draw_instances.size_bytes,
                                 last_gpu_build_result.resources.indirect_commands.size_bytes);
        }
        return;
    }
    if (last_gpu_build_result.resources.draw_instances.buffer == VK_NULL_HANDLE ||
        last_gpu_build_result.resources.indirect_commands.buffer == VK_NULL_HANDLE ||
        last_gpu_build_result.resources.draw_instances.size_bytes == 0U ||
        last_gpu_build_result.resources.indirect_commands.size_bytes == 0U ||
        particle_simulation_host == nullptr) {
        graph_compute_pass_owned = false;
        return;
    }

    graph_draw_instances_resource = builder_.CreateBuffer(
        "particle_2d_draw_instances",
        render_graph::BufferDesc{
            .size_bytes = last_gpu_build_result.resources.draw_instances.size_bytes,
            .usage = render_graph::buffer_usage_storage_flag |
                     render_graph::buffer_usage_vertex_flag |
                     render_graph::buffer_usage_transfer_dst_flag,
        },
        render_graph::ResourceLifetime::imported);
    graph_indirect_commands_resource = builder_.CreateBuffer(
        "particle_2d_indirect_commands",
        render_graph::BufferDesc{
            .size_bytes = last_gpu_build_result.resources.indirect_commands.size_bytes,
            .usage = render_graph::buffer_usage_storage_flag |
                     render_graph::buffer_usage_indirect_flag |
                     render_graph::buffer_usage_transfer_dst_flag,
        },
        render_graph::ResourceLifetime::imported);

    const auto compute_pass = builder_.AddPass("particle_2d_gpu_build",
                                               false,
                                               render_graph::QueueClass::compute);
    graph_draw_instances_version = builder_.Write(
        compute_pass,
        graph_draw_instances_resource,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::shader_storage_write,
            .buffer_range = {
                .offset_bytes = 0U,
                .size_bytes = last_gpu_build_result.resources.draw_instances.size_bytes,
            },
        });
    graph_indirect_commands_version = builder_.Write(
        compute_pass,
        graph_indirect_commands_resource,
        render_graph::AccessDesc{
            .access = render_graph::AccessKind::shader_storage_write,
            .buffer_range = {
                .offset_bytes = 0U,
                .size_bytes = last_gpu_build_result.resources.indirect_commands.size_bytes,
            },
        });
    builder_.SetExecuteCallback(
        compute_pass,
        [this, frame_index = active_frame_index](render_graph::GraphCommandContext& context_) {
            if (particle_simulation_host == nullptr) {
                return;
            }
            particle_simulation_host->RecordBuild2D(*context,
                                                    *pipeline_host,
                                                    frame_index,
                                                    context_.CommandBuffer());
        });
    append_overlay_reads(last_gpu_build_result.resources.draw_instances.size_bytes,
                         last_gpu_build_result.resources.indirect_commands.size_bytes);
    graph_compute_pass_scheduled = true;
}

void ParticleRenderer2D::RecordGraphOverlay(render_graph::GraphCommandContext& context_,
                                            render_graph::ResourceHandle color_target_) {
    RecordGraphInternal(context_, color_target_);
}

void ParticleRenderer2D::RecordGraphInternal(render_graph::GraphCommandContext& context_,
                                             render_graph::ResourceHandle color_target_) {
    if (!initialized) {
        throw std::runtime_error("ParticleRenderer2D::RecordGraphOverlay called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr) {
        throw std::runtime_error("ParticleRenderer2D::RecordGraphOverlay called before PrepareFrame");
    }
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "ParticleRenderer2D::RecordGraphOverlay requires initialized BindlessResourceSystem");
    }
    if (context_.CommandBuffer() == VK_NULL_HANDLE) {
        throw std::runtime_error("ParticleRenderer2D::RecordGraphOverlay requires valid command buffer");
    }

    const auto resolved_color = context_.ResolveTextureView(color_target_);
    const VkExtent2D render_extent{resolved_color.extent.width, resolved_color.extent.height};
    if (render_extent.width == 0U || render_extent.height == 0U) {
        throw std::runtime_error("ParticleRenderer2D::RecordGraphOverlay resolved zero-sized render extent");
    }

    EnsurePipelineObjects(*context,
                          *bindless_resources,
                          *pipeline_host,
                          resolved_color.format);

    if (gpu_build_active &&
        particle_simulation_host != nullptr &&
        !graph_compute_pass_owned) {
        particle_simulation_host->RecordBuild2D(*context,
                                                *pipeline_host,
                                                active_frame_index,
                                                context_.CommandBuffer());
    }

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(render_extent.width);
    viewport.height = static_cast<float>(render_extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(context_.CommandBuffer(), 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = VkOffset2D{0, 0};
    scissor.extent = render_extent;
    vkCmdSetScissor(context_.CommandBuffer(), 0U, 1U, &scissor);

    RecordDrawBatches(context_.CommandBuffer(),
                      render_extent,
                      resolved_color.format,
                      &context_);
}

void ParticleRenderer2D::RecordDrawBatches(VkCommandBuffer command_buffer_,
                                           VkExtent2D render_extent_,
                                           VkFormat color_format_,
                                           const render_graph::GraphCommandContext* graph_context_) {
    if (((gpu_build_active &&
          last_gpu_build_result.resources.draw_instances.buffer != VK_NULL_HANDLE) ||
         (last_upload_result.upload.buffer != VK_NULL_HANDLE)) &&
        !runtime_scratch.draw_batches.empty()) {
        const VkBuffer vertex_buffer = gpu_build_active
            ? last_gpu_build_result.resources.draw_instances.buffer
            : last_upload_result.upload.buffer;
        const VkDeviceSize vertex_offset = gpu_build_active
            ? 0U
            : last_upload_result.upload.offset;
        vkCmdBindVertexBuffers(command_buffer_,
                               0U,
                               1U,
                               &vertex_buffer,
                               &vertex_offset);

        PushConstants push_constants{};
        push_constants.viewport_width = static_cast<float>(render_extent_.width);
        push_constants.viewport_height = static_cast<float>(render_extent_.height);
        push_constants.inv_viewport_width_2x =
            (render_extent_.width > 0U)
                ? (2.0F / static_cast<float>(render_extent_.width))
                : 0.0F;
        push_constants.inv_viewport_height_2x =
            (render_extent_.height > 0U)
                ? (2.0F / static_cast<float>(render_extent_.height))
                : 0.0F;
        push_constants.params = 0U;
        push_constants.params |= create_info_cache.input_positions_pixel_space ? 0x1U : 0U;
        push_constants.params |= create_info_cache.pixel_space_origin_top_left ? 0x2U : 0U;
        push_constants.reserved0 = 0U;
        push_constants.reserved1 = 0U;
        push_constants.reserved2 = 0U;

        const VkPipelineLayout pipeline_layout = pipeline_host->GetPipelineLayout(pipeline_layout_id);
        if (graph_context_ != nullptr) {
            graph_context_->BindCurrentPassDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                          pipeline_layout,
                                                          0U,
                                                          2U);
        } else {
            const std::array<VkDescriptorSet, 2U> bindless_sets{
                bindless_resources->SampledImageSet(),
                bindless_resources->SamplerSet()
            };
            if (bindless_sets[0U] == VK_NULL_HANDLE || bindless_sets[1U] == VK_NULL_HANDLE) {
                throw std::runtime_error(
                    "ParticleRenderer2D::RecordDrawBatches requires valid bindless descriptor sets");
            }
            vkCmdBindDescriptorSets(command_buffer_,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    pipeline_layout,
                                    0U,
                                    static_cast<std::uint32_t>(bindless_sets.size()),
                                    bindless_sets.data(),
                                    0U,
                                    nullptr);
        }
        ++stats.descriptor_set_bind_count;

        render::GraphicsPipelineId bound_pipeline{};
        std::uint32_t batch_index = 0U;
        for (const ecs::ParticleDrawBatch& batch : runtime_scratch.draw_batches) {
            if (batch.instance_count == 0U) {
                ++batch_index;
                continue;
            }

            const BlendModeKind blend_mode = DecodeBlendModeKind(batch.pipeline_state);
            const render::GraphicsPipelineId pipeline_id = EnsurePipelineForBlendMode(
                *context,
                *pipeline_host,
                color_format_,
                blend_mode);
            if (!pipeline_id.IsValid()) {
                ++stats.skipped_batch_count;
                ++batch_index;
                continue;
            }

            if (bound_pipeline.value != pipeline_id.value) {
                vkCmdBindPipeline(command_buffer_,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline_host->GetGraphicsPipeline(pipeline_id));
                vkCmdPushConstants(command_buffer_,
                                   pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);
                bound_pipeline = pipeline_id;
            }

            if (gpu_build_active) {
                const VkDeviceSize indirect_offset =
                    static_cast<VkDeviceSize>(batch_index) * sizeof(ParticleGpuIndirectCommand);
                vkCmdDrawIndirect(command_buffer_,
                                  last_gpu_build_result.resources.indirect_commands.buffer,
                                  indirect_offset,
                                  1U,
                                  sizeof(ParticleGpuIndirectCommand));
                ++stats.indirect_draw_count;
            } else {
                vkCmdDraw(command_buffer_,
                          6U,
                          batch.instance_count,
                          0U,
                          batch.instance_begin);
            }
            ++stats.draw_call_count;
            ++batch_index;
        }
    }
}

void ParticleRenderer2D::OnSwapchainRecreated(std::uint32_t image_count_,
                                              VkExtent2D extent_,
                                              VkFormat format_) {
    OnSwapchainRecreated(image_count_,
                         extent_,
                         format_,
                         last_submitted_value_seen,
                         completed_submit_value_seen);
}

void ParticleRenderer2D::OnSwapchainRecreated(std::uint32_t image_count_,
                                              VkExtent2D extent_,
                                              VkFormat format_,
                                              std::uint64_t last_submitted_value_,
                                              std::uint64_t completed_submit_value_) {
    swapchain_extent = extent_;
    swapchain_format = format_;
    last_submitted_value_seen = last_submitted_value_;
    completed_submit_value_seen = completed_submit_value_;
    image_initialized.clear();
    image_initialized.resize(image_count_);
    for (auto& initialized_flag : image_initialized) {
        initialized_flag = 0U;
    }
}

bool ParticleRenderer2D::IsInitialized() const noexcept {
    return initialized;
}

const ParticleRenderer2DStats& ParticleRenderer2D::Stats() const noexcept {
    return stats;
}

void ParticleRenderer2D::EnsurePipelineObjects(VulkanContext& context_,
                                               render::BindlessResourceSystem& bindless_resources_,
                                               render::PipelineHost& pipeline_host_,
                                               VkFormat color_format_) {
    if (context_.EnabledVulkan13Features().dynamicRendering != VK_TRUE ||
        context_.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("ParticleRenderer2D requires Vulkan 1.3 dynamicRendering + synchronization2");
    }

    const render::PipelineHostStats& pipeline_stats = pipeline_host_.Stats();
    if (shader_vertex_id.IsValid() && pipeline_stats.shader_module_count < shader_vertex_id.value) {
        shader_vertex_id = {};
    }
    if (shader_fragment_id.IsValid() && pipeline_stats.shader_module_count < shader_fragment_id.value) {
        shader_fragment_id = {};
    }
    if (pipeline_layout_id.IsValid() && pipeline_stats.pipeline_layout_count < pipeline_layout_id.value) {
        pipeline_layout_id = {};
    }
    for (auto& pipeline_id : pipeline_ids) {
        if (pipeline_id.IsValid() && pipeline_stats.graphics_pipeline_count < pipeline_id.value) {
            pipeline_id = {};
        }
    }

    if (!shader_vertex_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_particle_2d_vert_spv;
        shader_create_info.word_count = std::size(generated::k_particle_2d_vert_spv);
        shader_vertex_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }
    if (!shader_fragment_id.IsValid()) {
        render::ShaderModuleCreateInfo shader_create_info{};
        shader_create_info.code_words = generated::k_particle_2d_frag_spv;
        shader_create_info.word_count = std::size(generated::k_particle_2d_frag_spv);
        shader_fragment_id = pipeline_host_.RegisterShaderModule(context_, shader_create_info);
    }

    if (!pipeline_layout_id.IsValid()) {
        const VkDescriptorSetLayout sampled_image_layout =
            bindless_resources_.SampledImageLayout();
        const VkDescriptorSetLayout sampler_layout =
            bindless_resources_.SamplerLayout();
        if (sampled_image_layout == VK_NULL_HANDLE || sampler_layout == VK_NULL_HANDLE) {
            throw std::runtime_error(
                "ParticleRenderer2D::EnsurePipelineObjects requires valid bindless set layouts");
        }
        render::PipelineLayoutDesc pipeline_layout_desc{};
        pipeline_layout_desc.set_layouts.push_back(sampled_image_layout);
        pipeline_layout_desc.set_layouts.push_back(sampler_layout);
        pipeline_layout_desc.push_constant_ranges.push_back({
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0U,
            .size = sizeof(PushConstants)
        });
        pipeline_layout_id = pipeline_host_.RegisterPipelineLayout(context_, pipeline_layout_desc);
    }

    if (pipeline_color_format != color_format_) {
        for (auto& pipeline_id : pipeline_ids) {
            pipeline_id = {};
        }
        pipeline_color_format = color_format_;
    }
}

render::GraphicsPipelineId ParticleRenderer2D::EnsurePipelineForBlendMode(
    VulkanContext& context_,
    render::PipelineHost& pipeline_host_,
    VkFormat color_format_,
    BlendModeKind blend_mode_) {
    EnsurePipelineObjects(context_, *bindless_resources, pipeline_host_, color_format_);

    render::GraphicsPipelineId& cached_pipeline_id = pipeline_ids[BlendModeIndex(blend_mode_)];
    if (cached_pipeline_id.IsValid() && pipeline_color_format == color_format_) {
        return cached_pipeline_id;
    }

    render::GraphicsPipelineDesc pipeline_desc{};
    pipeline_desc.layout = pipeline_host_.GetPipelineLayout(pipeline_layout_id);
    pipeline_desc.use_dynamic_rendering = true;
    pipeline_desc.rendering.color_attachment_formats.push_back(color_format_);

    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = pipeline_host_.GetShaderModule(shader_vertex_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });
    pipeline_desc.shader_stages.push_back({
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = pipeline_host_.GetShaderModule(shader_fragment_id),
        .entry_name = "main",
        .flags = 0U,
        .specialization = {}
    });

    pipeline_desc.vertex_input.bindings.push_back({
        .binding = 0U,
        .stride = static_cast<std::uint32_t>(sizeof(ecs::Particle2DGpuInstance)),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 0U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle2DGpuInstance, position_x))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 1U,
        .binding = 0U,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle2DGpuInstance, size_x))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 2U,
        .binding = 0U,
        .format = VK_FORMAT_R32_SFLOAT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle2DGpuInstance, rotation_radians))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 3U,
        .binding = 0U,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle2DGpuInstance, color_rgba8))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 4U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle2DGpuInstance, texture_slot))
    });
    pipeline_desc.vertex_input.attributes.push_back({
        .location = 5U,
        .binding = 0U,
        .format = VK_FORMAT_R32_UINT,
        .offset = static_cast<std::uint32_t>(offsetof(ecs::Particle2DGpuInstance, sampler_slot))
    });

    pipeline_desc.input_assembly.topology = k_particle_topology;
    pipeline_desc.input_assembly.primitive_restart_enable = false;

    pipeline_desc.viewport.viewport_count = 1U;
    pipeline_desc.viewport.scissor_count = 1U;
    pipeline_desc.dynamic.states.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    pipeline_desc.dynamic.states.push_back(VK_DYNAMIC_STATE_SCISSOR);

    pipeline_desc.rasterization.cull_mode = VK_CULL_MODE_NONE;
    pipeline_desc.rasterization.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    pipeline_desc.rasterization.polygon_mode = VK_POLYGON_MODE_FILL;
    pipeline_desc.rasterization.line_width = 1.0F;

    pipeline_desc.multisample.rasterization_samples = VK_SAMPLE_COUNT_1_BIT;

    render::ColorBlendPreset blend_preset = render::ColorBlendPreset::alpha;
    switch (blend_mode_) {
    case BlendModeKind::additive:
        blend_preset = render::ColorBlendPreset::additive;
        break;
    case BlendModeKind::multiply:
        blend_preset = render::ColorBlendPreset::multiply;
        break;
    case BlendModeKind::premultiplied_alpha:
        blend_preset = render::ColorBlendPreset::premultiplied_alpha;
        break;
    case BlendModeKind::screen:
        blend_preset = render::ColorBlendPreset::screen;
        break;
    case BlendModeKind::alpha:
    default:
        blend_preset = render::ColorBlendPreset::alpha;
        break;
    }
    const VkPipelineColorBlendAttachmentState blend_attachment =
        render::BuildColorBlendAttachment(blend_preset);
    pipeline_desc.color_blend.attachments.push_back(blend_attachment);

    cached_pipeline_id = pipeline_host_.RegisterGraphicsPipeline(context_, pipeline_desc);
    pipeline_color_format = color_format_;
    return cached_pipeline_id;
}

void ParticleRenderer2D::RemapCpuInstancesToBindless() {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        throw std::runtime_error(
            "ParticleRenderer2D::RemapCpuInstancesToBindless requires initialized bindless resources");
    }
    for (auto& instance : runtime_scratch.instances) {
        const std::uint32_t raw_texture_id = instance.texture_slot;
        instance.texture_slot = ResolveTextureSlot(raw_texture_id);
        instance.sampler_slot = ResolveSamplerSlot(raw_texture_id);
    }
}

std::uint32_t ParticleRenderer2D::ResolveTextureSlot(std::uint32_t texture_id_) const noexcept {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        return 0U;
    }
    if (texture_id_ == 0U || texture_host == nullptr || !texture_host->IsInitialized()) {
        return bindless_resources->PlaceholderImageSlot().index;
    }
    return bindless_resources->ResolveTextureImageSlot(*texture_host,
                                                       asset::TextureId{texture_id_}).index;
}

std::uint32_t ParticleRenderer2D::ResolveSamplerSlot(std::uint32_t texture_id_) const noexcept {
    if (bindless_resources == nullptr || !bindless_resources->IsInitialized()) {
        return 0U;
    }
    if (texture_id_ == 0U || texture_host == nullptr || !texture_host->IsInitialized()) {
        return bindless_resources->DefaultSamplerSlot().index;
    }
    return bindless_resources->ResolveTextureSamplerSlot(*texture_host,
                                                         asset::TextureId{texture_id_}).index;
}

} // namespace vr::particle


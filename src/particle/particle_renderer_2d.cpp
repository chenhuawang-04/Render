#include "vr/particle/particle_renderer_2d.hpp"

#include "vr/asset/texture_host.hpp"
#include "vr/ecs/system/transparency_render_policy.hpp"
#include "vr/particle/generated/particle_2d_frag_spv.hpp"
#include "vr/particle/generated/particle_2d_vert_spv.hpp"
#include "vr/particle/particle_simulation_host.hpp"
#include "vr/render/color_blend_state.hpp"
#include "vr/render/render_loop_host.hpp"
#include "vr/render/runtime_prepare_views.hpp"
#include "vr/render/upload_host.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/resource/gpu_memory_host.hpp"
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

std::size_t ParticleRenderer2D::LowerBoundTextureSetIndex(
    const ParticleRenderer2DMcVector<TextureSetEntry>& entries_,
    std::uint32_t texture_id_) noexcept {
    std::size_t first = 0U;
    std::size_t count = entries_.size();
    while (count > 0U) {
        const std::size_t step = count / 2U;
        const std::size_t it = first + step;
        if (entries_[it].texture_id < texture_id_) {
            first = it + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

void ParticleRenderer2D::Initialize(const ParticleRenderer2DCreateInfo& create_info_) {
    create_info_cache = create_info_;
    if (create_info_cache.reserve_component_count > 0U ||
        create_info_cache.reserve_particle_count > 0U) {
        ecs::ParticleRuntimeSystem<ecs::Dim2>::Reserve(runtime_scratch,
                                                       create_info_cache.reserve_component_count,
                                                       create_info_cache.reserve_particle_count);
    }

    descriptor_image_write_scratch.reserve(1U);
    descriptor_buffer_write_scratch.reserve(0U);
    descriptor_texel_write_scratch.reserve(0U);

    descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& pipeline_id : pipeline_ids) {
        pipeline_id = {};
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;

    frame_texture_sets.clear();
    fallback_texture = {};
    fallback_texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    texture_sampler_id = {};
    image_initialized.clear();
    output_target_config = {};
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
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;
    sampler_host = nullptr;
    active_frame_index = 0U;
    swapchain_extent = {};
    swapchain_format = VK_FORMAT_UNDEFINED;
    last_submitted_value_seen = 0U;
    completed_submit_value_seen = 0U;
    gpu_build_active = false;
    initialized = true;
}

void ParticleRenderer2D::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        (void)vkDeviceWaitIdle(context_.Device());
    }

    if (context_.Device() != VK_NULL_HANDLE) {
        resource::ImageHost::DestroyImage(context_, fallback_texture);
    } else {
        fallback_texture = {};
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
    pipeline_host = nullptr;
    gpu_memory_host = nullptr;
    sampler_host = nullptr;

    descriptor_layout_id = {};
    pipeline_layout_id = {};
    shader_vertex_id = {};
    shader_fragment_id = {};
    for (auto& pipeline_id : pipeline_ids) {
        pipeline_id = {};
    }
    pipeline_color_format = VK_FORMAT_UNDEFINED;

    frame_texture_sets.clear();
    descriptor_buffer_write_scratch.clear();
    descriptor_image_write_scratch.clear();
    descriptor_texel_write_scratch.clear();
    fallback_texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    texture_sampler_id = {};
    image_initialized.clear();
    output_target_config = {};

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

void ParticleRenderer2D::SetOutputTargetConfig(
    const render::RenderTargetColorOutputConfig& output_target_config_) noexcept {
    output_target_config = output_target_config_;
}

void ParticleRenderer2D::ResetOutputTargetConfig() noexcept {
    output_target_config = {};
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
    gpu_memory_host = &prepare_view_.gpu_memory;
    sampler_host = &prepare_view_.sampler;
    if (prepare_view_.texture != nullptr) {
        texture_host = prepare_view_.texture;
    }

    active_frame_index = prepare_view_.frame.frame_index;
    swapchain_extent = prepare_view_.frame.swapchain_extent;
    swapchain_format = prepare_view_.frame.swapchain_format;
    last_submitted_value_seen = prepare_view_.progress.last_submitted_value;
    completed_submit_value_seen = prepare_view_.progress.completed_submit_value;

    EnsureFallbackTexture(*context, *upload_host, active_frame_index);

    if (active_frame_index >= frame_texture_sets.size()) {
        frame_texture_sets.resize(active_frame_index + 1U);
    }
    frame_texture_sets[active_frame_index].clear();

    stats = {};
    stats.component_count = component_count;
    last_runtime_build_stats = {};
    last_simulation_resources = {};
    last_gpu_build_result = {};
    last_upload_result = {};
    gpu_build_active = false;

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
                last_runtime_build_stats);
            gpu_build_active = last_gpu_build_result.used_gpu_build;
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

    last_upload_result = particle_upload_host->PrepareRuntimeAndUpload2D(
        *context,
        *upload_host,
        active_frame_index,
        prepare_view_.progress.last_submitted_value,
        prepare_view_.progress.completed_submit_value,
        particle_components,
        particle_emitters,
        transforms,
        component_count,
        runtime_scratch,
        {},
        create_info_cache.runtime_upload_options);

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

void ParticleRenderer2D::Record(const render::FrameRecordContext& record_context_) {
    if (!initialized) {
        throw std::runtime_error("ParticleRenderer2D::Record called before Initialize");
    }
    if (context == nullptr || descriptor_host == nullptr || pipeline_host == nullptr) {
        throw std::runtime_error("ParticleRenderer2D::Record called before PrepareFrame");
    }
    if (record_context_.command_buffer == VK_NULL_HANDLE ||
        record_context_.image == VK_NULL_HANDLE ||
        record_context_.image_view == VK_NULL_HANDLE) {
        throw std::runtime_error("ParticleRenderer2D::Record requires valid frame context image handles");
    }
    if (record_context_.extent.width == 0U || record_context_.extent.height == 0U) {
        throw std::runtime_error("ParticleRenderer2D::Record received zero-sized swapchain extent");
    }

    if (record_context_.image_index >= image_initialized.size()) {
        const std::size_t previous_size = image_initialized.size();
        image_initialized.resize(record_context_.image_index + 1U);
        for (std::size_t index = previous_size; index < image_initialized.size(); ++index) {
            image_initialized[index] = 0U;
        }
    }
    const bool has_previous_content = image_initialized[record_context_.image_index] != 0U;

    const render::ResolvedColorRenderPass color_pass = render::BuildColorRenderPass(
        record_context_,
        output_target_config,
        create_info_cache.clear_swapchain,
        create_info_cache.clear_color,
        has_previous_content);
    EnsurePipelineObjects(*context, *descriptor_host, *pipeline_host, color_pass.target.format);

    if (gpu_build_active && particle_simulation_host != nullptr) {
        particle_simulation_host->RecordBuild2D(*context,
                                                *pipeline_host,
                                                active_frame_index,
                                                record_context_.command_buffer);
    }

    vkCmdBeginRendering(record_context_.command_buffer, color_pass.rendering_info.VkInfoPtr());

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(color_pass.target.extent.width);
    viewport.height = static_cast<float>(color_pass.target.extent.height);
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(record_context_.command_buffer, 0U, 1U, &viewport);

    VkRect2D scissor{};
    scissor.offset = VkOffset2D{0, 0};
    scissor.extent = color_pass.target.extent;
    vkCmdSetScissor(record_context_.command_buffer, 0U, 1U, &scissor);

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
        vkCmdBindVertexBuffers(record_context_.command_buffer,
                               0U,
                               1U,
                               &vertex_buffer,
                               &vertex_offset);

        PushConstants push_constants{};
        push_constants.viewport_width = static_cast<float>(color_pass.target.extent.width);
        push_constants.viewport_height = static_cast<float>(color_pass.target.extent.height);
        push_constants.inv_viewport_width_2x =
            (color_pass.target.extent.width > 0U)
                ? (2.0F / static_cast<float>(color_pass.target.extent.width))
                : 0.0F;
        push_constants.inv_viewport_height_2x =
            (color_pass.target.extent.height > 0U)
                ? (2.0F / static_cast<float>(color_pass.target.extent.height))
                : 0.0F;
        push_constants.params = 0U;
        push_constants.params |= create_info_cache.input_positions_pixel_space ? 0x1U : 0U;
        push_constants.params |= create_info_cache.pixel_space_origin_top_left ? 0x2U : 0U;

        render::GraphicsPipelineId bound_pipeline{};
        VkDescriptorSet bound_descriptor_set = VK_NULL_HANDLE;

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
                color_pass.target.format,
                blend_mode);
            if (!pipeline_id.IsValid()) {
                ++stats.skipped_batch_count;
                continue;
            }

            const VkDescriptorSet descriptor_set = AcquireTextureDescriptorSet(active_frame_index,
                                                                               batch.texture_id);
            if (descriptor_set == VK_NULL_HANDLE) {
                ++stats.skipped_batch_count;
                continue;
            }

            if (bound_pipeline.value != pipeline_id.value) {
                vkCmdBindPipeline(record_context_.command_buffer,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline_host->GetGraphicsPipeline(pipeline_id));
                vkCmdPushConstants(record_context_.command_buffer,
                                   pipeline_host->GetPipelineLayout(pipeline_layout_id),
                                   VK_SHADER_STAGE_VERTEX_BIT,
                                   0U,
                                   sizeof(PushConstants),
                                   &push_constants);
                bound_pipeline = pipeline_id;
                bound_descriptor_set = VK_NULL_HANDLE;
            }

            if (bound_descriptor_set != descriptor_set) {
                vkCmdBindDescriptorSets(record_context_.command_buffer,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        pipeline_host->GetPipelineLayout(pipeline_layout_id),
                                        0U,
                                        1U,
                                        &descriptor_set,
                                        0U,
                                        nullptr);
                bound_descriptor_set = descriptor_set;
                ++stats.descriptor_set_bind_count;
            }

            if (gpu_build_active) {
                const VkDeviceSize indirect_offset =
                    static_cast<VkDeviceSize>(batch_index) * sizeof(ParticleGpuIndirectCommand);
                vkCmdDrawIndirect(record_context_.command_buffer,
                                  last_gpu_build_result.resources.indirect_commands.buffer,
                                  indirect_offset,
                                  1U,
                                  sizeof(ParticleGpuIndirectCommand));
                ++stats.indirect_draw_count;
            } else {
                vkCmdDraw(record_context_.command_buffer,
                          6U,
                          batch.instance_count,
                          0U,
                          batch.instance_begin);
            }
            ++stats.draw_call_count;
            ++batch_index;
        }
    }

    vkCmdEndRendering(record_context_.command_buffer);
    render::RecordEndColorPass(record_context_, output_target_config);
    image_initialized[record_context_.image_index] = 1U;
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
                                               render::DescriptorHost& descriptor_host_,
                                               render::PipelineHost& pipeline_host_,
                                               VkFormat color_format_) {
    if (context_.EnabledVulkan13Features().dynamicRendering != VK_TRUE ||
        context_.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("ParticleRenderer2D requires Vulkan 1.3 dynamicRendering + synchronization2");
    }

    if (descriptor_layout_id.IsValid() &&
        descriptor_host_.CachedLayoutCount() < descriptor_layout_id.value) {
        descriptor_layout_id = {};
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

    if (!descriptor_layout_id.IsValid()) {
        render::DescriptorSetLayoutDesc layout_desc{};
        layout_desc.bindings.push_back({
            .binding = 0U,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1U,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr
        });
        descriptor_layout_id = descriptor_host_.RegisterLayout(context_, layout_desc);
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
        render::PipelineLayoutDesc pipeline_layout_desc{};
        pipeline_layout_desc.set_layouts.push_back(descriptor_host_.GetLayout(descriptor_layout_id));
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
    EnsurePipelineObjects(context_, *descriptor_host, pipeline_host_, color_format_);

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

void ParticleRenderer2D::EnsureFallbackTexture(VulkanContext& context_,
                                               render::UploadHost& upload_host_,
                                               std::uint32_t frame_index_) {
    if (sampler_host == nullptr) {
        throw std::runtime_error("ParticleRenderer2D::EnsureFallbackTexture missing SamplerHost");
    }
    if (gpu_memory_host == nullptr) {
        throw std::runtime_error("ParticleRenderer2D::EnsureFallbackTexture missing GpuMemoryHost");
    }
    if (context_.EnabledVulkan13Features().synchronization2 != VK_TRUE) {
        throw std::runtime_error("ParticleRenderer2D::EnsureFallbackTexture requires synchronization2");
    }

    if (!texture_sampler_id.IsValid()) {
        resource::SamplerDesc sampler_desc{};
        sampler_desc.mag_filter = VK_FILTER_LINEAR;
        sampler_desc.min_filter = VK_FILTER_LINEAR;
        sampler_desc.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        sampler_desc.address_mode_u = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.address_mode_v = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.address_mode_w = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_desc.max_lod = 0.0F;
        texture_sampler_id = sampler_host->RegisterSampler(context_, sampler_desc);
    }

    if (fallback_texture.image != VK_NULL_HANDLE && fallback_texture.default_view != VK_NULL_HANDLE) {
        return;
    }

    resource::ImageCreateInfo create_info{};
    create_info.image_type = VK_IMAGE_TYPE_2D;
    create_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    create_info.extent = VkExtent3D{1U, 1U, 1U};
    create_info.mip_levels = 1U;
    create_info.array_layers = 1U;
    create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    create_info.sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    create_info.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    create_info.create_default_view = true;
    create_info.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
    create_info.default_view_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    create_info.default_base_mip_level = 0U;
    create_info.default_level_count = 1U;
    create_info.default_base_array_layer = 0U;
    create_info.default_layer_count = 1U;
    fallback_texture = resource::ImageHost::CreateImage(context_, create_info, *gpu_memory_host);

    VkImageMemoryBarrier2 to_transfer{};
    to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    to_transfer.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    to_transfer.srcAccessMask = 0U;
    to_transfer.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    to_transfer.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = fallback_texture.image;
    to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.baseMipLevel = 0U;
    to_transfer.subresourceRange.levelCount = 1U;
    to_transfer.subresourceRange.baseArrayLayer = 0U;
    to_transfer.subresourceRange.layerCount = 1U;
    upload_host_.RecordImageBarrier2(frame_index_, to_transfer);

    const std::uint32_t white_rgba8 = 0xFFFF'FFFFU;
    VkBufferImageCopy copy_region{};
    copy_region.bufferOffset = 0U;
    copy_region.bufferRowLength = 0U;
    copy_region.bufferImageHeight = 0U;
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel = 0U;
    copy_region.imageSubresource.baseArrayLayer = 0U;
    copy_region.imageSubresource.layerCount = 1U;
    copy_region.imageOffset = VkOffset3D{0, 0, 0};
    copy_region.imageExtent = VkExtent3D{1U, 1U, 1U};
    upload_host_.StageAndRecordCopyImage(frame_index_,
                                         fallback_texture.image,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                         copy_region,
                                         &white_rgba8,
                                         sizeof(white_rgba8),
                                         alignof(std::uint32_t));

    VkImageMemoryBarrier2 to_shader_read{};
    to_shader_read.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    to_shader_read.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    to_shader_read.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    to_shader_read.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    to_shader_read.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    to_shader_read.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_shader_read.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_shader_read.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_shader_read.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_shader_read.image = fallback_texture.image;
    to_shader_read.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_shader_read.subresourceRange.baseMipLevel = 0U;
    to_shader_read.subresourceRange.levelCount = 1U;
    to_shader_read.subresourceRange.baseArrayLayer = 0U;
    to_shader_read.subresourceRange.layerCount = 1U;
    upload_host_.RecordImageBarrier2(frame_index_, to_shader_read);
    fallback_texture_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

VkDescriptorSet ParticleRenderer2D::AcquireTextureDescriptorSet(std::uint32_t frame_index_,
                                                                std::uint32_t texture_id_) {
    if (context == nullptr || descriptor_host == nullptr || sampler_host == nullptr) {
        throw std::runtime_error("ParticleRenderer2D::AcquireTextureDescriptorSet missing runtime hosts");
    }
    if (!descriptor_layout_id.IsValid()) {
        return VK_NULL_HANDLE;
    }
    if (frame_index_ >= frame_texture_sets.size()) {
        throw std::out_of_range("ParticleRenderer2D::AcquireTextureDescriptorSet frame index out of range");
    }

    const asset::TextureHost::TextureRecord* texture_record = nullptr;
    if (texture_id_ != 0U && texture_host != nullptr && texture_host->IsInitialized()) {
        texture_record = texture_host->FindTexture(asset::TextureId{texture_id_});
    }

    const std::uint32_t effective_texture_id = (texture_record != nullptr) ? texture_id_ : 0U;
    auto& entries = frame_texture_sets[frame_index_];
    const std::size_t lower_bound_index = LowerBoundTextureSetIndex(entries, effective_texture_id);
    if (lower_bound_index < entries.size() && entries[lower_bound_index].texture_id == effective_texture_id) {
        return entries[lower_bound_index].descriptor_set;
    }

    VkImageView image_view = fallback_texture.default_view;
    VkImageLayout image_layout = fallback_texture_layout;
    if (texture_record != nullptr &&
        texture_record->resource.default_view != VK_NULL_HANDLE &&
        texture_record->current_layout != VK_IMAGE_LAYOUT_UNDEFINED) {
        image_view = texture_record->resource.default_view;
        image_layout = texture_record->current_layout;
    }

    const VkSampler sampler = sampler_host->GetSampler(texture_sampler_id);
    if (sampler == VK_NULL_HANDLE || image_view == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    const VkDescriptorSet descriptor_set =
        descriptor_host->AllocateSet(*context, frame_index_, descriptor_layout_id);
    descriptor_buffer_write_scratch.clear();
    descriptor_image_write_scratch.clear();
    descriptor_texel_write_scratch.clear();
    descriptor_image_write_scratch.push_back({
        .binding = 0U,
        .array_element = 0U,
        .descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .sampler = sampler,
        .image_view = image_view,
        .image_layout = image_layout
    });
    descriptor_host->UpdateSet(*context,
                               descriptor_set,
                               descriptor_buffer_write_scratch,
                               descriptor_image_write_scratch,
                               descriptor_texel_write_scratch);
    ++stats.descriptor_set_update_count;

    const std::size_t old_size = entries.size();
    entries.resize(old_size + 1U);
    if (lower_bound_index < old_size) {
        for (std::size_t index = old_size; index > lower_bound_index; --index) {
            entries[index] = std::move(entries[index - 1U]);
        }
    }
    entries[lower_bound_index] = TextureSetEntry{
        .texture_id = effective_texture_id,
        .descriptor_set = descriptor_set
    };
    return descriptor_set;
}

} // namespace vr::particle

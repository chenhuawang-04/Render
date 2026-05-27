#pragma once

#include "vr/render/descriptor_host.hpp"
#include "vr/render_graph/render_graph_types.hpp"

#include <string_view>

namespace vr::render {

inline constexpr std::uint32_t scene_3d_sampled_image_set = 0U;
inline constexpr std::uint32_t scene_3d_sampler_set = 1U;
inline constexpr std::uint32_t scene_3d_shared_buffer_set = 2U;
inline constexpr std::uint32_t scene_3d_ibl_set = 3U;
inline constexpr std::uint32_t scene_3d_temporal_motion_buffer_set = 0U;

inline constexpr std::uint32_t scene_3d_light_records_binding = 0U;
inline constexpr std::uint32_t scene_3d_cluster_headers_binding = 1U;
inline constexpr std::uint32_t scene_3d_cluster_indices_binding = 2U;
inline constexpr std::uint32_t scene_3d_shadow_views_binding = 3U;
inline constexpr std::uint32_t scene_3d_lighting_uniform_binding = 4U;
inline constexpr std::uint32_t scene_3d_skeletal_components_binding = 5U;
inline constexpr std::uint32_t scene_3d_skeletal_matrices_binding = 6U;
inline constexpr std::uint32_t scene_3d_geometry_appearance_binding = 7U;
inline constexpr std::uint32_t scene_3d_surface_appearance_binding = 8U;
inline constexpr std::uint32_t scene_3d_ibl_params_binding = 0U;
inline constexpr std::uint32_t
    scene_3d_temporal_current_skeletal_components_binding = 0U;
inline constexpr std::uint32_t
    scene_3d_temporal_current_skeletal_matrices_binding = 1U;
inline constexpr std::uint32_t
    scene_3d_temporal_previous_skeletal_components_binding = 2U;
inline constexpr std::uint32_t
    scene_3d_temporal_previous_skeletal_matrices_binding = 3U;

[[nodiscard]] inline DescriptorSetLayoutDesc BuildSharedScene3DBufferLayoutDesc() {
    DescriptorSetLayoutDesc layout_desc{};

    auto append_binding =
        [&](const std::uint32_t binding_,
            const VkDescriptorType descriptor_type_,
            const VkShaderStageFlags stage_flags_) {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = binding_;
            binding.descriptorType = descriptor_type_;
            binding.descriptorCount = 1U;
            binding.stageFlags = stage_flags_;
            binding.pImmutableSamplers = nullptr;
            layout_desc.bindings.push_back(binding);
        };

    append_binding(scene_3d_light_records_binding,
                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   VK_SHADER_STAGE_FRAGMENT_BIT);
    append_binding(scene_3d_cluster_headers_binding,
                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   VK_SHADER_STAGE_FRAGMENT_BIT);
    append_binding(scene_3d_cluster_indices_binding,
                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   VK_SHADER_STAGE_FRAGMENT_BIT);
    append_binding(scene_3d_shadow_views_binding,
                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   VK_SHADER_STAGE_FRAGMENT_BIT);
    append_binding(scene_3d_lighting_uniform_binding,
                   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                   VK_SHADER_STAGE_FRAGMENT_BIT);
    append_binding(scene_3d_skeletal_components_binding,
                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   VK_SHADER_STAGE_VERTEX_BIT);
    append_binding(scene_3d_skeletal_matrices_binding,
                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   VK_SHADER_STAGE_VERTEX_BIT);
    append_binding(scene_3d_geometry_appearance_binding,
                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   VK_SHADER_STAGE_FRAGMENT_BIT);
    append_binding(scene_3d_surface_appearance_binding,
                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   VK_SHADER_STAGE_FRAGMENT_BIT);
    return layout_desc;
}

[[nodiscard]] inline render_graph::PassShaderContractDesc BuildSharedScene3DShaderContract(
    std::string_view debug_name_) {
    auto contract = render_graph::MakeSharedBindlessFragmentShaderContract(debug_name_);

    auto append_binding =
        [&](const std::uint32_t set_,
            const std::uint32_t binding_,
            const render_graph::DescriptorBindingKind kind_,
            const std::uint32_t stage_flags_) {
            contract.bindings.push_back({
                .set = set_,
                .binding = binding_,
                .kind = kind_,
                .stage_flags = stage_flags_,
                .descriptor_count = 1U,
            });
        };

    append_binding(scene_3d_shared_buffer_set,
                   scene_3d_light_records_binding,
                   render_graph::DescriptorBindingKind::storage_buffer,
                   render_graph::shader_stage_fragment_flag);
    append_binding(scene_3d_shared_buffer_set,
                   scene_3d_cluster_headers_binding,
                   render_graph::DescriptorBindingKind::storage_buffer,
                   render_graph::shader_stage_fragment_flag);
    append_binding(scene_3d_shared_buffer_set,
                   scene_3d_cluster_indices_binding,
                   render_graph::DescriptorBindingKind::storage_buffer,
                   render_graph::shader_stage_fragment_flag);
    append_binding(scene_3d_shared_buffer_set,
                   scene_3d_shadow_views_binding,
                   render_graph::DescriptorBindingKind::storage_buffer,
                   render_graph::shader_stage_fragment_flag);
    append_binding(scene_3d_shared_buffer_set,
                   scene_3d_lighting_uniform_binding,
                   render_graph::DescriptorBindingKind::uniform_buffer,
                   render_graph::shader_stage_fragment_flag);
    append_binding(scene_3d_shared_buffer_set,
                   scene_3d_skeletal_components_binding,
                   render_graph::DescriptorBindingKind::storage_buffer,
                   render_graph::shader_stage_vertex_flag);
    append_binding(scene_3d_shared_buffer_set,
                   scene_3d_skeletal_matrices_binding,
                   render_graph::DescriptorBindingKind::storage_buffer,
                   render_graph::shader_stage_vertex_flag);
    append_binding(scene_3d_shared_buffer_set,
                   scene_3d_geometry_appearance_binding,
                   render_graph::DescriptorBindingKind::storage_buffer,
                   render_graph::shader_stage_fragment_flag);
    append_binding(scene_3d_shared_buffer_set,
                   scene_3d_surface_appearance_binding,
                   render_graph::DescriptorBindingKind::storage_buffer,
                   render_graph::shader_stage_fragment_flag);
    append_binding(scene_3d_ibl_set,
                   scene_3d_ibl_params_binding,
                   render_graph::DescriptorBindingKind::uniform_buffer,
                   render_graph::shader_stage_fragment_flag);
    return contract;
}

[[nodiscard]] inline DescriptorSetLayoutDesc
BuildScene3DTemporalMotionBufferLayoutDesc() {
    DescriptorSetLayoutDesc layout_desc{};

    auto append_binding =
        [&](const std::uint32_t binding_,
            const VkDescriptorType descriptor_type_,
            const VkShaderStageFlags stage_flags_) {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = binding_;
            binding.descriptorType = descriptor_type_;
            binding.descriptorCount = 1U;
            binding.stageFlags = stage_flags_;
            binding.pImmutableSamplers = nullptr;
            layout_desc.bindings.push_back(binding);
        };

    append_binding(scene_3d_temporal_current_skeletal_components_binding,
                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   VK_SHADER_STAGE_VERTEX_BIT);
    append_binding(scene_3d_temporal_current_skeletal_matrices_binding,
                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   VK_SHADER_STAGE_VERTEX_BIT);
    append_binding(scene_3d_temporal_previous_skeletal_components_binding,
                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   VK_SHADER_STAGE_VERTEX_BIT);
    append_binding(scene_3d_temporal_previous_skeletal_matrices_binding,
                   VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   VK_SHADER_STAGE_VERTEX_BIT);
    return layout_desc;
}

[[nodiscard]] inline render_graph::PassShaderContractDesc
BuildScene3DTemporalMotionShaderContract(std::string_view debug_name_) {
    render_graph::PassShaderContractDesc contract{};
    contract.debug_name.assign(debug_name_.begin(), debug_name_.end());
    contract.bindings.push_back({
        .set = scene_3d_temporal_motion_buffer_set,
        .binding = scene_3d_temporal_current_skeletal_components_binding,
        .kind = render_graph::DescriptorBindingKind::storage_buffer,
        .stage_flags = render_graph::shader_stage_vertex_flag,
        .descriptor_count = 1U,
    });
    contract.bindings.push_back({
        .set = scene_3d_temporal_motion_buffer_set,
        .binding = scene_3d_temporal_current_skeletal_matrices_binding,
        .kind = render_graph::DescriptorBindingKind::storage_buffer,
        .stage_flags = render_graph::shader_stage_vertex_flag,
        .descriptor_count = 1U,
    });
    contract.bindings.push_back({
        .set = scene_3d_temporal_motion_buffer_set,
        .binding = scene_3d_temporal_previous_skeletal_components_binding,
        .kind = render_graph::DescriptorBindingKind::storage_buffer,
        .stage_flags = render_graph::shader_stage_vertex_flag,
        .descriptor_count = 1U,
    });
    contract.bindings.push_back({
        .set = scene_3d_temporal_motion_buffer_set,
        .binding = scene_3d_temporal_previous_skeletal_matrices_binding,
        .kind = render_graph::DescriptorBindingKind::storage_buffer,
        .stage_flags = render_graph::shader_stage_vertex_flag,
        .descriptor_count = 1U,
    });
    return contract;
}

} // namespace vr::render

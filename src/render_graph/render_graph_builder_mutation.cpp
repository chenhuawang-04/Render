#include "render_graph_builder_internal.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace vr::render_graph {
using namespace builder_detail;

ResourceHandle RenderGraphBuilder::CreateTexture(std::string_view debug_name_,
                                                 const TextureDesc& desc_,
                                                 const ResourceLifetime lifetime_) {
    ResourceNode resource{};
    resource.kind = ResourceKind::texture;
    resource.lifetime = lifetime_;
    resource.debug_name = std::string(debug_name_);
    resource.texture = desc_;
    resource.versions.emplace_back();
    resources.push_back(std::move(resource));
    return ResourceHandle{
        .index = static_cast<std::uint32_t>(resources.size() - 1U),
        .generation = 1U,
    };
}

ResourceHandle RenderGraphBuilder::CreateBuffer(std::string_view debug_name_,
                                                const BufferDesc& desc_,
                                                const ResourceLifetime lifetime_) {
    ResourceNode resource{};
    resource.kind = ResourceKind::buffer;
    resource.lifetime = lifetime_;
    resource.debug_name = std::string(debug_name_);
    resource.buffer = desc_;
    resource.versions.emplace_back();
    resources.push_back(std::move(resource));
    return ResourceHandle{
        .index = static_cast<std::uint32_t>(resources.size() - 1U),
        .generation = 1U,
    };
}

PassHandle RenderGraphBuilder::AddPass(std::string_view debug_name_,
                                       const bool side_effect_,
                                       const QueueClass queue_) {
    PassNode pass{};
    pass.handle = PassHandle{.index = static_cast<std::uint32_t>(passes.size())};
    pass.debug_name = std::string(debug_name_);
    pass.side_effect = side_effect_;
    pass.queue = queue_;
    passes.push_back(std::move(pass));
    return passes.back().handle;
}

ResourceVersionHandle RenderGraphBuilder::Read(const PassHandle pass_,
                                               const ResourceHandle resource_) {
    return Read(pass_, resource_, AccessDesc{});
}

ResourceVersionHandle RenderGraphBuilder::Read(const PassHandle pass_,
                                               const ResourceHandle resource_,
                                               const AccessDesc& access_) {
    const ResourceNode& resource = RequireResource(resource_);
    return Read(pass_,
                ResourceVersionHandle{
                    .resource_index = resource_.index,
                    .version = resource.latest_version,
                },
                access_);
}

ResourceVersionHandle RenderGraphBuilder::Read(const PassHandle pass_,
                                               const ResourceVersionHandle version_) {
    return Read(pass_, version_, AccessDesc{});
}

ResourceVersionHandle RenderGraphBuilder::Read(const PassHandle pass_,
                                               const ResourceVersionHandle version_,
                                               const AccessDesc& access_) {
    PassNode& pass = RequirePass(pass_);
    const ResourceNode& resource = RequireVersion(version_);

    auto& version_node = resources[version_.resource_index].versions[version_.version];
    AppendUnique(version_node.consumers, pass_);
    AppendUnique(pass.reads, BindAccessDesc(version_, resource.kind, access_));
    return version_;
}

ResourceVersionHandle RenderGraphBuilder::Write(const PassHandle pass_,
                                                const ResourceHandle resource_) {
    return Write(pass_, resource_, AccessDesc{});
}

ResourceVersionHandle RenderGraphBuilder::Write(const PassHandle pass_,
                                                const ResourceHandle resource_,
                                                const AccessDesc& access_) {
    const ResourceNode& resource = RequireResource(resource_);
    return Write(pass_,
                 ResourceVersionHandle{
                     .resource_index = resource_.index,
                     .version = resource.latest_version,
                 },
                 access_);
}

ResourceVersionHandle RenderGraphBuilder::Write(const PassHandle pass_,
                                                const ResourceVersionHandle version_) {
    return Write(pass_, version_, AccessDesc{});
}

ResourceVersionHandle RenderGraphBuilder::Write(const PassHandle pass_,
                                                const ResourceVersionHandle version_,
                                                const AccessDesc& access_) {
    PassNode& pass = RequirePass(pass_);
    (void)RequireVersion(version_);

    ResourceNode& resource = resources[version_.resource_index];
    if (version_.version != resource.latest_version) {
        throw std::invalid_argument(
            "RenderGraphBuilder::Write currently requires the latest resource version");
    }

    auto& input_version = resource.versions[version_.version];
    AppendUnique(input_version.consumers, pass_);

    const ResourceVersionHandle output_version{
        .resource_index = version_.resource_index,
        .version = static_cast<std::uint32_t>(resource.versions.size()),
    };
    resource.versions.push_back(ResourceVersionNode{.producer = pass_});
    resource.latest_version = output_version.version;
    pass.writes.push_back(WriteRecord{
        .input = version_,
        .output = output_version,
        .access = BindAccessDesc(output_version, resource.kind, access_),
    });
    return output_version;
}

void RenderGraphBuilder::AddDependency(const PassHandle pass_,
                                       const PassHandle dependency_) {
    PassNode& target_pass = RequirePass(pass_);
    (void)RequirePass(dependency_);
    AppendUnique(target_pass.explicit_dependencies, dependency_);
}

void RenderGraphBuilder::SetPassCompileHints(const PassHandle pass_,
                                             const PassCompileHints compile_hints_) {
    PassNode& target_pass = RequirePass(pass_);
    target_pass.compile_hints = compile_hints_;
}

void RenderGraphBuilder::SetNativePassPlannerConfig(
    const NativePassPlannerConfig planner_config_) noexcept {
    native_pass_planner_config = planner_config_;
}

const NativePassPlannerConfig& RenderGraphBuilder::NativePassPlannerConfigInfo() const noexcept {
    return native_pass_planner_config;
}

std::uint32_t RenderGraphBuilder::RegisterExternalBufferBindingResolver(
    ExternalBufferBindingResolver resolver_) {
    if (resolver_.user_data == nullptr) {
        throw std::invalid_argument(
            "RenderGraphBuilder::RegisterExternalBufferBindingResolver requires non-null user data");
    }
    if (resolver_.resolve_fn == nullptr) {
        throw std::invalid_argument(
            "RenderGraphBuilder::RegisterExternalBufferBindingResolver requires a valid resolve function");
    }
    if (external_buffer_binding_resolvers.size() >=
        static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)() - 1U)) {
        throw std::overflow_error(
            "RenderGraphBuilder::RegisterExternalBufferBindingResolver overflowed resolver ids");
    }
    external_buffer_binding_resolvers.push_back(std::move(resolver_));
    return static_cast<std::uint32_t>(external_buffer_binding_resolvers.size());
}

void RenderGraphBuilder::AddPassDescriptorBinding(
    const PassHandle pass_,
    const PassDescriptorBindingDesc& descriptor_binding_) {
    PassNode& target_pass = RequirePass(pass_);
    if (descriptor_binding_.source == DescriptorBindingSource::none) {
        throw std::invalid_argument(
            "RenderGraphBuilder::AddPassDescriptorBinding requires a valid descriptor binding source");
    }
    if (descriptor_binding_.stage_flags == shader_stage_none_flag) {
        throw std::invalid_argument(
            "RenderGraphBuilder::AddPassDescriptorBinding requires non-empty shader stage flags");
    }
    if (descriptor_binding_.source_id == 0U) {
        throw std::invalid_argument(
            "RenderGraphBuilder::AddPassDescriptorBinding requires a valid descriptor binding source id");
    }

    const auto duplicate = std::find_if(
        target_pass.descriptor_bindings.begin(),
        target_pass.descriptor_bindings.end(),
        [&](const PassDescriptorBindingDesc& existing_) {
            return existing_.set == descriptor_binding_.set &&
                   existing_.binding == descriptor_binding_.binding;
        });
    if (duplicate != target_pass.descriptor_bindings.end()) {
        if (duplicate->source == descriptor_binding_.source &&
            duplicate->kind == descriptor_binding_.kind &&
            duplicate->stage_flags == descriptor_binding_.stage_flags &&
            duplicate->source_id == descriptor_binding_.source_id) {
            return;
        }
        throw std::invalid_argument(
            "RenderGraphBuilder::AddPassDescriptorBinding encountered duplicate set/binding in one pass");
    }

    target_pass.descriptor_bindings.push_back(descriptor_binding_);
}

void RenderGraphBuilder::SetPassShaderContract(const PassHandle pass_,
                                               PassShaderContractDesc shader_contract_) {
    PassNode& target_pass = RequirePass(pass_);
    if (shader_contract_.bindings.empty()) {
        throw std::invalid_argument(
            "RenderGraphBuilder::SetPassShaderContract requires at least one shader contract binding");
    }
    for (const auto& binding_ : shader_contract_.bindings) {
        if (binding_.stage_flags == shader_stage_none_flag) {
            throw std::invalid_argument(
                "RenderGraphBuilder::SetPassShaderContract requires non-empty shader stage flags");
        }
        if (binding_.descriptor_count == 0U) {
            throw std::invalid_argument(
                "RenderGraphBuilder::SetPassShaderContract requires non-zero descriptor counts");
        }
    }
    std::sort(shader_contract_.bindings.begin(),
              shader_contract_.bindings.end(),
              [](const ShaderContractBindingDesc& lhs_,
                 const ShaderContractBindingDesc& rhs_) {
                  if (lhs_.set != rhs_.set) {
                      return lhs_.set < rhs_.set;
                  }
                  return lhs_.binding < rhs_.binding;
              });
    for (std::size_t index = 1U; index < shader_contract_.bindings.size(); ++index) {
        const auto& previous = shader_contract_.bindings[index - 1U];
        const auto& current = shader_contract_.bindings[index];
        if (previous.set == current.set && previous.binding == current.binding) {
            throw std::invalid_argument(
                "RenderGraphBuilder::SetPassShaderContract encountered duplicate set/binding");
        }
    }
    if (!target_pass.shader_contract.has_value()) {
        target_pass.shader_contract = std::move(shader_contract_);
        return;
    }

    auto& existing_contract = *target_pass.shader_contract;
    for (const auto& binding_ : shader_contract_.bindings) {
        const auto existing_it = std::find_if(
            existing_contract.bindings.begin(),
            existing_contract.bindings.end(),
            [&](const ShaderContractBindingDesc& existing_) {
                return existing_.set == binding_.set &&
                       existing_.binding == binding_.binding;
            });
        if (existing_it == existing_contract.bindings.end()) {
            existing_contract.bindings.push_back(binding_);
            continue;
        }
        if (existing_it->kind != binding_.kind ||
            existing_it->stage_flags != binding_.stage_flags ||
            existing_it->descriptor_count != binding_.descriptor_count) {
            throw std::invalid_argument(
                "RenderGraphBuilder::SetPassShaderContract encountered conflicting set/binding requirements");
        }
    }
    std::sort(existing_contract.bindings.begin(),
              existing_contract.bindings.end(),
              [](const ShaderContractBindingDesc& lhs_,
                 const ShaderContractBindingDesc& rhs_) {
                  if (lhs_.set != rhs_.set) {
                      return lhs_.set < rhs_.set;
                  }
                  return lhs_.binding < rhs_.binding;
              });
}

void RenderGraphBuilder::AddBindlessTableBinding(
    const PassHandle pass_,
    const std::uint32_t set_,
    const DescriptorBindingKind kind_,
    const std::uint32_t bindless_table_id_,
    const std::uint32_t stage_flags_,
    const std::uint32_t binding_) {
    AddPassDescriptorBinding(pass_,
                             PassDescriptorBindingDesc{
                                 .set = set_,
                                 .binding = binding_,
                                 .source = DescriptorBindingSource::bindless_table,
                                 .kind = kind_,
                                 .stage_flags = stage_flags_,
                                 .source_id = bindless_table_id_,
                             });
}

void RenderGraphBuilder::AddExternalBufferBinding(
    const PassHandle pass_,
    const std::uint32_t set_,
    const std::uint32_t binding_,
    const DescriptorBindingKind kind_,
    const std::uint32_t resolver_id_,
    const std::uint32_t stage_flags_) {
    if (kind_ != DescriptorBindingKind::storage_buffer &&
        kind_ != DescriptorBindingKind::uniform_buffer) {
        throw std::invalid_argument(
            "RenderGraphBuilder::AddExternalBufferBinding requires storage/uniform buffer kinds");
    }
    AddPassDescriptorBinding(pass_,
                             PassDescriptorBindingDesc{
                                 .set = set_,
                                 .binding = binding_,
                                 .source = DescriptorBindingSource::external_buffer,
                                 .kind = kind_,
                                 .stage_flags = stage_flags_,
                             .source_id = resolver_id_,
                         });
}

bool RenderGraphBuilder::HasPassDescriptorBinding(const PassHandle pass_,
                                                  const std::uint32_t set_,
                                                  const std::uint32_t binding_) const noexcept {
    if (pass_.index >= passes.size()) {
        return false;
    }
    const auto& target_pass = passes[pass_.index];
    return std::any_of(target_pass.descriptor_bindings.begin(),
                       target_pass.descriptor_bindings.end(),
                       [&](const PassDescriptorBindingDesc& binding_desc_) {
                           return binding_desc_.set == set_ &&
                                  binding_desc_.binding == binding_;
                       });
}

void RenderGraphBuilder::SetRasterPassDesc(const PassHandle pass_,
                                           RasterPassDesc raster_pass_) {
    PassNode& target_pass = RequirePass(pass_);
    target_pass.raster_pass = std::move(raster_pass_);
}

void RenderGraphBuilder::SetExecuteCallback(const PassHandle pass_,
                                            PassExecutionThunk execute_) {
    PassNode& target_pass = RequirePass(pass_);
    target_pass.execute = std::move(execute_);
}

} // namespace vr::render_graph

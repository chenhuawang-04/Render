#include "vr/render_graph/render_graph_builder.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace vr::render_graph {
namespace {

[[nodiscard]] std::string BuildVersionDebugName(const std::string& base_name_,
                                                const std::uint32_t version_) {
    std::ostringstream oss{};
    oss << base_name_ << "#v" << version_;
    return oss.str();
}

[[nodiscard]] const char* ResourceKindToString(const ResourceKind kind_) noexcept {
    switch (kind_) {
    case ResourceKind::texture:
        return "texture";
    case ResourceKind::buffer:
        return "buffer";
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] const char* ResourceLifetimeToString(const ResourceLifetime lifetime_) noexcept {
    switch (lifetime_) {
    case ResourceLifetime::imported:
        return "imported";
    case ResourceLifetime::persistent:
        return "persistent";
    case ResourceLifetime::transient:
        return "transient";
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] const char* QueueClassToString(const QueueClass queue_) noexcept {
    switch (queue_) {
    case QueueClass::graphics:
        return "graphics";
    case QueueClass::compute:
        return "compute";
    case QueueClass::transfer:
        return "transfer";
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] const char* DescriptorBindingSourceToString(
    const DescriptorBindingSource source_) noexcept {
    switch (source_) {
    case DescriptorBindingSource::none:
        return "none";
    case DescriptorBindingSource::bindless_table:
        return "bindless_table";
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] const char* DescriptorBindingKindToString(
    const DescriptorBindingKind kind_) noexcept {
    switch (kind_) {
    case DescriptorBindingKind::sampled_image_table:
        return "sampled_image_table";
    case DescriptorBindingKind::sampler_table:
        return "sampler_table";
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] std::string BuildShaderStageFlagsString(
    const std::uint32_t stage_flags_) {
    std::ostringstream oss{};
    bool first = true;
    const auto append = [&](const char* label_) {
        if (!first) {
            oss << '|';
        }
        oss << label_;
        first = false;
    };
    if (HasShaderStageFlag(stage_flags_, shader_stage_vertex_flag)) {
        append("vertex");
    }
    if (HasShaderStageFlag(stage_flags_, shader_stage_fragment_flag)) {
        append("fragment");
    }
    if (HasShaderStageFlag(stage_flags_, shader_stage_compute_flag)) {
        append("compute");
    }
    if (first) {
        return "none";
    }
    return oss.str();
}

[[nodiscard]] const char* AccessKindToString(const AccessKind access_) noexcept {
    switch (access_) {
    case AccessKind::none:
        return "none";
    case AccessKind::color_attachment_read:
        return "color_attachment_read";
    case AccessKind::color_attachment_write:
        return "color_attachment_write";
    case AccessKind::depth_stencil_read:
        return "depth_stencil_read";
    case AccessKind::depth_stencil_write:
        return "depth_stencil_write";
    case AccessKind::depth_stencil_read_write:
        return "depth_stencil_read_write";
    case AccessKind::shader_sample_read:
        return "shader_sample_read";
    case AccessKind::shader_storage_read:
        return "shader_storage_read";
    case AccessKind::shader_storage_write:
        return "shader_storage_write";
    case AccessKind::shader_storage_read_write:
        return "shader_storage_read_write";
    case AccessKind::uniform_read:
        return "uniform_read";
    case AccessKind::vertex_buffer_read:
        return "vertex_buffer_read";
    case AccessKind::index_buffer_read:
        return "index_buffer_read";
    case AccessKind::indirect_command_read:
        return "indirect_command_read";
    case AccessKind::transfer_read:
        return "transfer_read";
    case AccessKind::transfer_write:
        return "transfer_write";
    case AccessKind::present:
        return "present";
    case AccessKind::host_read:
        return "host_read";
    case AccessKind::host_write:
        return "host_write";
    default:
        break;
    }
    return "unknown";
}

[[nodiscard]] std::string EscapeJsonString(const std::string& value_) {
    std::ostringstream oss{};
    for (const char character_ : value_) {
        switch (character_) {
        case '\\':
            oss << "\\\\";
            break;
        case '"':
            oss << "\\\"";
            break;
        case '\n':
            oss << "\\n";
            break;
        case '\r':
            oss << "\\r";
            break;
        case '\t':
            oss << "\\t";
            break;
        default:
            oss << character_;
            break;
        }
    }
    return oss.str();
}

[[nodiscard]] std::string EscapeDotLabel(const std::string& value_) {
    std::ostringstream oss{};
    for (const char character_ : value_) {
        switch (character_) {
        case '\\':
            oss << "\\\\";
            break;
        case '"':
            oss << "\\\"";
            break;
        case '\n':
            oss << "\\n";
            break;
        default:
            oss << character_;
            break;
        }
    }
    return oss.str();
}

[[nodiscard]] std::string MakeResourceNodeId(const ResourceVersionHandle version_) {
    std::ostringstream oss{};
    oss << "resource_" << version_.resource_index << "_v" << version_.version;
    return oss.str();
}

[[nodiscard]] const CompiledResourceVersionLiveness* FindLivenessRange(
    const std::vector<CompiledResourceVersionLiveness>& liveness_ranges_,
    const ResourceVersionHandle version_) {
    for (const auto& range_ : liveness_ranges_) {
        if (range_.version.resource_index == version_.resource_index &&
            range_.version.version == version_.version) {
            return &range_;
        }
    }
    return nullptr;
}

[[nodiscard]] constexpr bool HasExplicitSubresourceRange(const SubresourceRange& range_) noexcept {
    return range_.level_count != 0U || range_.layer_count != 0U;
}

[[nodiscard]] constexpr bool HasExplicitBufferRange(const BufferRange& range_) noexcept {
    return range_.offset_bytes != 0U || range_.size_bytes != 0U;
}

[[nodiscard]] AccessDesc BindAccessDesc(const ResourceVersionHandle version_,
                                        const ResourceKind kind_,
                                        const AccessDesc& access_) noexcept {
    AccessDesc bound = access_;
    bound.resource = version_;
    if (kind_ == ResourceKind::texture) {
        bound.buffer_range = {};
    } else {
        bound.subresource_range = {};
    }
    return bound;
}

void AppendJsonAccessDesc(std::ostringstream& oss_,
                          const AccessDesc& access_,
                          const ResourceKind kind_) {
    oss_ << "{\"resourceIndex\": " << access_.resource.resource_index
         << ", \"version\": " << access_.resource.version
         << ", \"access\": \"" << AccessKindToString(access_.access) << "\"";
    if (kind_ == ResourceKind::texture) {
        oss_ << ", \"subresourceRange\": {"
             << "\"baseMipLevel\": " << access_.subresource_range.base_mip_level
             << ", \"levelCount\": " << access_.subresource_range.level_count
             << ", \"baseArrayLayer\": " << access_.subresource_range.base_array_layer
             << ", \"layerCount\": " << access_.subresource_range.layer_count << "}";
    } else {
        oss_ << ", \"bufferRange\": {"
             << "\"offsetBytes\": " << access_.buffer_range.offset_bytes
             << ", \"sizeBytes\": " << access_.buffer_range.size_bytes << "}";
    }
    oss_ << '}';
}

void AppendJsonDescriptorBinding(std::ostringstream& oss_,
                                 const PassDescriptorBindingDesc& binding_) {
    oss_ << "{\"set\": " << binding_.set
         << ", \"binding\": " << binding_.binding
         << ", \"source\": \"" << DescriptorBindingSourceToString(binding_.source) << '"'
         << ", \"kind\": \"" << DescriptorBindingKindToString(binding_.kind) << '"'
         << ", \"stageFlags\": \"" << BuildShaderStageFlagsString(binding_.stage_flags) << '"'
         << ", \"sourceId\": " << binding_.source_id << '}';
}

[[nodiscard]] std::string BuildAccessDotLabel(const AccessDesc& access_,
                                              const ResourceKind kind_) {
    std::ostringstream oss{};
    oss << AccessKindToString(access_.access);
    if (kind_ == ResourceKind::texture && HasExplicitSubresourceRange(access_.subresource_range)) {
        oss << "\\nmip=" << access_.subresource_range.base_mip_level
            << "+" << access_.subresource_range.level_count
            << " layer=" << access_.subresource_range.base_array_layer
            << "+" << access_.subresource_range.layer_count;
    }
    if (kind_ == ResourceKind::buffer && HasExplicitBufferRange(access_.buffer_range)) {
        oss << "\\noffset=" << access_.buffer_range.offset_bytes
            << " size=" << access_.buffer_range.size_bytes;
    }
    return oss.str();
}

} // namespace

const CompiledPass* CompiledRenderGraph::FindPass(const PassHandle handle_) const noexcept {
    for (const auto& pass_ : passes) {
        if (pass_.handle.index == handle_.index) {
            return &pass_;
        }
    }
    return nullptr;
}

const CompiledResource* CompiledRenderGraph::FindResource(const ResourceHandle handle_) const noexcept {
    for (const auto& resource_ : resources) {
        if (resource_.handle.index == handle_.index) {
            return &resource_;
        }
    }
    return nullptr;
}

std::string CompiledRenderGraph::BuildDebugString() const {
    std::ostringstream oss{};
    oss << "execution_order=" << execution_order.size() << '\n';
    for (std::size_t index = 0; index < passes.size(); ++index) {
        const auto& pass_ = passes[index];
        oss << '[' << index << "] pass=" << pass_.debug_name;
        if (pass_.side_effect) {
            oss << " side_effect";
        }
        oss << " queue=" << QueueClassToString(pass_.queue);
        oss << " deps=";
        for (std::size_t dep_index = 0; dep_index < pass_.dependencies.size(); ++dep_index) {
            if (dep_index != 0U) {
                oss << ',';
            }
            oss << pass_.dependencies[dep_index].index;
        }
        oss << " reads=" << pass_.reads.size();
        oss << " writes=" << pass_.writes.size();
        if (pass_.raster_pass.has_value()) {
            oss << " raster_colors=" << pass_.raster_pass->color_attachments.size();
            if (pass_.raster_pass->has_depth_attachment) {
                oss << " depth";
            }
        }
        if (pass_.executable) {
            oss << " executable";
        }
        if (!pass_.descriptor_bindings.empty()) {
            oss << " descriptor_bindings=" << pass_.descriptor_bindings.size();
        }
        oss << '\n';
    }

    oss << "liveness=" << liveness_ranges.size() << '\n';
    for (const auto& range_ : liveness_ranges) {
        oss << "resource=" << range_.debug_name
            << " first=" << range_.first_pass_order
            << " last=" << range_.last_pass_order << '\n';
    }

    oss << barrier_plan.BuildDebugString();
    return oss.str();
}

std::string CompiledRenderGraph::BuildDotGraph() const {
    std::ostringstream oss{};
    oss << "digraph RenderGraph {\n";
    oss << "  rankdir=LR;\n";
    oss << "  node [fontsize=10];\n";

    for (const auto& pass_ : passes) {
        oss << "  pass_" << pass_.handle.index << " [shape=box,label=\""
            << EscapeDotLabel(pass_.debug_name);
        if (pass_.side_effect) {
            oss << "\\nside_effect";
        }
        oss << "\\nqueue=" << QueueClassToString(pass_.queue);
        if (!pass_.descriptor_bindings.empty()) {
            oss << "\\ndescriptors=" << pass_.descriptor_bindings.size();
        }
        oss << "\"];\n";
    }

    for (const auto& range_ : liveness_ranges) {
        oss << "  " << MakeResourceNodeId(range_.version)
            << " [shape=ellipse,label=\""
            << EscapeDotLabel(range_.debug_name)
            << "\\n" << ResourceKindToString(range_.kind)
            << " " << ResourceLifetimeToString(range_.lifetime)
            << "\\n" << range_.first_pass_order << "->" << range_.last_pass_order
            << "\"];\n";
    }

    for (const auto& pass_ : passes) {
        for (const auto dependency_ : pass_.dependencies) {
            oss << "  pass_" << dependency_.index << " -> pass_" << pass_.handle.index
                << " [style=dashed,label=\"dep\"];\n";
        }
        for (const auto& read_ : pass_.reads) {
            if (const auto* liveness_ = FindLivenessRange(liveness_ranges, read_.resource);
                liveness_ != nullptr) {
                oss << "  " << MakeResourceNodeId(read_.resource) << " -> pass_" << pass_.handle.index
                    << " [label=\"" << BuildAccessDotLabel(read_, liveness_->kind) << "\"];\n";
            }
        }
        for (const auto& write_ : pass_.writes) {
            if (const auto* liveness_ = FindLivenessRange(liveness_ranges, write_.resource);
                liveness_ != nullptr) {
                oss << "  pass_" << pass_.handle.index << " -> " << MakeResourceNodeId(write_.resource)
                    << " [label=\"" << BuildAccessDotLabel(write_, liveness_->kind) << "\"];\n";
            }
        }
        for (const auto& binding_ : pass_.descriptor_bindings) {
            oss << "  pass_" << pass_.handle.index << " -> pass_" << pass_.handle.index
                << " [style=dotted,label=\"set " << binding_.set
                << " " << DescriptorBindingKindToString(binding_.kind) << "\"];\n";
        }
    }

    oss << "}\n";
    return oss.str();
}

std::string CompiledRenderGraph::BuildJson() const {
    std::ostringstream oss{};
    oss << "{\n";
    oss << "  \"executionOrder\": [";
    for (std::size_t index = 0; index < execution_order.size(); ++index) {
        if (index != 0U) {
            oss << ", ";
        }
        oss << execution_order[index].index;
    }
    oss << "],\n";

    oss << "  \"resources\": [\n";
    for (std::size_t index = 0; index < resources.size(); ++index) {
        const auto& resource_ = resources[index];
        oss << "    {\n";
        oss << "      \"index\": " << resource_.handle.index << ",\n";
        oss << "      \"name\": \"" << EscapeJsonString(resource_.debug_name) << "\",\n";
        oss << "      \"kind\": \"" << ResourceKindToString(resource_.kind) << "\",\n";
        oss << "      \"lifetime\": \"" << ResourceLifetimeToString(resource_.lifetime) << "\"\n";
        oss << "    }";
        if (index + 1U != resources.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"passes\": [\n";
    for (std::size_t index = 0; index < passes.size(); ++index) {
        const auto& pass_ = passes[index];
        oss << "    {\n";
        oss << "      \"index\": " << pass_.handle.index << ",\n";
        oss << "      \"name\": \"" << EscapeJsonString(pass_.debug_name) << "\",\n";
        oss << "      \"sideEffect\": " << (pass_.side_effect ? "true" : "false") << ",\n";
        oss << "      \"executable\": " << (pass_.executable ? "true" : "false") << ",\n";
        oss << "      \"queue\": \"" << QueueClassToString(pass_.queue) << "\",\n";

        oss << "      \"dependencies\": [";
        for (std::size_t dep_index = 0; dep_index < pass_.dependencies.size(); ++dep_index) {
            if (dep_index != 0U) {
                oss << ", ";
            }
            oss << pass_.dependencies[dep_index].index;
        }
        oss << "],\n";

        oss << "      \"reads\": [";
        for (std::size_t read_index = 0; read_index < pass_.reads.size(); ++read_index) {
            const auto& read_ = pass_.reads[read_index];
            if (read_index != 0U) {
                oss << ", ";
            }
            const auto* resource_ = FindResource(ResourceHandle{
                .index = read_.resource.resource_index,
                .generation = 1U,
            });
            const ResourceKind kind = (resource_ != nullptr) ? resource_->kind : ResourceKind::buffer;
            AppendJsonAccessDesc(oss, read_, kind);
        }
        oss << "],\n";

        oss << "      \"writes\": [";
        for (std::size_t write_index = 0; write_index < pass_.writes.size(); ++write_index) {
            const auto& write_ = pass_.writes[write_index];
            if (write_index != 0U) {
                oss << ", ";
            }
            const auto* resource_ = FindResource(ResourceHandle{
                .index = write_.resource.resource_index,
                .generation = 1U,
            });
            const ResourceKind kind = (resource_ != nullptr) ? resource_->kind : ResourceKind::buffer;
            AppendJsonAccessDesc(oss, write_, kind);
        }
        oss << "],\n";

        oss << "      \"descriptorBindings\": [";
        for (std::size_t binding_index = 0; binding_index < pass_.descriptor_bindings.size(); ++binding_index) {
            if (binding_index != 0U) {
                oss << ", ";
            }
            AppendJsonDescriptorBinding(oss, pass_.descriptor_bindings[binding_index]);
        }
        oss << "]\n";
        oss << "    }";
        if (index + 1U != passes.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";

    oss << "  \"livenessRanges\": [\n";
    for (std::size_t index = 0; index < liveness_ranges.size(); ++index) {
        const auto& range_ = liveness_ranges[index];
        oss << "    {\n";
        oss << "      \"resourceIndex\": " << range_.version.resource_index << ",\n";
        oss << "      \"version\": " << range_.version.version << ",\n";
        oss << "      \"name\": \"" << EscapeJsonString(range_.debug_name) << "\",\n";
        oss << "      \"kind\": \"" << ResourceKindToString(range_.kind) << "\",\n";
        oss << "      \"lifetime\": \"" << ResourceLifetimeToString(range_.lifetime) << "\",\n";
        oss << "      \"firstPassOrder\": " << range_.first_pass_order << ",\n";
        oss << "      \"lastPassOrder\": " << range_.last_pass_order << '\n';
        oss << "    }";
        if (index + 1U != liveness_ranges.size()) {
            oss << ',';
        }
        oss << '\n';
    }
    oss << "  ],\n";
    oss << "  \"barrierPlan\": " << barrier_plan.BuildJson() << "\n";
    oss << "}\n";
    return oss.str();
}

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
        throw std::invalid_argument(
            "RenderGraphBuilder::AddPassDescriptorBinding encountered duplicate set/binding in one pass");
    }

    target_pass.descriptor_bindings.push_back(descriptor_binding_);
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

CompiledRenderGraph RenderGraphBuilder::Compile() const {
    CompiledRenderGraph compiled{};
    if (passes.empty()) {
        return compiled;
    }

    std::vector<std::vector<PassHandle>> dependencies(passes.size());
    for (const auto& pass_ : passes) {
        auto& pass_dependencies = dependencies[pass_.handle.index];
        for (const auto& read_ : pass_.reads) {
            const auto& version_node = resources[read_.resource.resource_index].versions[read_.resource.version];
            if (IsValidPassHandle(version_node.producer) &&
                version_node.producer.index != pass_.handle.index) {
                AppendUnique(pass_dependencies, version_node.producer);
            }
        }

        for (const auto& write_ : pass_.writes) {
            const auto& input_version = resources[write_.input.resource_index].versions[write_.input.version];
            for (const auto consumer_ : input_version.consumers) {
                if (consumer_.index != pass_.handle.index) {
                    AppendUnique(pass_dependencies, consumer_);
                }
            }
        }

        for (const auto dependency_ : pass_.explicit_dependencies) {
            if (dependency_.index != pass_.handle.index) {
                AppendUnique(pass_dependencies, dependency_);
            }
        }

        std::sort(pass_dependencies.begin(),
                  pass_dependencies.end(),
                  [](const PassHandle lhs_, const PassHandle rhs_) {
                      return lhs_.index < rhs_.index;
                  });
    }

    std::vector<bool> active(passes.size(), false);
    std::vector<std::uint32_t> stack{};
    for (const auto& pass_ : passes) {
        if (!pass_.side_effect) {
            continue;
        }
        active[pass_.handle.index] = true;
        stack.push_back(pass_.handle.index);
    }

    while (!stack.empty()) {
        const std::uint32_t pass_index = stack.back();
        stack.pop_back();
        for (const auto dependency_ : dependencies[pass_index]) {
            if (!active[dependency_.index]) {
                active[dependency_.index] = true;
                stack.push_back(dependency_.index);
            }
        }
    }

    std::uint32_t active_pass_count = 0U;
    for (const bool is_active_ : active) {
        active_pass_count += is_active_ ? 1U : 0U;
    }
    if (active_pass_count == 0U) {
        return compiled;
    }

    std::vector<std::uint32_t> indegree(passes.size(), 0U);
    std::vector<std::vector<std::uint32_t>> dependents(passes.size());
    for (const auto& pass_ : passes) {
        if (!active[pass_.handle.index]) {
            continue;
        }
        for (const auto dependency_ : dependencies[pass_.handle.index]) {
            if (!active[dependency_.index]) {
                continue;
            }
            indegree[pass_.handle.index] += 1U;
            dependents[dependency_.index].push_back(pass_.handle.index);
        }
    }

    std::vector<bool> emitted(passes.size(), false);
    compiled.execution_order.reserve(active_pass_count);
    std::uint32_t emitted_count = 0U;
    while (emitted_count < active_pass_count) {
        bool progressed = false;
        for (std::uint32_t pass_index = 0U;
             pass_index < static_cast<std::uint32_t>(passes.size());
             ++pass_index) {
            if (!active[pass_index] || emitted[pass_index] || indegree[pass_index] != 0U) {
                continue;
            }

            emitted[pass_index] = true;
            progressed = true;
            emitted_count += 1U;
            compiled.execution_order.push_back(PassHandle{.index = pass_index});

            for (const auto dependent_index : dependents[pass_index]) {
                if (indegree[dependent_index] == 0U) {
                    throw std::runtime_error(
                        "RenderGraphBuilder::Compile encountered an invalid dependency state");
                }
                indegree[dependent_index] -= 1U;
            }
        }

        if (!progressed) {
            throw std::runtime_error("RenderGraphBuilder::Compile detected a cycle");
        }
    }

    std::vector<std::uint32_t> execution_order_by_pass(passes.size(), invalid_render_graph_index);
    for (std::uint32_t order_index = 0U;
         order_index < static_cast<std::uint32_t>(compiled.execution_order.size());
         ++order_index) {
        execution_order_by_pass[compiled.execution_order[order_index].index] = order_index;
    }

    compiled.passes.reserve(compiled.execution_order.size());
    for (const auto handle_ : compiled.execution_order) {
        const auto& pass_ = passes[handle_.index];
        CompiledPass compiled_pass{};
        compiled_pass.handle = handle_;
        compiled_pass.debug_name = pass_.debug_name;
        compiled_pass.side_effect = pass_.side_effect;
        compiled_pass.executable = static_cast<bool>(pass_.execute) || pass_.raster_pass.has_value();
        compiled_pass.queue = pass_.queue;
        compiled_pass.raster_pass = pass_.raster_pass;
        compiled_pass.execute = pass_.execute;
        compiled_pass.reads = pass_.reads;
        compiled_pass.descriptor_bindings = pass_.descriptor_bindings;
        std::sort(compiled_pass.descriptor_bindings.begin(),
                  compiled_pass.descriptor_bindings.end(),
                  [](const PassDescriptorBindingDesc& lhs_,
                     const PassDescriptorBindingDesc& rhs_) {
                      if (lhs_.set != rhs_.set) {
                          return lhs_.set < rhs_.set;
                      }
                      return lhs_.binding < rhs_.binding;
                  });
        for (const auto dependency_ : dependencies[handle_.index]) {
            if (active[dependency_.index]) {
                compiled_pass.dependencies.push_back(dependency_);
            }
        }
        for (const auto& write_ : pass_.writes) {
            compiled_pass.writes.push_back(write_.access);
        }
        compiled.passes.push_back(std::move(compiled_pass));
    }

    for (std::uint32_t resource_index = 0U;
         resource_index < static_cast<std::uint32_t>(resources.size());
         ++resource_index) {
        const auto& resource = resources[resource_index];
        bool resource_has_active_use = false;
        for (std::uint32_t version_index = 0U;
             version_index < static_cast<std::uint32_t>(resource.versions.size());
             ++version_index) {
            const auto& version_node = resource.versions[version_index];
            bool has_active_use = false;
            std::uint32_t first_order = invalid_render_graph_index;
            std::uint32_t last_order = 0U;

            const auto consider_pass = [&](const PassHandle pass_handle_) {
                if (!IsValidPassHandle(pass_handle_) || !active[pass_handle_.index]) {
                    return;
                }
                const std::uint32_t order = execution_order_by_pass[pass_handle_.index];
                has_active_use = true;
                first_order = std::min(first_order, order);
                last_order = std::max(last_order, order);
            };

            consider_pass(version_node.producer);
            for (const auto consumer_ : version_node.consumers) {
                consider_pass(consumer_);
            }

            if (!has_active_use) {
                continue;
            }
            resource_has_active_use = true;

            compiled.liveness_ranges.push_back(CompiledResourceVersionLiveness{
                .version = ResourceVersionHandle{
                    .resource_index = resource_index,
                    .version = version_index,
                },
                .debug_name = BuildVersionDebugName(resource.debug_name, version_index),
                .kind = resource.kind,
                .lifetime = resource.lifetime,
                .first_pass_order = first_order,
                .last_pass_order = last_order,
            });
        }

        if (resource_has_active_use) {
            compiled.resources.push_back(CompiledResource{
                .handle = ResourceHandle{
                    .index = resource_index,
                    .generation = 1U,
                },
                .debug_name = resource.debug_name,
                .kind = resource.kind,
                .lifetime = resource.lifetime,
                .texture = resource.texture,
                .buffer = resource.buffer,
            });
        }
    }

    compiled.barrier_plan = BuildBarrierPlan(compiled);
    return compiled;
}

void RenderGraphBuilder::Reset() noexcept {
    resources.clear();
    passes.clear();
}

RenderGraphBuilder::ResourceNode& RenderGraphBuilder::RequireResource(
    const ResourceHandle handle_) {
    if (!IsValidResourceHandle(handle_) || handle_.index >= resources.size()) {
        throw std::out_of_range("RenderGraphBuilder resource handle is out of range");
    }
    return resources[handle_.index];
}

const RenderGraphBuilder::ResourceNode& RenderGraphBuilder::RequireResource(
    const ResourceHandle handle_) const {
    if (!IsValidResourceHandle(handle_) || handle_.index >= resources.size()) {
        throw std::out_of_range("RenderGraphBuilder resource handle is out of range");
    }
    return resources[handle_.index];
}

const RenderGraphBuilder::ResourceNode& RenderGraphBuilder::RequireVersion(
    const ResourceVersionHandle handle_) const {
    if (!IsValidResourceVersionHandle(handle_) ||
        handle_.resource_index >= resources.size()) {
        throw std::out_of_range("RenderGraphBuilder resource version handle is out of range");
    }
    const auto& resource = resources[handle_.resource_index];
    if (handle_.version >= resource.versions.size()) {
        throw std::out_of_range("RenderGraphBuilder resource version is out of range");
    }
    return resource;
}

RenderGraphBuilder::PassNode& RenderGraphBuilder::RequirePass(const PassHandle handle_) {
    if (!IsValidPassHandle(handle_) || handle_.index >= passes.size()) {
        throw std::out_of_range("RenderGraphBuilder pass handle is out of range");
    }
    return passes[handle_.index];
}

const RenderGraphBuilder::PassNode& RenderGraphBuilder::RequirePass(
    const PassHandle handle_) const {
    if (!IsValidPassHandle(handle_) || handle_.index >= passes.size()) {
        throw std::out_of_range("RenderGraphBuilder pass handle is out of range");
    }
    return passes[handle_.index];
}

void RenderGraphBuilder::AppendUnique(std::vector<PassHandle>& values_,
                                      const PassHandle value_) {
    const auto existing = std::find_if(values_.begin(),
                                       values_.end(),
                                       [&](const PassHandle current_) {
                                           return current_.index == value_.index;
                                       });
    if (existing == values_.end()) {
        values_.push_back(value_);
    }
}

void RenderGraphBuilder::AppendUnique(std::vector<AccessDesc>& values_,
                                      const AccessDesc& value_) {
    const auto existing = std::find_if(values_.begin(),
                                       values_.end(),
                                       [&](const AccessDesc& current_) {
                                           return current_.resource.resource_index == value_.resource.resource_index &&
                                                  current_.resource.version == value_.resource.version &&
                                                  current_.access == value_.access &&
                                                  current_.subresource_range.base_mip_level == value_.subresource_range.base_mip_level &&
                                                  current_.subresource_range.level_count == value_.subresource_range.level_count &&
                                                  current_.subresource_range.base_array_layer == value_.subresource_range.base_array_layer &&
                                                  current_.subresource_range.layer_count == value_.subresource_range.layer_count &&
                                                  current_.buffer_range.offset_bytes == value_.buffer_range.offset_bytes &&
                                                  current_.buffer_range.size_bytes == value_.buffer_range.size_bytes;
                                       });
    if (existing == values_.end()) {
        values_.push_back(value_);
    }
}

} // namespace vr::render_graph

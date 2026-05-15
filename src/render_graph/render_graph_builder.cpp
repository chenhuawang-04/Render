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

} // namespace

const CompiledPass* CompiledRenderGraph::FindPass(const PassHandle handle_) const noexcept {
    for (const auto& pass_ : passes) {
        if (pass_.handle.index == handle_.index) {
            return &pass_;
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
        oss << " deps=";
        for (std::size_t dep_index = 0; dep_index < pass_.dependencies.size(); ++dep_index) {
            if (dep_index != 0U) {
                oss << ',';
            }
            oss << pass_.dependencies[dep_index].index;
        }
        oss << " reads=" << pass_.reads.size();
        oss << " writes=" << pass_.writes.size() << '\n';
    }

    oss << "liveness=" << liveness_ranges.size() << '\n';
    for (const auto& range_ : liveness_ranges) {
        oss << "resource=" << range_.debug_name
            << " first=" << range_.first_pass_order
            << " last=" << range_.last_pass_order << '\n';
    }

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
                                       const bool side_effect_) {
    PassNode pass{};
    pass.handle = PassHandle{.index = static_cast<std::uint32_t>(passes.size())};
    pass.debug_name = std::string(debug_name_);
    pass.side_effect = side_effect_;
    passes.push_back(std::move(pass));
    return passes.back().handle;
}

ResourceVersionHandle RenderGraphBuilder::Read(const PassHandle pass_,
                                               const ResourceHandle resource_) {
    const ResourceNode& resource = RequireResource(resource_);
    return Read(pass_, ResourceVersionHandle{
        .resource_index = resource_.index,
        .version = resource.latest_version,
    });
}

ResourceVersionHandle RenderGraphBuilder::Read(const PassHandle pass_,
                                               const ResourceVersionHandle version_) {
    PassNode& pass = RequirePass(pass_);
    (void)RequireVersion(version_);

    auto& version_node = resources[version_.resource_index].versions[version_.version];
    AppendUnique(version_node.consumers, pass_);
    AppendUnique(pass.reads, version_);
    return version_;
}

ResourceVersionHandle RenderGraphBuilder::Write(const PassHandle pass_,
                                                const ResourceHandle resource_) {
    const ResourceNode& resource = RequireResource(resource_);
    return Write(pass_, ResourceVersionHandle{
        .resource_index = resource_.index,
        .version = resource.latest_version,
    });
}

ResourceVersionHandle RenderGraphBuilder::Write(const PassHandle pass_,
                                                const ResourceVersionHandle version_) {
    PassNode& pass = RequirePass(pass_);
    (void)RequireVersion(version_);

    ResourceNode& resource = resources[version_.resource_index];
    if (version_.version != resource.latest_version) {
        throw std::invalid_argument(
            "RenderGraphBuilder::Write currently requires the latest resource version");
    }

    (void)Read(pass_, version_);

    const ResourceVersionHandle output_version{
        .resource_index = version_.resource_index,
        .version = static_cast<std::uint32_t>(resource.versions.size()),
    };
    resource.versions.push_back(ResourceVersionNode{.producer = pass_});
    resource.latest_version = output_version.version;
    pass.writes.push_back(WriteRecord{
        .input = version_,
        .output = output_version,
    });
    return output_version;
}

CompiledRenderGraph RenderGraphBuilder::Compile() const {
    CompiledRenderGraph compiled{};
    if (passes.empty()) {
        return compiled;
    }

    std::vector<std::vector<PassHandle>> dependencies(passes.size());
    for (const auto& pass_ : passes) {
        auto& pass_dependencies = dependencies[pass_.handle.index];
        for (const auto read_ : pass_.reads) {
            const auto& version_node = resources[read_.resource_index].versions[read_.version];
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
        compiled_pass.reads = pass_.reads;
        for (const auto dependency_ : dependencies[handle_.index]) {
            if (active[dependency_.index]) {
                compiled_pass.dependencies.push_back(dependency_);
            }
        }
        for (const auto& write_ : pass_.writes) {
            compiled_pass.writes.push_back(write_.output);
        }
        compiled.passes.push_back(std::move(compiled_pass));
    }

    for (std::uint32_t resource_index = 0U;
         resource_index < static_cast<std::uint32_t>(resources.size());
         ++resource_index) {
        const auto& resource = resources[resource_index];
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
    }

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

void RenderGraphBuilder::AppendUnique(std::vector<ResourceVersionHandle>& values_,
                                      const ResourceVersionHandle value_) {
    const auto existing = std::find_if(values_.begin(),
                                       values_.end(),
                                       [&](const ResourceVersionHandle current_) {
                                           return current_.resource_index == value_.resource_index &&
                                                  current_.version == value_.version;
                                       });
    if (existing == values_.end()) {
        values_.push_back(value_);
    }
}

} // namespace vr::render_graph

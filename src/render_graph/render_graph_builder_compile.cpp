#include "render_graph_builder_internal.hpp"

#include <algorithm>
#include <stdexcept>

namespace vr::render_graph {
using namespace builder_detail;

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
        compiled_pass.compile_hints = pass_.compile_hints;
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
        if (compiled_pass.descriptor_bindings.empty()) {
            if (pass_.shader_contract.has_value() &&
                !pass_.shader_contract->bindings.empty()) {
                throw std::runtime_error(
                    "RenderGraphBuilder::Compile pass '" + pass_.debug_name +
                    "' declared a shader contract but did not declare descriptor bindings");
            }
        } else {
            if (!pass_.shader_contract.has_value()) {
                throw std::runtime_error(
                    "RenderGraphBuilder::Compile pass '" + pass_.debug_name +
                    "' declared descriptor bindings without a shader contract");
            }

            const auto& shader_contract = *pass_.shader_contract;
            for (const auto& actual_binding : compiled_pass.descriptor_bindings) {
                const auto expected_it = std::find_if(
                    shader_contract.bindings.begin(),
                    shader_contract.bindings.end(),
                    [&](const ShaderContractBindingDesc& expected_) {
                        return expected_.set == actual_binding.set &&
                               expected_.binding == actual_binding.binding;
                    });
                if (expected_it == shader_contract.bindings.end()) {
                    throw std::runtime_error(
                        "RenderGraphBuilder::Compile pass '" + pass_.debug_name +
                        "' declared extra descriptor binding set=" +
                        std::to_string(actual_binding.set) + " binding=" +
                        std::to_string(actual_binding.binding) + " outside contract '" +
                        shader_contract.debug_name + "'");
                }
                if (expected_it->kind != actual_binding.kind) {
                    throw std::runtime_error(
                        "RenderGraphBuilder::Compile pass '" + pass_.debug_name +
                        "' has descriptor binding kind mismatch at set=" +
                        std::to_string(actual_binding.set) + " binding=" +
                        std::to_string(actual_binding.binding) + " for contract '" +
                        shader_contract.debug_name + "'");
                }
                if (expected_it->stage_flags != actual_binding.stage_flags) {
                    throw std::runtime_error(
                        "RenderGraphBuilder::Compile pass '" + pass_.debug_name +
                        "' has descriptor stage mismatch at set=" +
                        std::to_string(actual_binding.set) + " binding=" +
                        std::to_string(actual_binding.binding) + " for contract '" +
                        shader_contract.debug_name + "'");
                }
            }
        }
        for (const auto dependency_ : dependencies[handle_.index]) {
            if (active[dependency_.index]) {
                compiled_pass.dependencies.push_back(dependency_);
            }
        }
        for (const auto& write_ : pass_.writes) {
            compiled_pass.writes.push_back(write_.access);
        }
        if (!compiled_pass.descriptor_bindings.empty()) {
            std::vector<PassDescriptorBindingDesc> layout_bindings{};
            if (pass_.shader_contract.has_value()) {
                layout_bindings.reserve(pass_.shader_contract->bindings.size());
                for (const auto& contract_binding : pass_.shader_contract->bindings) {
                    layout_bindings.push_back({
                        .set = contract_binding.set,
                        .binding = contract_binding.binding,
                        .source = DescriptorBindingSource::none,
                        .kind = contract_binding.kind,
                        .stage_flags = contract_binding.stage_flags,
                        .source_id = 0U,
                    });
                }
            } else {
                layout_bindings = compiled_pass.descriptor_bindings;
            }
            compiled.descriptor_plan.pass_layouts.push_back(PassDescriptorLayout{
                .pass = handle_,
                .bindings = std::move(layout_bindings),
            });

            DescriptorWriteBatch write_batch{};
            write_batch.pass = handle_;
            write_batch.writes.reserve(compiled_pass.descriptor_bindings.size());
            for (const auto& binding_ : compiled_pass.descriptor_bindings) {
                write_batch.writes.push_back(DescriptorWriteDesc{
                    .set = binding_.set,
                    .binding = binding_.binding,
                    .source = binding_.source,
                    .kind = binding_.kind,
                    .stage_flags = binding_.stage_flags,
                    .source_id = binding_.source_id,
                });
                if (binding_.source == DescriptorBindingSource::bindless_table) {
                    const auto existing_allocation = std::find_if(
                        compiled.descriptor_plan.bindless_allocations.begin(),
                        compiled.descriptor_plan.bindless_allocations.end(),
                        [&](const BindlessAllocation& allocation_) {
                            return allocation_.table_id == binding_.source_id &&
                                   allocation_.kind == binding_.kind &&
                                   allocation_.stage_flags == binding_.stage_flags;
                        });
                    if (existing_allocation == compiled.descriptor_plan.bindless_allocations.end()) {
                        compiled.descriptor_plan.bindless_allocations.push_back(BindlessAllocation{
                            .table_id = binding_.source_id,
                            .kind = binding_.kind,
                            .stage_flags = binding_.stage_flags,
                        });
                    }
                }
            }
            compiled.descriptor_plan.writes.push_back(std::move(write_batch));
        }
        compiled.passes.push_back(std::move(compiled_pass));
    }

    std::sort(compiled.descriptor_plan.bindless_allocations.begin(),
              compiled.descriptor_plan.bindless_allocations.end(),
              [](const BindlessAllocation& lhs_,
                 const BindlessAllocation& rhs_) {
                  if (lhs_.table_id != rhs_.table_id) {
                      return lhs_.table_id < rhs_.table_id;
                  }
                  if (lhs_.kind != rhs_.kind) {
                      return static_cast<std::uint32_t>(lhs_.kind) <
                             static_cast<std::uint32_t>(rhs_.kind);
                  }
                  return lhs_.stage_flags < rhs_.stage_flags;
              });

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

    compiled.native_pass_plan =
        BuildNativePassPlan(compiled, native_pass_planner_config);
    compiled.external_buffer_binding_resolvers = external_buffer_binding_resolvers;
    compiled.transient_allocation_plan = BuildTransientAllocationPlan(compiled);
    compiled.barrier_plan = BuildBarrierPlan(compiled);
    return compiled;
}

} // namespace vr::render_graph

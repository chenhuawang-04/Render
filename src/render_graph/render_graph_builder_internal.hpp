#pragma once

#include "vr/render_graph/render_graph_builder.hpp"

#include <sstream>
#include <string>
#include <vector>

namespace vr::render_graph::builder_detail {

[[nodiscard]] std::string BuildVersionDebugName(const std::string& base_name_,
                                                std::uint32_t version_);
[[nodiscard]] const char* ResourceKindToString(ResourceKind kind_) noexcept;
[[nodiscard]] const char* ResourceLifetimeToString(ResourceLifetime lifetime_) noexcept;
[[nodiscard]] const char* QueueClassToString(QueueClass queue_) noexcept;
[[nodiscard]] const char* DescriptorBindingSourceToString(
    DescriptorBindingSource source_) noexcept;
[[nodiscard]] const char* DescriptorBindingKindToString(
    DescriptorBindingKind kind_) noexcept;
[[nodiscard]] std::string BuildShaderStageFlagsString(std::uint32_t stage_flags_);
[[nodiscard]] const char* AccessKindToString(AccessKind access_) noexcept;
[[nodiscard]] std::string EscapeJsonString(const std::string& value_);
[[nodiscard]] std::string EscapeDotLabel(const std::string& value_);
[[nodiscard]] std::string MakeResourceNodeId(ResourceVersionHandle version_);
[[nodiscard]] const CompiledResourceVersionLiveness* FindLivenessRange(
    const std::vector<CompiledResourceVersionLiveness>& liveness_ranges_,
    ResourceVersionHandle version_);
[[nodiscard]] bool HasExplicitSubresourceRange(const SubresourceRange& range_) noexcept;
[[nodiscard]] bool HasExplicitBufferRange(const BufferRange& range_) noexcept;
[[nodiscard]] AccessDesc BindAccessDesc(ResourceVersionHandle version_,
                                        ResourceKind kind_,
                                        const AccessDesc& access_) noexcept;
void AppendJsonAccessDesc(std::ostringstream& oss_,
                          const AccessDesc& access_,
                          ResourceKind kind_);
void AppendJsonDescriptorBinding(std::ostringstream& oss_,
                                 const PassDescriptorBindingDesc& binding_);
void AppendJsonDescriptorWrite(std::ostringstream& oss_,
                               const DescriptorWriteDesc& write_);
void AppendJsonBindlessAllocation(std::ostringstream& oss_,
                                  const BindlessAllocation& allocation_);
[[nodiscard]] std::string BuildDescriptorLayoutDotLabel(
    const PassDescriptorBindingDesc& binding_);
[[nodiscard]] std::string BuildAccessDotLabel(const AccessDesc& access_,
                                              ResourceKind kind_);
void AppendTransientAllocationJson(std::ostringstream& oss_,
                                   const TransientAllocationPlan& plan_);

} // namespace vr::render_graph::builder_detail

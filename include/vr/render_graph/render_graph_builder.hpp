#pragma once

#include "vr/render_graph/compiled_render_graph.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace vr::render_graph {

class RenderGraphBuilder final {
public:
    [[nodiscard]] ResourceHandle CreateTexture(std::string_view debug_name_,
                                               const TextureDesc& desc_,
                                               ResourceLifetime lifetime_ =
                                                   ResourceLifetime::transient);

    [[nodiscard]] ResourceHandle CreateBuffer(std::string_view debug_name_,
                                              const BufferDesc& desc_,
                                              ResourceLifetime lifetime_ =
                                                  ResourceLifetime::transient);

    [[nodiscard]] PassHandle AddPass(std::string_view debug_name_,
                                     bool side_effect_ = false);

    [[nodiscard]] ResourceVersionHandle Read(PassHandle pass_, ResourceHandle resource_);
    [[nodiscard]] ResourceVersionHandle Read(PassHandle pass_, ResourceVersionHandle version_);

    [[nodiscard]] ResourceVersionHandle Write(PassHandle pass_, ResourceHandle resource_);
    [[nodiscard]] ResourceVersionHandle Write(PassHandle pass_, ResourceVersionHandle version_);

    [[nodiscard]] CompiledRenderGraph Compile() const;

    void Reset() noexcept;

    [[nodiscard]] std::size_t ResourceCount() const noexcept {
        return resources.size();
    }

    [[nodiscard]] std::size_t PassCount() const noexcept {
        return passes.size();
    }

private:
    struct ResourceVersionNode final {
        PassHandle producer{};
        std::vector<PassHandle> consumers{};
    };

    struct ResourceNode final {
        ResourceKind kind = ResourceKind::buffer;
        ResourceLifetime lifetime = ResourceLifetime::transient;
        std::string debug_name{};
        TextureDesc texture{};
        BufferDesc buffer{};
        std::vector<ResourceVersionNode> versions{};
        std::uint32_t latest_version = 0U;
    };

    struct WriteRecord final {
        ResourceVersionHandle input{};
        ResourceVersionHandle output{};
    };

    struct PassNode final {
        PassHandle handle{};
        std::string debug_name{};
        bool side_effect = false;
        std::vector<ResourceVersionHandle> reads{};
        std::vector<WriteRecord> writes{};
    };

    [[nodiscard]] ResourceNode& RequireResource(ResourceHandle handle_);
    [[nodiscard]] const ResourceNode& RequireResource(ResourceHandle handle_) const;
    [[nodiscard]] const ResourceNode& RequireVersion(ResourceVersionHandle handle_) const;
    [[nodiscard]] PassNode& RequirePass(PassHandle handle_);
    [[nodiscard]] const PassNode& RequirePass(PassHandle handle_) const;

    static void AppendUnique(std::vector<PassHandle>& values_, PassHandle value_);
    static void AppendUnique(std::vector<ResourceVersionHandle>& values_,
                             ResourceVersionHandle value_);

private:
    std::vector<ResourceNode> resources{};
    std::vector<PassNode> passes{};
};

} // namespace vr::render_graph

#pragma once

#include "vr/runtime/runtime_service.hpp"
#include "vr/runtime/service_dependency.hpp"

#include <string_view>

namespace vr::runtime::services {

class CommandService final {
public:
    using CreateInfo = vr::runtime::EmptyServiceCreateInfo;
    using Dependencies = vr::runtime::DependsOn<>;
    static constexpr std::string_view Name = "CommandService";

    void BindAvailable(bool available_ = true) noexcept {
        available = available_;
    }

    void Reset() noexcept {
        available = false;
    }

    [[nodiscard]] bool IsAvailable() const noexcept {
        return available;
    }

private:
    bool available = false;
};

} // namespace vr::runtime::services


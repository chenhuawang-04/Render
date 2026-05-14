#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace vr::runtime::services::detail {

template<typename HostT>
class BoundHostService {
public:
    void Bind(HostT& host_) noexcept {
        host = &host_;
    }

    void Bind(HostT* host_) noexcept {
        host = host_;
    }

    void Reset() noexcept {
        host = nullptr;
    }

    [[nodiscard]] bool IsAvailable() const noexcept {
        return host != nullptr;
    }

    [[nodiscard]] HostT* HostPtr() noexcept {
        return host;
    }

    [[nodiscard]] const HostT* HostPtr() const noexcept {
        return host;
    }

protected:
    [[nodiscard]] HostT& RequireHost(std::string_view service_name_) {
        if (host == nullptr) {
            throw std::runtime_error(std::string(service_name_) + " is not available");
        }
        return *host;
    }

    [[nodiscard]] const HostT& RequireHost(std::string_view service_name_) const {
        if (host == nullptr) {
            throw std::runtime_error(std::string(service_name_) + " is not available");
        }
        return *host;
    }

private:
    HostT* host = nullptr;
};

} // namespace vr::runtime::services::detail


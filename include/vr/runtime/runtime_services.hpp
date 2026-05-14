#pragma once

#include "vr/runtime/runtime_profile.hpp"
#include "vr/runtime/runtime_service.hpp"

#include <stdexcept>
#include <string>
#include <tuple>

namespace vr::runtime {

template<typename ProfileT>
class RuntimeServices;

template<typename... ServiceTs>
class RuntimeServices<RuntimeProfile<ServiceTs...>> final {
public:
    using Profile = RuntimeProfile<ServiceTs...>;
    using PointerTuple = std::tuple<ServiceTs*...>;

    static_assert((RuntimeService<ServiceTs> && ...),
                  "RuntimeServices requires every profile entry to satisfy RuntimeService");
    static_assert((profile_satisfies_service_dependencies_v<Profile, ServiceTs> && ...),
                  "RuntimeServices profile does not satisfy one or more service dependencies");

    RuntimeServices() = default;

    template<typename... BoundServiceTs>
    void Bind(BoundServiceTs&... services_) noexcept {
        (Set(services_), ...);
    }

    void Reset() noexcept {
        pointers = PointerTuple{static_cast<ServiceTs*>(nullptr)...};
    }

    [[nodiscard]] bool Empty() const noexcept {
        return ((std::get<ServiceTs*>(pointers) == nullptr) && ...);
    }

    template<typename ServiceT>
    [[nodiscard]] static consteval bool Contains() noexcept {
        return profile_contains_v<Profile, ServiceT>;
    }

    template<typename ServiceT>
    [[nodiscard]] ServiceT* TryGet() noexcept {
        static_assert(Contains<ServiceT>(), "Requested service is not part of this RuntimeProfile");
        return std::get<ServiceT*>(pointers);
    }

    template<typename ServiceT>
    [[nodiscard]] const ServiceT* TryGet() const noexcept {
        static_assert(Contains<ServiceT>(), "Requested service is not part of this RuntimeProfile");
        return std::get<ServiceT*>(pointers);
    }

    template<typename ServiceT>
    [[nodiscard]] ServiceT& Get() {
        if (ServiceT* service_ = TryGet<ServiceT>()) {
            return *service_;
        }
        throw std::runtime_error(std::string(ServiceT::Name) + " is not bound");
    }

    template<typename ServiceT>
    [[nodiscard]] const ServiceT& Get() const {
        if (const ServiceT* service_ = TryGet<ServiceT>()) {
            return *service_;
        }
        throw std::runtime_error(std::string(ServiceT::Name) + " is not bound");
    }

    template<typename FnT>
    void ForEachBound(FnT&& function_) {
        ForEachBoundImpl(function_);
    }

    template<typename FnT>
    void ForEachBound(FnT&& function_) const {
        ForEachBoundImpl(function_);
    }

    template<typename FnT>
    void ForEachBoundReverse(FnT&& function_) {
        ForEachBoundReverseImpl<sizeof...(ServiceTs)>(function_);
    }

    template<typename FnT>
    void ForEachBoundReverse(FnT&& function_) const {
        ForEachBoundReverseImpl<sizeof...(ServiceTs)>(function_);
    }

    template<typename ContextT>
    void Initialize(ContextT& context_) {
        ForEachBound([&](auto& service_) {
            CallInitializeIfSupported(service_, context_);
        });
    }

    template<typename ContextT>
    void PostInitialize(ContextT& context_) {
        ForEachBound([&](auto& service_) {
            CallPostInitializeIfSupported(service_, context_);
        });
    }

    template<typename ContextT>
    void BeginFrame(ContextT& context_) {
        ForEachBound([&](auto& service_) {
            CallBeginFrameIfSupported(service_, context_);
        });
    }

    template<typename ContextT>
    void PrepareFrame(ContextT& context_) {
        ForEachBound([&](auto& service_) {
            CallPrepareFrameIfSupported(service_, context_);
        });
    }

    template<typename ContextT>
    void PreRecord(ContextT& context_) {
        ForEachBound([&](auto& service_) {
            CallPreRecordIfSupported(service_, context_);
        });
    }

    template<typename ContextT>
    void PostRecord(ContextT& context_) {
        ForEachBound([&](auto& service_) {
            CallPostRecordIfSupported(service_, context_);
        });
    }

    template<typename ContextT>
    void EndFrame(ContextT& context_) {
        ForEachBoundReverse([&](auto& service_) {
            CallEndFrameIfSupported(service_, context_);
        });
    }

    template<typename ContextT>
    void Retire(ContextT& context_) {
        ForEachBoundReverse([&](auto& service_) {
            CallRetireIfSupported(service_, context_);
        });
    }

    template<typename ContextT>
    void Shutdown(ContextT& context_) {
        ForEachBoundReverse([&](auto& service_) {
            CallShutdownIfSupported(service_, context_);
        });
    }

private:
    template<typename ServiceT>
    void Set(ServiceT& service_) noexcept {
        static_assert(Contains<ServiceT>(), "Bound service is not part of this RuntimeProfile");
        std::get<ServiceT*>(pointers) = &service_;
    }

    template<typename FnT, std::size_t index_v = 0U>
    void ForEachBoundImpl(FnT& function_) {
        if constexpr (index_v < sizeof...(ServiceTs)) {
            if (auto* service_ = std::get<index_v>(pointers)) {
                function_(*service_);
            }
            ForEachBoundImpl<FnT, index_v + 1U>(function_);
        }
    }

    template<typename FnT, std::size_t index_v = 0U>
    void ForEachBoundImpl(FnT& function_) const {
        if constexpr (index_v < sizeof...(ServiceTs)) {
            if (const auto* service_ = std::get<index_v>(pointers)) {
                function_(*service_);
            }
            ForEachBoundImpl<FnT, index_v + 1U>(function_);
        }
    }

    template<std::size_t index_v, typename FnT>
    void ForEachBoundReverseImpl(FnT& function_) {
        if constexpr (index_v > 0U) {
            if (auto* service_ = std::get<index_v - 1U>(pointers)) {
                function_(*service_);
            }
            ForEachBoundReverseImpl<index_v - 1U>(function_);
        }
    }

    template<std::size_t index_v, typename FnT>
    void ForEachBoundReverseImpl(FnT& function_) const {
        if constexpr (index_v > 0U) {
            if (const auto* service_ = std::get<index_v - 1U>(pointers)) {
                function_(*service_);
            }
            ForEachBoundReverseImpl<index_v - 1U>(function_);
        }
    }

private:
    PointerTuple pointers{};
};

} // namespace vr::runtime


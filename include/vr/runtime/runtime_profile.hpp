#pragma once

#include "vr/runtime/service_dependency.hpp"

#include <tuple>
#include <type_traits>

namespace vr::runtime {

template<typename... Services>
struct RuntimeProfile final {
    using ServiceTuple = std::tuple<Services...>;

    static constexpr std::size_t service_count = sizeof...(Services);

    template<typename ServiceT>
    static constexpr bool contains = (std::is_same_v<ServiceT, Services> || ...);
};

template<typename ProfileT, typename ServiceT>
inline constexpr bool profile_contains_v = ProfileT::template contains<ServiceT>;

template<typename ProfileT, typename DependencyListT>
struct ProfileSatisfiesDependencies;

template<typename ProfileT, typename... Dependencies>
struct ProfileSatisfiesDependencies<ProfileT, DependsOn<Dependencies...>>
    : std::bool_constant<(profile_contains_v<ProfileT, Dependencies> && ...)> {};

template<typename ProfileT, typename ServiceT>
inline constexpr bool profile_satisfies_service_dependencies_v =
    ProfileSatisfiesDependencies<ProfileT, typename ServiceT::Dependencies>::value;

} // namespace vr::runtime

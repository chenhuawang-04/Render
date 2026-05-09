#pragma once

#include <type_traits>

namespace vr::runtime {

template<typename... Services>
struct DependsOn final {};

template<typename T, typename DependencyListT>
struct DependencyListContains;

template<typename T, typename... Dependencies>
struct DependencyListContains<T, DependsOn<Dependencies...>>
    : std::bool_constant<(std::is_same_v<T, Dependencies> || ...)> {};

template<typename T, typename DependencyListT>
inline constexpr bool dependency_list_contains_v =
    DependencyListContains<T, DependencyListT>::value;

template<typename ServiceT, typename DependencyT>
inline constexpr bool service_depends_on_v =
    dependency_list_contains_v<DependencyT, typename ServiceT::Dependencies>;

} // namespace vr::runtime

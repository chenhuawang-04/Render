#pragma once

#include <compare>
#include <cstdint>
#include <type_traits>

namespace vr::runtime::detail {

#define VR_DEFINE_RUNTIME_INGRESS_ID_TYPE(type_name_, underlying_type_)                           \
    struct type_name_ final {                                                                     \
        using underlying_type = underlying_type_;                                                 \
                                                                                                  \
        static constexpr underlying_type invalid_value = static_cast<underlying_type>(0);         \
                                                                                                  \
        underlying_type value = invalid_value;                                                    \
                                                                                                  \
        constexpr type_name_() noexcept = default;                                                \
        constexpr type_name_(underlying_type value_) noexcept                                     \
            : value(value_) {}                                                                    \
                                                                                                  \
        [[nodiscard]] constexpr bool IsValid() const noexcept {                                   \
            return value != invalid_value;                                                        \
        }                                                                                         \
                                                                                                  \
        [[nodiscard]] static constexpr type_name_ Invalid() noexcept {                            \
            return {};                                                                            \
        }                                                                                         \
                                                                                                  \
        constexpr operator underlying_type() const noexcept {                                     \
            return value;                                                                         \
        }                                                                                         \
                                                                                                  \
        friend constexpr bool operator==(const type_name_&,                                       \
                                         const type_name_&) noexcept = default;                   \
                                                                                                  \
        friend constexpr bool operator==(const type_name_& lhs_,                                  \
                                         underlying_type rhs_) noexcept {                         \
            return lhs_.value == rhs_;                                                            \
        }                                                                                         \
                                                                                                  \
        friend constexpr bool operator==(underlying_type lhs_,                                    \
                                         const type_name_& rhs_) noexcept {                       \
            return lhs_ == rhs_.value;                                                            \
        }                                                                                         \
                                                                                                  \
        constexpr auto operator<=>(const type_name_&) const noexcept = default;                   \
    };                                                                                            \
    static_assert(std::is_standard_layout_v<type_name_>);                                         \
    static_assert(std::is_trivially_copyable_v<type_name_>);                                      \
    static_assert(sizeof(type_name_) == sizeof(underlying_type_))

} // namespace vr::runtime::detail

namespace vr::asset {

VR_DEFINE_RUNTIME_INGRESS_ID_TYPE(TextureId, std::uint32_t);

} // namespace vr::asset

namespace vr::geometry {

VR_DEFINE_RUNTIME_INGRESS_ID_TYPE(GeometryResourceId, std::uint32_t);
VR_DEFINE_RUNTIME_INGRESS_ID_TYPE(GeometryImageId, std::uint32_t);
VR_DEFINE_RUNTIME_INGRESS_ID_TYPE(GeometryAppearanceId, std::uint32_t);

} // namespace vr::geometry

namespace vr::surface {

VR_DEFINE_RUNTIME_INGRESS_ID_TYPE(SurfaceImageId, std::uint32_t);

} // namespace vr::surface

namespace vr::render {

VR_DEFINE_RUNTIME_INGRESS_ID_TYPE(IblEnvironmentId, std::uint32_t);
VR_DEFINE_RUNTIME_INGRESS_ID_TYPE(SceneSubmissionId, std::uint64_t);

} // namespace vr::render

#undef VR_DEFINE_RUNTIME_INGRESS_ID_TYPE

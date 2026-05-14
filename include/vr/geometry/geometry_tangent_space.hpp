#pragma once

#include "vr/ecs/system/spatial_math.hpp"
#include "vr/geometry/geometry_types.hpp"

#include <cmath>
#include <cstdint>

namespace vr::geometry {

enum class GeometryTangentBuildStatus : std::uint8_t {
    unchanged = 0U,
    normalized_existing = 1U,
    generated = 2U,
};

struct GeometryTangentBuildResult final {
    GeometryTangentBuildStatus status = GeometryTangentBuildStatus::unchanged;
    bool used_fallback_basis = false;
    bool encountered_degenerate_uv = false;
};

namespace detail {

inline constexpr float k_tangent_epsilon = 1e-6F;

[[nodiscard]] inline bool IsFinite(float value_) noexcept {
    return std::isfinite(value_);
}

[[nodiscard]] inline bool IsFiniteVec3(const ecs::Float3& value_) noexcept {
    return IsFinite(value_.x) && IsFinite(value_.y) && IsFinite(value_.z);
}

[[nodiscard]] inline ecs::Float3 MakeFloat3(float x_, float y_, float z_) noexcept {
    return ecs::Float3{.x = x_, .y = y_, .z = z_};
}

[[nodiscard]] inline ecs::Float2 MakeFloat2(float x_, float y_) noexcept {
    return ecs::Float2{.x = x_, .y = y_};
}

[[nodiscard]] inline ecs::Float3 Add3(const ecs::Float3& lhs_,
                                      const ecs::Float3& rhs_) noexcept {
    return MakeFloat3(lhs_.x + rhs_.x,
                      lhs_.y + rhs_.y,
                      lhs_.z + rhs_.z);
}

[[nodiscard]] inline ecs::Float3 Sub3(const ecs::Float3& lhs_,
                                      const ecs::Float3& rhs_) noexcept {
    return MakeFloat3(lhs_.x - rhs_.x,
                      lhs_.y - rhs_.y,
                      lhs_.z - rhs_.z);
}

[[nodiscard]] inline ecs::Float3 Mul3(const ecs::Float3& value_,
                                      float scalar_) noexcept {
    return MakeFloat3(value_.x * scalar_,
                      value_.y * scalar_,
                      value_.z * scalar_);
}

[[nodiscard]] inline float Dot2(const ecs::Float2& lhs_,
                                const ecs::Float2& rhs_) noexcept {
    return lhs_.x * rhs_.x + lhs_.y * rhs_.y;
}

[[nodiscard]] inline float LengthSquared3(const ecs::Float3& value_) noexcept {
    return ecs::spatial_math::Dot3(value_, value_);
}

[[nodiscard]] inline ecs::Float3 NormalFromVertex(const GeometryMeshVertex& vertex_) noexcept {
    const ecs::Float3 source = MakeFloat3(vertex_.normal_x,
                                          vertex_.normal_y,
                                          vertex_.normal_z);
    return ecs::spatial_math::Normalize3(source,
                                         MakeFloat3(0.0F, 0.0F, 1.0F));
}

[[nodiscard]] inline ecs::Float3 PositionFromVertex(const GeometryMeshVertex& vertex_) noexcept {
    return MakeFloat3(vertex_.position_x,
                      vertex_.position_y,
                      vertex_.position_z);
}

[[nodiscard]] inline ecs::Float2 UvFromVertex(const GeometryMeshVertex& vertex_) noexcept {
    return MakeFloat2(vertex_.uv_u, vertex_.uv_v);
}

[[nodiscard]] inline ecs::Float3 BuildFallbackTangent(const ecs::Float3& normal_) noexcept {
    const ecs::Float3 axis_hint = (std::fabs(normal_.z) < 0.999F)
        ? MakeFloat3(0.0F, 0.0F, 1.0F)
        : MakeFloat3(0.0F, 1.0F, 0.0F);
    const ecs::Float3 tangent = ecs::spatial_math::Cross3(axis_hint, normal_);
    return ecs::spatial_math::Normalize3(tangent, MakeFloat3(1.0F, 0.0F, 0.0F));
}

[[nodiscard]] inline bool HasUsableTangent(const GeometryMeshVertex& vertex_) noexcept {
    const ecs::Float3 tangent = MakeFloat3(vertex_.tangent_x,
                                           vertex_.tangent_y,
                                           vertex_.tangent_z);
    return IsFiniteVec3(tangent) &&
           IsFinite(vertex_.tangent_w) &&
           LengthSquared3(tangent) > k_tangent_epsilon &&
           std::fabs(vertex_.tangent_w) > k_tangent_epsilon;
}

inline void WriteNormalizedTangent(GeometryMeshVertex& vertex_,
                                   const ecs::Float3& normal_,
                                   const ecs::Float3& tangent_accum_,
                                   const ecs::Float3& bitangent_accum_,
                                   bool& out_used_fallback_) noexcept {
    ecs::Float3 tangent = tangent_accum_;
    tangent = Sub3(tangent,
                   Mul3(normal_, ecs::spatial_math::Dot3(normal_, tangent)));

    if (!IsFiniteVec3(tangent) || LengthSquared3(tangent) <= k_tangent_epsilon) {
        tangent = BuildFallbackTangent(normal_);
        out_used_fallback_ = true;
    } else {
        tangent = ecs::spatial_math::Normalize3(tangent, BuildFallbackTangent(normal_));
    }

    ecs::Float3 bitangent = bitangent_accum_;
    if (!IsFiniteVec3(bitangent) || LengthSquared3(bitangent) <= k_tangent_epsilon) {
        bitangent = ecs::spatial_math::Cross3(normal_, tangent);
        out_used_fallback_ = true;
    }

    const ecs::Float3 derived_bitangent = ecs::spatial_math::Cross3(normal_, tangent);
    const float handedness = ecs::spatial_math::Dot3(derived_bitangent, bitangent) < 0.0F
        ? -1.0F
        : 1.0F;

    vertex_.tangent_x = tangent.x;
    vertex_.tangent_y = tangent.y;
    vertex_.tangent_z = tangent.z;
    vertex_.tangent_w = handedness;
}

} // namespace detail

[[nodiscard]] inline GeometryTangentBuildResult PrepareGeometryMeshTangents(
    GeometryMeshVertex* vertices_,
    std::uint32_t vertex_count_,
    const std::uint32_t* indices_,
    std::uint32_t index_count_,
    VkPrimitiveTopology topology_) {
    GeometryTangentBuildResult result{};
    if (vertices_ == nullptr || vertex_count_ == 0U) {
        return result;
    }

    bool requires_generation = false;
    for (std::uint32_t index = 0U; index < vertex_count_; ++index) {
        if (!detail::HasUsableTangent(vertices_[index])) {
            requires_generation = true;
            break;
        }
    }

    if (!requires_generation) {
        result.status = GeometryTangentBuildStatus::normalized_existing;
        for (std::uint32_t index = 0U; index < vertex_count_; ++index) {
            GeometryMeshVertex& vertex = vertices_[index];
            const ecs::Float3 normal = detail::NormalFromVertex(vertex);
            const ecs::Float3 tangent = detail::MakeFloat3(vertex.tangent_x,
                                                           vertex.tangent_y,
                                                           vertex.tangent_z);
            const ecs::Float3 bitangent = ecs::spatial_math::Cross3(normal, tangent);
            detail::WriteNormalizedTangent(vertex,
                                           normal,
                                           tangent,
                                           detail::Mul3(bitangent, vertex.tangent_w),
                                           result.used_fallback_basis);
        }
        return result;
    }

    result.status = GeometryTangentBuildStatus::generated;
    if (topology_ != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST ||
        indices_ == nullptr ||
        index_count_ < 3U) {
        for (std::uint32_t index = 0U; index < vertex_count_; ++index) {
            GeometryMeshVertex& vertex = vertices_[index];
            const ecs::Float3 normal = detail::NormalFromVertex(vertex);
            detail::WriteNormalizedTangent(vertex,
                                           normal,
                                           detail::BuildFallbackTangent(normal),
                                           ecs::spatial_math::Cross3(normal,
                                                                     detail::BuildFallbackTangent(normal)),
                                           result.used_fallback_basis);
        }
        return result;
    }

    GeometryMcVector<ecs::Float3> tangent_scratch{};
    GeometryMcVector<ecs::Float3> bitangent_scratch{};
    tangent_scratch.resize(vertex_count_);
    bitangent_scratch.resize(vertex_count_);
    for (std::uint32_t index = 0U; index < vertex_count_; ++index) {
        tangent_scratch[index] = detail::MakeFloat3(0.0F, 0.0F, 0.0F);
        bitangent_scratch[index] = detail::MakeFloat3(0.0F, 0.0F, 0.0F);
    }

    for (std::uint32_t index = 0U; (index + 2U) < index_count_; index += 3U) {
        const std::uint32_t i0 = indices_[index + 0U];
        const std::uint32_t i1 = indices_[index + 1U];
        const std::uint32_t i2 = indices_[index + 2U];
        if (i0 >= vertex_count_ || i1 >= vertex_count_ || i2 >= vertex_count_) {
            continue;
        }

        const GeometryMeshVertex& v0 = vertices_[i0];
        const GeometryMeshVertex& v1 = vertices_[i1];
        const GeometryMeshVertex& v2 = vertices_[i2];

        const ecs::Float3 p0 = detail::PositionFromVertex(v0);
        const ecs::Float3 p1 = detail::PositionFromVertex(v1);
        const ecs::Float3 p2 = detail::PositionFromVertex(v2);
        const ecs::Float2 uv0 = detail::UvFromVertex(v0);
        const ecs::Float2 uv1 = detail::UvFromVertex(v1);
        const ecs::Float2 uv2 = detail::UvFromVertex(v2);

        const ecs::Float3 edge1 = detail::Sub3(p1, p0);
        const ecs::Float3 edge2 = detail::Sub3(p2, p0);
        const ecs::Float2 duv1 = detail::MakeFloat2(uv1.x - uv0.x, uv1.y - uv0.y);
        const ecs::Float2 duv2 = detail::MakeFloat2(uv2.x - uv0.x, uv2.y - uv0.y);

        const float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
        if (!detail::IsFinite(determinant) ||
            std::fabs(determinant) <= detail::k_tangent_epsilon) {
            result.encountered_degenerate_uv = true;
            continue;
        }

        const float inv_det = 1.0F / determinant;
        const ecs::Float3 triangle_tangent = detail::Mul3(
            detail::Sub3(detail::Mul3(edge1, duv2.y),
                         detail::Mul3(edge2, duv1.y)),
            inv_det);
        const ecs::Float3 triangle_bitangent = detail::Mul3(
            detail::Sub3(detail::Mul3(edge2, duv1.x),
                         detail::Mul3(edge1, duv2.x)),
            inv_det);

        tangent_scratch[i0] = detail::Add3(tangent_scratch[i0], triangle_tangent);
        tangent_scratch[i1] = detail::Add3(tangent_scratch[i1], triangle_tangent);
        tangent_scratch[i2] = detail::Add3(tangent_scratch[i2], triangle_tangent);
        bitangent_scratch[i0] = detail::Add3(bitangent_scratch[i0], triangle_bitangent);
        bitangent_scratch[i1] = detail::Add3(bitangent_scratch[i1], triangle_bitangent);
        bitangent_scratch[i2] = detail::Add3(bitangent_scratch[i2], triangle_bitangent);
    }

    for (std::uint32_t index = 0U; index < vertex_count_; ++index) {
        GeometryMeshVertex& vertex = vertices_[index];
        const ecs::Float3 normal = detail::NormalFromVertex(vertex);
        detail::WriteNormalizedTangent(vertex,
                                       normal,
                                       tangent_scratch[index],
                                       bitangent_scratch[index],
                                       result.used_fallback_basis);
    }

    return result;
}

} // namespace vr::geometry


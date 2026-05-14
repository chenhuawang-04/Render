#include "support/test_framework.hpp"
#include "vr/geometry/geometry_tangent_space.hpp"

#include <array>
#include <cmath>

namespace {

[[nodiscard]] bool NearlyEqual(float lhs_,
                               float rhs_,
                               float epsilon_ = 1e-4F) noexcept {
    return std::fabs(lhs_ - rhs_) <= epsilon_;
}

VR_TEST_CASE(GeometryTangentSpace_generates_expected_basis_for_planar_quad,
             "unit;geometry;tangent") {
    std::array<vr::geometry::GeometryMeshVertex, 4U> vertices{
        vr::geometry::GeometryMeshVertex{
            .position_x = -0.5F, .position_y = -0.5F, .position_z = 0.0F,
            .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F,
            .uv_u = 0.0F, .uv_v = 0.0F
        },
        vr::geometry::GeometryMeshVertex{
            .position_x = 0.5F, .position_y = -0.5F, .position_z = 0.0F,
            .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F,
            .uv_u = 1.0F, .uv_v = 0.0F
        },
        vr::geometry::GeometryMeshVertex{
            .position_x = 0.5F, .position_y = 0.5F, .position_z = 0.0F,
            .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F,
            .uv_u = 1.0F, .uv_v = 1.0F
        },
        vr::geometry::GeometryMeshVertex{
            .position_x = -0.5F, .position_y = 0.5F, .position_z = 0.0F,
            .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F,
            .uv_u = 0.0F, .uv_v = 1.0F
        }
    };
    constexpr std::array<std::uint32_t, 6U> indices{0U, 1U, 2U, 2U, 3U, 0U};

    const vr::geometry::GeometryTangentBuildResult result =
        vr::geometry::PrepareGeometryMeshTangents(vertices.data(),
                                                  static_cast<std::uint32_t>(vertices.size()),
                                                  indices.data(),
                                                  static_cast<std::uint32_t>(indices.size()),
                                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    VR_CHECK(result.status == vr::geometry::GeometryTangentBuildStatus::generated);
    VR_CHECK(!result.used_fallback_basis);
    VR_CHECK(!result.encountered_degenerate_uv);

    for (const auto& vertex : vertices) {
        VR_CHECK(NearlyEqual(vertex.tangent_x, 1.0F));
        VR_CHECK(NearlyEqual(vertex.tangent_y, 0.0F));
        VR_CHECK(NearlyEqual(vertex.tangent_z, 0.0F));
        VR_CHECK(NearlyEqual(vertex.tangent_w, 1.0F));
    }
}

VR_TEST_CASE(GeometryTangentSpace_normalizes_existing_tangent_and_preserves_handedness,
             "unit;geometry;tangent") {
    std::array<vr::geometry::GeometryMeshVertex, 3U> vertices{
        vr::geometry::GeometryMeshVertex{
            .position_x = 0.0F, .position_y = 0.0F, .position_z = 0.0F,
            .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F,
            .uv_u = 0.0F, .uv_v = 0.0F,
            .tangent_x = 5.0F, .tangent_y = 0.0F, .tangent_z = 0.0F, .tangent_w = -2.0F
        },
        vr::geometry::GeometryMeshVertex{
            .position_x = 1.0F, .position_y = 0.0F, .position_z = 0.0F,
            .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F,
            .uv_u = 1.0F, .uv_v = 0.0F,
            .tangent_x = 5.0F, .tangent_y = 0.0F, .tangent_z = 0.0F, .tangent_w = -2.0F
        },
        vr::geometry::GeometryMeshVertex{
            .position_x = 0.0F, .position_y = 1.0F, .position_z = 0.0F,
            .normal_x = 0.0F, .normal_y = 0.0F, .normal_z = 1.0F,
            .uv_u = 0.0F, .uv_v = 1.0F,
            .tangent_x = 5.0F, .tangent_y = 0.0F, .tangent_z = 0.0F, .tangent_w = -2.0F
        }
    };
    constexpr std::array<std::uint32_t, 3U> indices{0U, 1U, 2U};

    const vr::geometry::GeometryTangentBuildResult result =
        vr::geometry::PrepareGeometryMeshTangents(vertices.data(),
                                                  static_cast<std::uint32_t>(vertices.size()),
                                                  indices.data(),
                                                  static_cast<std::uint32_t>(indices.size()),
                                                  VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    VR_CHECK(result.status == vr::geometry::GeometryTangentBuildStatus::normalized_existing);
    VR_CHECK(!result.used_fallback_basis);
    for (const auto& vertex : vertices) {
        VR_CHECK(NearlyEqual(vertex.tangent_x, 1.0F));
        VR_CHECK(NearlyEqual(vertex.tangent_y, 0.0F));
        VR_CHECK(NearlyEqual(vertex.tangent_z, 0.0F));
        VR_CHECK(NearlyEqual(vertex.tangent_w, -1.0F));
    }
}

} // namespace


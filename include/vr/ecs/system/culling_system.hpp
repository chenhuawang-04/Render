#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/bounds_component.hpp"
#include "vr/ecs/component/camera_component.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace vr::ecs {

template<typename T>
using CullingSystemMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct CullingBuildOptions final {
    bool enable_culling_mask_filter = true;
    bool enable_frustum_culling = true;
    bool enable_aabb_refine = true;
    bool write_visibility_bits = false;
};

struct CullingBuildStats final {
    std::uint32_t input_count = 0U;
    std::uint32_t scanned_count = 0U;
    std::uint32_t candidate_count = 0U;
    std::uint32_t out_of_range_candidate_count = 0U;
    std::uint32_t visible_count = 0U;
    std::uint32_t culled_by_mask_count = 0U;
    std::uint32_t culled_by_frustum_count = 0U;
    std::uint32_t culled_by_invalid_bounds_count = 0U;
    std::uint32_t sphere_reject_count = 0U;
    std::uint32_t aabb_reject_count = 0U;
    std::uint32_t plane_test_count = 0U;
    std::uint64_t visible_set_signature = 0U;
    bool used_mask_filter = false;
    bool used_frustum_filter = false;
    bool used_aabb_refine = false;
    bool wrote_visibility_bits = false;
};

template<DimensionTag DimensionT>
struct CullingScratch final {
    CullingSystemMcVector<std::uint32_t> visible_indices{};
    CullingSystemMcVector<std::uint8_t> visibility_bits{};

    void Reserve(std::uint32_t component_count_) {
        const std::size_t reserve_count = static_cast<std::size_t>(component_count_);
        if (visible_indices.capacity() < reserve_count) {
            visible_indices.reserve(reserve_count);
        }
        if (visibility_bits.capacity() < reserve_count) {
            visibility_bits.reserve(reserve_count);
        }
    }
};

template<DimensionTag DimensionT>
class CullingSystem final {
public:
    using BoundsType = Bounds<DimensionT>;
    using CameraType = Camera<DimensionT>;
    using ScratchType = CullingScratch<DimensionT>;

    static constexpr std::uint32_t k_plane_count = std::same_as<DimensionT, Dim2> ? 4U : 6U;

    struct FrustumPlane final {
        float nx;
        float ny;
        float nz;
        float d;
    };

    struct FrustumPlanes final {
        FrustumPlane planes[6U]{};
        std::uint32_t count = 0U;
    };

    struct PreparedCamera final {
        FrustumPlanes frustum_planes{};
        std::uint32_t culling_mask = 0xFFFFFFFFU;
        std::uint8_t has_camera = 0U;
        std::uint8_t use_mask_filter = 0U;
        std::uint8_t use_frustum_filter = 0U;
        std::uint8_t use_aabb_refine = 0U;
    };

    static void Reserve(ScratchType& scratch_, std::uint32_t component_count_) {
        scratch_.Reserve(component_count_);
    }

    [[nodiscard]] static PreparedCamera PrepareCamera(
        const CameraType* camera_component_,
        const CullingBuildOptions& options_ = {}) noexcept {
        PreparedCamera prepared{};
        prepared.use_mask_filter = options_.enable_culling_mask_filter ? 1U : 0U;
        prepared.use_frustum_filter = options_.enable_frustum_culling ? 1U : 0U;
        prepared.use_aabb_refine = (options_.enable_frustum_culling && options_.enable_aabb_refine) ? 1U : 0U;

        if (camera_component_ == nullptr) {
            prepared.has_camera = 0U;
            prepared.use_mask_filter = 0U;
            prepared.use_frustum_filter = 0U;
            prepared.use_aabb_refine = 0U;
            return prepared;
        }

        prepared.has_camera = 1U;
        prepared.culling_mask = camera_component_->runtime.culling_mask;
        if (prepared.use_frustum_filter != 0U) {
            prepared.frustum_planes = BuildFrustumPlanes(camera_component_->runtime.view_projection_matrix);
        }
        return prepared;
    }

    [[nodiscard]] static CullingBuildStats BuildVisibleSet(
        const BoundsType* bounds_components_,
        std::uint32_t component_count_,
        const CameraType* camera_component_,
        ScratchType& scratch_,
        const CullingBuildOptions& options_ = {}) {
        const PreparedCamera prepared_camera = PrepareCamera(camera_component_, options_);
        return BuildVisibleSet(bounds_components_, component_count_, prepared_camera, scratch_, options_);
    }

    [[nodiscard]] static CullingBuildStats BuildVisibleSet(
        const BoundsType* bounds_components_,
        std::uint32_t component_count_,
        const CameraType& camera_component_,
        ScratchType& scratch_,
        const CullingBuildOptions& options_ = {}) {
        return BuildVisibleSet(bounds_components_, component_count_, &camera_component_, scratch_, options_);
    }

    [[nodiscard]] static CullingBuildStats BuildVisibleSet(
        const BoundsType* bounds_components_,
        std::uint32_t component_count_,
        const PreparedCamera& prepared_camera_,
        ScratchType& scratch_,
        const CullingBuildOptions& options_ = {}) {
        return BuildVisibleSetInternal(bounds_components_,
                                       component_count_,
                                       static_cast<const std::uint32_t*>(nullptr),
                                       0U,
                                       prepared_camera_,
                                       scratch_,
                                       options_);
    }

    [[nodiscard]] static CullingBuildStats BuildVisibleSetFromCandidates(
        const BoundsType* bounds_components_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_count_,
        const CameraType* camera_component_,
        ScratchType& scratch_,
        const CullingBuildOptions& options_ = {}) {
        const PreparedCamera prepared_camera = PrepareCamera(camera_component_, options_);
        return BuildVisibleSetFromCandidates(bounds_components_,
                                             component_count_,
                                             candidate_component_indices_,
                                             candidate_count_,
                                             prepared_camera,
                                             scratch_,
                                             options_);
    }

    [[nodiscard]] static CullingBuildStats BuildVisibleSetFromCandidates(
        const BoundsType* bounds_components_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_count_,
        const CameraType& camera_component_,
        ScratchType& scratch_,
        const CullingBuildOptions& options_ = {}) {
        return BuildVisibleSetFromCandidates(bounds_components_,
                                             component_count_,
                                             candidate_component_indices_,
                                             candidate_count_,
                                             &camera_component_,
                                             scratch_,
                                             options_);
    }

    [[nodiscard]] static CullingBuildStats BuildVisibleSetFromCandidates(
        const BoundsType* bounds_components_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_count_,
        const PreparedCamera& prepared_camera_,
        ScratchType& scratch_,
        const CullingBuildOptions& options_ = {}) {
        return BuildVisibleSetInternal(bounds_components_,
                                       component_count_,
                                       candidate_component_indices_,
                                       candidate_count_,
                                       prepared_camera_,
                                       scratch_,
                                       options_);
    }

    [[nodiscard]] static const CullingSystemMcVector<std::uint32_t>& VisibleIndices(
        const ScratchType& scratch_) noexcept {
        return scratch_.visible_indices;
    }

    [[nodiscard]] static bool IsVisible(const ScratchType& scratch_,
                                        std::uint32_t component_index_) noexcept {
        if (component_index_ >= scratch_.visibility_bits.size()) {
            return false;
        }
        return scratch_.visibility_bits[component_index_] != 0U;
    }

private:
    [[nodiscard]] static CullingBuildStats BuildVisibleSetInternal(
        const BoundsType* bounds_components_,
        std::uint32_t component_count_,
        const std::uint32_t* candidate_component_indices_,
        std::uint32_t candidate_count_,
        const PreparedCamera& prepared_camera_,
        ScratchType& scratch_,
        const CullingBuildOptions& options_) {
        CullingBuildStats stats{};
        stats.input_count = component_count_;
        stats.used_mask_filter = prepared_camera_.use_mask_filter != 0U;
        stats.used_frustum_filter = prepared_camera_.use_frustum_filter != 0U;
        stats.used_aabb_refine = prepared_camera_.use_aabb_refine != 0U;
        stats.wrote_visibility_bits = options_.write_visibility_bits;

        scratch_.visible_indices.clear();
        if (component_count_ == 0U || bounds_components_ == nullptr) {
            scratch_.visibility_bits.clear();
            return stats;
        }

        Reserve(scratch_, component_count_);
        if (options_.write_visibility_bits) {
            scratch_.visibility_bits.resize(component_count_);
            std::fill(scratch_.visibility_bits.begin(), scratch_.visibility_bits.end(), std::uint8_t{0U});
        } else {
            scratch_.visibility_bits.clear();
        }

        const bool use_candidate_scan =
            (candidate_component_indices_ != nullptr) && (candidate_count_ > 0U);
        stats.candidate_count = use_candidate_scan ? candidate_count_ : component_count_;
        stats.scanned_count = stats.candidate_count;

        for (std::uint32_t i = 0U; i < stats.scanned_count; ++i) {
            const std::uint32_t component_index = use_candidate_scan
                ? candidate_component_indices_[i]
                : i;
            if (component_index >= component_count_) {
                ++stats.out_of_range_candidate_count;
                continue;
            }

            const BoundsType& bounds = bounds_components_[component_index];

            if (!HasValidWorldAabb(bounds)) {
                ++stats.culled_by_invalid_bounds_count;
                continue;
            }

            if (stats.used_mask_filter) {
                if ((bounds.runtime.visibility_mask & prepared_camera_.culling_mask) == 0U) {
                    ++stats.culled_by_mask_count;
                    continue;
                }
            }

            if (stats.used_frustum_filter) {
                if (!IntersectsFrustum(bounds,
                                       prepared_camera_.frustum_planes,
                                       stats.used_aabb_refine,
                                       stats)) {
                    ++stats.culled_by_frustum_count;
                    continue;
                }
            }

            scratch_.visible_indices.push_back(component_index);
            if (options_.write_visibility_bits) {
                scratch_.visibility_bits[component_index] = 1U;
            }
        }

        stats.visible_count = static_cast<std::uint32_t>(scratch_.visible_indices.size());
        stats.visible_set_signature = ComputeVisibleSetSignature(scratch_.visible_indices);
        return stats;
    }

    [[nodiscard]] static bool HasValidWorldAabb(const BoundsType& bounds_) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            return bounds_.runtime.world_min.x <= bounds_.runtime.world_max.x &&
                   bounds_.runtime.world_min.y <= bounds_.runtime.world_max.y;
        } else {
            return bounds_.runtime.world_min.x <= bounds_.runtime.world_max.x &&
                   bounds_.runtime.world_min.y <= bounds_.runtime.world_max.y &&
                   bounds_.runtime.world_min.z <= bounds_.runtime.world_max.z;
        }
    }

    static void HashCombine(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_ + 0x9e3779b97f4a7c15ULL + (hash_ << 6U) + (hash_ >> 2U);
    }

    [[nodiscard]] static std::uint64_t ComputeVisibleSetSignature(
        const CullingSystemMcVector<std::uint32_t>& visible_indices_) noexcept {
        std::uint64_t hash = 0x9a3f2bc8d4e5f607ULL;
        HashCombine(hash, static_cast<std::uint64_t>(visible_indices_.size()));
        for (const std::uint32_t component_index : visible_indices_) {
            HashCombine(hash, static_cast<std::uint64_t>(component_index));
        }
        return hash;
    }

    [[nodiscard]] static FrustumPlane NormalizePlane(const FrustumPlane& plane_) noexcept {
        const float length_sq = plane_.nx * plane_.nx +
                                plane_.ny * plane_.ny +
                                plane_.nz * plane_.nz;
        if (length_sq <= 1e-20F) {
            return FrustumPlane{.nx = 0.0F, .ny = 0.0F, .nz = 0.0F, .d = -1.0F};
        }
        const float inv_length = 1.0F / std::sqrt(length_sq);
        return FrustumPlane{
            .nx = plane_.nx * inv_length,
            .ny = plane_.ny * inv_length,
            .nz = plane_.nz * inv_length,
            .d = plane_.d * inv_length
        };
    }

    [[nodiscard]] static FrustumPlanes BuildFrustumPlanes(const Matrix4x4& view_projection_) noexcept {
        const float row0x = view_projection_.m[0];
        const float row0y = view_projection_.m[4];
        const float row0z = view_projection_.m[8];
        const float row0w = view_projection_.m[12];

        const float row1x = view_projection_.m[1];
        const float row1y = view_projection_.m[5];
        const float row1z = view_projection_.m[9];
        const float row1w = view_projection_.m[13];

        const float row2x = view_projection_.m[2];
        const float row2y = view_projection_.m[6];
        const float row2z = view_projection_.m[10];
        const float row2w = view_projection_.m[14];

        const float row3x = view_projection_.m[3];
        const float row3y = view_projection_.m[7];
        const float row3z = view_projection_.m[11];
        const float row3w = view_projection_.m[15];

        FrustumPlanes out{};
        out.count = k_plane_count;
        out.planes[0] = NormalizePlane(FrustumPlane{
            .nx = row3x + row0x,
            .ny = row3y + row0y,
            .nz = row3z + row0z,
            .d = row3w + row0w
        });
        out.planes[1] = NormalizePlane(FrustumPlane{
            .nx = row3x - row0x,
            .ny = row3y - row0y,
            .nz = row3z - row0z,
            .d = row3w - row0w
        });
        out.planes[2] = NormalizePlane(FrustumPlane{
            .nx = row3x + row1x,
            .ny = row3y + row1y,
            .nz = row3z + row1z,
            .d = row3w + row1w
        });
        out.planes[3] = NormalizePlane(FrustumPlane{
            .nx = row3x - row1x,
            .ny = row3y - row1y,
            .nz = row3z - row1z,
            .d = row3w - row1w
        });

        if constexpr (std::same_as<DimensionT, Dim3>) {
            // D3D depth range: [0, 1]
            out.planes[4] = NormalizePlane(FrustumPlane{
                .nx = row2x,
                .ny = row2y,
                .nz = row2z,
                .d = row2w
            });
            out.planes[5] = NormalizePlane(FrustumPlane{
                .nx = row3x - row2x,
                .ny = row3y - row2y,
                .nz = row3z - row2z,
                .d = row3w - row2w
            });
        }
        return out;
    }

    [[nodiscard]] static bool IntersectsFrustum(const BoundsType& bounds_,
                                                const FrustumPlanes& frustum_planes_,
                                                bool use_aabb_refine_,
                                                CullingBuildStats& stats_) noexcept {
        const float center_x = bounds_.runtime.world_center.x;
        const float center_y = bounds_.runtime.world_center.y;
        float center_z = 0.0F;
        if constexpr (std::same_as<DimensionT, Dim3>) {
            center_z = bounds_.runtime.world_center.z;
        }
        const float sphere_radius = std::max(0.0F, bounds_.runtime.world_radius);

        for (std::uint32_t plane_index = 0U; plane_index < frustum_planes_.count; ++plane_index) {
            const FrustumPlane& plane = frustum_planes_.planes[plane_index];
            ++stats_.plane_test_count;
            const float center_distance = plane.nx * center_x +
                                          plane.ny * center_y +
                                          plane.nz * center_z +
                                          plane.d;
            if (center_distance < -sphere_radius) {
                ++stats_.sphere_reject_count;
                return false;
            }
        }

        if (!use_aabb_refine_) {
            return true;
        }

        const float min_x = bounds_.runtime.world_min.x;
        const float min_y = bounds_.runtime.world_min.y;
        const float max_x = bounds_.runtime.world_max.x;
        const float max_y = bounds_.runtime.world_max.y;

        float min_z = 0.0F;
        float max_z = 0.0F;
        if constexpr (std::same_as<DimensionT, Dim3>) {
            min_z = bounds_.runtime.world_min.z;
            max_z = bounds_.runtime.world_max.z;
        }

        for (std::uint32_t plane_index = 0U; plane_index < frustum_planes_.count; ++plane_index) {
            const FrustumPlane& plane = frustum_planes_.planes[plane_index];
            ++stats_.plane_test_count;

            const float px = (plane.nx >= 0.0F) ? max_x : min_x;
            const float py = (plane.ny >= 0.0F) ? max_y : min_y;
            const float pz = (plane.nz >= 0.0F) ? max_z : min_z;
            const float positive_distance = plane.nx * px +
                                            plane.ny * py +
                                            plane.nz * pz +
                                            plane.d;
            if (positive_distance < 0.0F) {
                ++stats_.aabb_reject_count;
                return false;
            }
        }
        return true;
    }
};

} // namespace vr::ecs

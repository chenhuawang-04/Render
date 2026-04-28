#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/ecs/component/bounds_component.hpp"
#include "vr/ecs/component/camera_component.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>

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
    CullingSystemMcVector<std::uint32_t> visibility_stamps{};
    std::uint32_t visibility_epoch = 1U;

    void Reserve(std::uint32_t component_count_) {
        const std::size_t reserve_count = static_cast<std::size_t>(component_count_);
        if (visible_indices.capacity() < reserve_count) {
            visible_indices.reserve(reserve_count);
        }
        if (visibility_stamps.capacity() < reserve_count) {
            visibility_stamps.reserve(reserve_count);
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
        if (component_index_ >= scratch_.visibility_stamps.size()) {
            return false;
        }
        return scratch_.visibility_stamps[component_index_] == scratch_.visibility_epoch;
    }

private:
    static constexpr std::uint64_t k_visible_signature_seed = 14695981039346656037ULL;
    static constexpr std::uint64_t k_visible_signature_prime = 1099511628211ULL;

    static void SignatureMix(std::uint64_t& hash_, std::uint64_t value_) noexcept {
        hash_ ^= value_;
        hash_ *= k_visible_signature_prime;
    }

    [[nodiscard]] static std::uint64_t FinalizeVisibleSignature(std::uint64_t running_hash_,
                                                                std::uint32_t visible_count_) noexcept {
        SignatureMix(running_hash_, static_cast<std::uint64_t>(visible_count_));
        if (running_hash_ == 0U) {
            running_hash_ = k_visible_signature_seed;
        }
        return running_hash_;
    }

    static void EnsureVisibilityStampSize(ScratchType& scratch_,
                                          std::uint32_t component_count_) {
        const std::size_t required_size = static_cast<std::size_t>(component_count_);
        const std::size_t old_size = scratch_.visibility_stamps.size();
        if (old_size >= required_size) {
            return;
        }

        scratch_.visibility_stamps.resize(required_size);
        std::fill(scratch_.visibility_stamps.begin() + static_cast<std::ptrdiff_t>(old_size),
                  scratch_.visibility_stamps.end(),
                  0U);
    }

    static void AdvanceVisibilityEpoch(ScratchType& scratch_) {
        if (scratch_.visibility_epoch == std::numeric_limits<std::uint32_t>::max()) {
            std::fill(scratch_.visibility_stamps.begin(),
                      scratch_.visibility_stamps.end(),
                      0U);
            scratch_.visibility_epoch = 1U;
            return;
        }
        ++scratch_.visibility_epoch;
    }

    template<bool write_visibility_bits_>
    static void AppendVisibleIndex(ScratchType& scratch_,
                                   std::uint32_t component_index_,
                                   std::uint64_t& visible_signature_,
                                   std::uint32_t& visible_count_) {
        scratch_.visible_indices.push_back(component_index_);
        if constexpr (write_visibility_bits_) {
            scratch_.visibility_stamps[component_index_] = scratch_.visibility_epoch;
        }
        SignatureMix(visible_signature_, static_cast<std::uint64_t>(component_index_));
        ++visible_count_;
    }

    template<bool use_aabb_refine_>
    [[nodiscard]] static bool IntersectsFrustum(const BoundsType& bounds_,
                                                const FrustumPlanes& frustum_planes_,
                                                CullingBuildStats& stats_) noexcept {
        const float center_x = bounds_.runtime.world_center.x;
        const float center_y = bounds_.runtime.world_center.y;
        float center_z = 0.0F;
        if constexpr (std::same_as<DimensionT, Dim3>) {
            center_z = bounds_.runtime.world_center.z;
        }
        const float sphere_radius = std::max(0.0F, bounds_.runtime.world_radius);
        bool sphere_fully_inside_all_planes = true;

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
            if (center_distance < sphere_radius) {
                sphere_fully_inside_all_planes = false;
            }
        }

        if constexpr (!use_aabb_refine_) {
            return true;
        }
        if (sphere_fully_inside_all_planes) {
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

    template<bool use_candidate_scan_,
             bool use_mask_filter_,
             bool use_frustum_filter_,
             bool use_aabb_refine_,
             bool write_visibility_bits_>
    static void ScanVisible(const BoundsType* bounds_components_,
                            std::uint32_t component_count_,
                            const std::uint32_t* candidate_component_indices_,
                            const PreparedCamera& prepared_camera_,
                            CullingBuildStats& stats_,
                            ScratchType& scratch_,
                            std::uint64_t& visible_signature_,
                            std::uint32_t& visible_count_) {
        const std::uint32_t scanned_count = stats_.scanned_count;
        for (std::uint32_t i = 0U; i < scanned_count; ++i) {
            const std::uint32_t component_index = use_candidate_scan_
                ? candidate_component_indices_[i]
                : i;
            if (component_index >= component_count_) {
                ++stats_.out_of_range_candidate_count;
                continue;
            }

            const BoundsType& bounds = bounds_components_[component_index];
            if (!HasValidWorldAabb(bounds)) {
                ++stats_.culled_by_invalid_bounds_count;
                continue;
            }

            if constexpr (use_mask_filter_) {
                if ((bounds.runtime.visibility_mask & prepared_camera_.culling_mask) == 0U) {
                    ++stats_.culled_by_mask_count;
                    continue;
                }
            }

            if constexpr (use_frustum_filter_) {
                if (!IntersectsFrustum<use_aabb_refine_>(bounds, prepared_camera_.frustum_planes, stats_)) {
                    ++stats_.culled_by_frustum_count;
                    continue;
                }
            }

            AppendVisibleIndex<write_visibility_bits_>(
                scratch_,
                component_index,
                visible_signature_,
                visible_count_);
        }
    }

    template<bool use_candidate_scan_>
    static void DispatchScan(const BoundsType* bounds_components_,
                             std::uint32_t component_count_,
                             const std::uint32_t* candidate_component_indices_,
                             const PreparedCamera& prepared_camera_,
                             const CullingBuildOptions& options_,
                             CullingBuildStats& stats_,
                             ScratchType& scratch_,
                             std::uint64_t& visible_signature_,
                             std::uint32_t& visible_count_) {
        const bool use_mask_filter = prepared_camera_.use_mask_filter != 0U;
        const bool use_frustum_filter = prepared_camera_.use_frustum_filter != 0U;
        const bool use_aabb_refine = prepared_camera_.use_aabb_refine != 0U;
        const bool write_visibility_bits = options_.write_visibility_bits;

        if (use_frustum_filter) {
            if (use_mask_filter) {
                if (write_visibility_bits) {
                    if (use_aabb_refine) {
                        ScanVisible<use_candidate_scan_, true, true, true, true>(
                            bounds_components_,
                            component_count_,
                            candidate_component_indices_,
                            prepared_camera_,
                            stats_,
                            scratch_,
                            visible_signature_,
                            visible_count_);
                    } else {
                        ScanVisible<use_candidate_scan_, true, true, false, true>(
                            bounds_components_,
                            component_count_,
                            candidate_component_indices_,
                            prepared_camera_,
                            stats_,
                            scratch_,
                            visible_signature_,
                            visible_count_);
                    }
                } else {
                    if (use_aabb_refine) {
                        ScanVisible<use_candidate_scan_, true, true, true, false>(
                            bounds_components_,
                            component_count_,
                            candidate_component_indices_,
                            prepared_camera_,
                            stats_,
                            scratch_,
                            visible_signature_,
                            visible_count_);
                    } else {
                        ScanVisible<use_candidate_scan_, true, true, false, false>(
                            bounds_components_,
                            component_count_,
                            candidate_component_indices_,
                            prepared_camera_,
                            stats_,
                            scratch_,
                            visible_signature_,
                            visible_count_);
                    }
                }
            } else {
                if (write_visibility_bits) {
                    if (use_aabb_refine) {
                        ScanVisible<use_candidate_scan_, false, true, true, true>(
                            bounds_components_,
                            component_count_,
                            candidate_component_indices_,
                            prepared_camera_,
                            stats_,
                            scratch_,
                            visible_signature_,
                            visible_count_);
                    } else {
                        ScanVisible<use_candidate_scan_, false, true, false, true>(
                            bounds_components_,
                            component_count_,
                            candidate_component_indices_,
                            prepared_camera_,
                            stats_,
                            scratch_,
                            visible_signature_,
                            visible_count_);
                    }
                } else {
                    if (use_aabb_refine) {
                        ScanVisible<use_candidate_scan_, false, true, true, false>(
                            bounds_components_,
                            component_count_,
                            candidate_component_indices_,
                            prepared_camera_,
                            stats_,
                            scratch_,
                            visible_signature_,
                            visible_count_);
                    } else {
                        ScanVisible<use_candidate_scan_, false, true, false, false>(
                            bounds_components_,
                            component_count_,
                            candidate_component_indices_,
                            prepared_camera_,
                            stats_,
                            scratch_,
                            visible_signature_,
                            visible_count_);
                    }
                }
            }
            return;
        }

        if (use_mask_filter) {
            if (write_visibility_bits) {
                ScanVisible<use_candidate_scan_, true, false, false, true>(
                    bounds_components_,
                    component_count_,
                    candidate_component_indices_,
                    prepared_camera_,
                    stats_,
                    scratch_,
                    visible_signature_,
                    visible_count_);
            } else {
                ScanVisible<use_candidate_scan_, true, false, false, false>(
                    bounds_components_,
                    component_count_,
                    candidate_component_indices_,
                    prepared_camera_,
                    stats_,
                    scratch_,
                    visible_signature_,
                    visible_count_);
            }
        } else {
            if (write_visibility_bits) {
                ScanVisible<use_candidate_scan_, false, false, false, true>(
                    bounds_components_,
                    component_count_,
                    candidate_component_indices_,
                    prepared_camera_,
                    stats_,
                    scratch_,
                    visible_signature_,
                    visible_count_);
            } else {
                ScanVisible<use_candidate_scan_, false, false, false, false>(
                    bounds_components_,
                    component_count_,
                    candidate_component_indices_,
                    prepared_camera_,
                    stats_,
                    scratch_,
                    visible_signature_,
                    visible_count_);
            }
        }
    }

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
        AdvanceVisibilityEpoch(scratch_);
        if (component_count_ == 0U || bounds_components_ == nullptr) {
            stats.visible_set_signature = FinalizeVisibleSignature(k_visible_signature_seed, 0U);
            return stats;
        }

        Reserve(scratch_, component_count_);
        if (options_.write_visibility_bits) {
            EnsureVisibilityStampSize(scratch_, component_count_);
        }

        const bool use_candidate_scan = candidate_count_ > 0U;
        stats.candidate_count = use_candidate_scan ? candidate_count_ : component_count_;
        stats.scanned_count = stats.candidate_count;

        if (use_candidate_scan && candidate_component_indices_ == nullptr) {
            stats.out_of_range_candidate_count = candidate_count_;
            stats.visible_count = 0U;
            stats.visible_set_signature = FinalizeVisibleSignature(k_visible_signature_seed, 0U);
            return stats;
        }

        std::uint64_t visible_signature = k_visible_signature_seed;
        std::uint32_t visible_count = 0U;
        if (use_candidate_scan) {
            DispatchScan<true>(
                bounds_components_,
                component_count_,
                candidate_component_indices_,
                prepared_camera_,
                options_,
                stats,
                scratch_,
                visible_signature,
                visible_count);
        } else {
            DispatchScan<false>(
                bounds_components_,
                component_count_,
                candidate_component_indices_,
                prepared_camera_,
                options_,
                stats,
                scratch_,
                visible_signature,
                visible_count);
        }

        stats.visible_count = visible_count;
        stats.visible_set_signature = FinalizeVisibleSignature(visible_signature, visible_count);
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

};

} // namespace vr::ecs

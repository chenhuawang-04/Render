#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/render/appearance_sampled_surface.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/runtime/runtime_ingress_ids.hpp"

#include <cstdint>

namespace vr::geometry {

template<typename T>
using GeometryAppearanceMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct GeometryAppearanceHostCreateInfo {
    std::uint32_t reserve_appearance_count = 256U;
};

enum GeometryAppearanceFlags : std::uint32_t {
    geometry_appearance_flag_alpha_test = 1U << 0U,
};

struct GeometryAppearanceDesc {
    GeometryAppearanceId appearance_id{};
    render::AppearanceSampledSurfaceBinding3D sampled_surface_binding =
        render::MakeAppearanceSampledSurfaceBinding3D(
            render::AppearanceSampledSurfaceDomain::geometry_image);
    std::uint32_t flags = 0U;
    float uv_scale_u = 1.0F;
    float uv_scale_v = 1.0F;
    float uv_bias_u = 0.0F;
    float uv_bias_v = 0.0F;
    float alpha_cutoff = 0.0F;
    float metallic_factor = 0.0F;
    float roughness_factor = 1.0F;
    float normal_scale = 1.0F;
    float occlusion_strength = 1.0F;
};

struct GeometryAppearanceHostStats {
    std::uint32_t appearance_count = 0U;
    std::uint32_t added_appearance_count = 0U;
    std::uint32_t updated_appearance_count = 0U;
    std::uint32_t removed_appearance_count = 0U;
    std::uint32_t revision = 0U;
};

class GeometryAppearanceHost final {
public:
    struct AppearanceRecord final {
        GeometryAppearanceDesc desc{};
        std::uint32_t revision = 0U;
    };

    GeometryAppearanceHost() = default;
    ~GeometryAppearanceHost() = default;

    GeometryAppearanceHost(const GeometryAppearanceHost&) = delete;
    GeometryAppearanceHost& operator=(const GeometryAppearanceHost&) = delete;

    GeometryAppearanceHost(GeometryAppearanceHost&&) = delete;
    GeometryAppearanceHost& operator=(GeometryAppearanceHost&&) = delete;

    void Initialize(const GeometryAppearanceHostCreateInfo& create_info_ = {});
    void Shutdown() noexcept;

    void UpsertAppearance(const GeometryAppearanceDesc& desc_);
    [[nodiscard]] bool RemoveAppearance(GeometryAppearanceId appearance_id_) noexcept;
    [[nodiscard]] const AppearanceRecord* FindAppearance(GeometryAppearanceId appearance_id_) const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GeometryAppearanceHostStats& Stats() const noexcept;

private:
    [[nodiscard]] std::size_t LowerBoundAppearanceIndex(GeometryAppearanceId appearance_id_) const noexcept;

private:
    GeometryAppearanceHostCreateInfo create_info_cache{};
    GeometryAppearanceMcVector<AppearanceRecord> appearances{};
    GeometryAppearanceHostStats stats{};
    bool initialized = false;
};

} // namespace vr::geometry


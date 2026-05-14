#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/resource/sampler_host.hpp"

#include <cstdint>

namespace vr::geometry {

template<typename T>
using GeometryMaterialMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct GeometryMaterialHostCreateInfo {
    std::uint32_t reserve_material_count = 256U;
};

enum GeometryMaterialFlags : std::uint32_t {
    geometry_material_flag_alpha_test = 1U << 0U,
};

struct GeometryMaterialDesc {
    std::uint32_t material_id = 0U;
    std::uint32_t image_id = 0U;
    resource::SamplerId sampler_id{};
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

struct GeometryMaterialHostStats {
    std::uint32_t material_count = 0U;
    std::uint32_t added_material_count = 0U;
    std::uint32_t updated_material_count = 0U;
    std::uint32_t removed_material_count = 0U;
    std::uint32_t revision = 0U;
};

class GeometryMaterialHost final {
public:
    struct MaterialRecord final {
        GeometryMaterialDesc desc{};
        std::uint32_t revision = 0U;
    };

    GeometryMaterialHost() = default;
    ~GeometryMaterialHost() = default;

    GeometryMaterialHost(const GeometryMaterialHost&) = delete;
    GeometryMaterialHost& operator=(const GeometryMaterialHost&) = delete;

    GeometryMaterialHost(GeometryMaterialHost&&) = delete;
    GeometryMaterialHost& operator=(GeometryMaterialHost&&) = delete;

    void Initialize(const GeometryMaterialHostCreateInfo& create_info_ = {});
    void Shutdown() noexcept;

    void UpsertMaterial(const GeometryMaterialDesc& desc_);
    [[nodiscard]] bool RemoveMaterial(std::uint32_t material_id_) noexcept;
    [[nodiscard]] const MaterialRecord* FindMaterial(std::uint32_t material_id_) const noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const GeometryMaterialHostStats& Stats() const noexcept;

private:
    [[nodiscard]] std::size_t LowerBoundMaterialIndex(std::uint32_t material_id_) const noexcept;

private:
    GeometryMaterialHostCreateInfo create_info_cache{};
    GeometryMaterialMcVector<MaterialRecord> materials{};
    GeometryMaterialHostStats stats{};
    bool initialized = false;
};

} // namespace vr::geometry

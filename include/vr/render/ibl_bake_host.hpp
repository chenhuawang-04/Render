#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/asset/texture_host.hpp"
#include "vr/ecs/component/spatial_types.hpp"
#include "vr/render/ibl_host.hpp"
#include "vr/render/scene_prepare_views.hpp"

#include <array>
#include <cstdint>

namespace vr {
class VulkanContext;
}

namespace vr::render {

template<typename T>
using IblBakeMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

enum class IblBakeSourceKind : std::uint8_t {
    equirectangular = 0U,
    cubemap = 1U
};

struct IblCpuImageView final {
    const ecs::Float4* pixels = nullptr;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t row_pitch_pixels = 0U;
};

struct IblCpuCubemapView final {
    std::array<IblCpuImageView, 6U> faces{};
};

struct IblBakeSourceDesc final {
    IblBakeSourceKind kind = IblBakeSourceKind::equirectangular;
    IblCpuImageView equirect{};
    IblCpuCubemapView cubemap{};
};

struct IblBakeHostCreateInfo final {
    std::uint32_t reserve_owned_texture_count = 16U;
    std::uint32_t default_specular_sample_count = 256U;
    std::uint32_t default_sh_sample_count = 4096U;
    std::uint32_t default_brdf_sample_count = 256U;
    std::uint32_t default_brdf_lut_size = 256U;
    bool auto_register_brdf_lut_with_ibl_host = true;
    bool auto_register_environment_with_ibl_host = true;
};

struct IblBakeRequest final {
    IblBakeSourceDesc source{};
    IblEnvironmentId environment_id{};
    asset::TextureId skybox_texture_id{};
    asset::TextureId specular_texture_id{};
    asset::TextureId brdf_lut_texture_id{};
    std::uint32_t skybox_cube_size = 256U;
    std::uint32_t specular_cube_size = 256U;
    std::uint32_t brdf_lut_size = 0U;
    std::uint32_t specular_sample_count = 0U;
    std::uint32_t sh_sample_count = 0U;
    std::uint32_t brdf_sample_count = 0U;
    float intensity = 1.0F;
    float rotation_y_radians = 0.0F;
    std::array<float, 3U> tint_color{1.0F, 1.0F, 1.0F};
    bool bake_brdf_lut = true;
    bool bake_skybox = true;
    bool bake_specular = true;
    bool bake_sh9 = true;
};

struct IblBakeResult final {
    asset::TextureId skybox_cube{};
    asset::TextureId specular_cube{};
    asset::TextureId brdf_lut{};
    IblEnvironmentId environment_id{};
    IblEnvironmentAssetDesc environment{};
    std::uint32_t specular_mip_levels = 0U;
    std::uint32_t brdf_lut_size = 0U;
    bool registered_with_ibl_host = false;
};

struct IblBakeHostStats final {
    std::uint32_t baked_environment_count = 0U;
    std::uint32_t baked_brdf_lut_count = 0U;
    std::uint32_t generated_texture_count = 0U;
    std::uint32_t owned_texture_count = 0U;
    std::uint32_t revision = 0U;
};

class IblBakeHost final {
public:
    IblBakeHost() = default;
    ~IblBakeHost() = default;

    IblBakeHost(const IblBakeHost&) = delete;
    IblBakeHost& operator=(const IblBakeHost&) = delete;

    IblBakeHost(IblBakeHost&&) = delete;
    IblBakeHost& operator=(IblBakeHost&&) = delete;

    void Initialize(asset::TextureHost& texture_host_,
                    IblHost* ibl_host_ = nullptr,
                    const IblBakeHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    [[nodiscard]] asset::TextureId EnsureBrdfLut(const IblBakeHostPrepareView& prepare_view_,
                                                 asset::TextureId texture_id_ = {},
                                                 std::uint32_t lut_size_ = 0U,
                                                 std::uint32_t sample_count_ = 0U);

    [[nodiscard]] IblBakeResult BakeEnvironment(const IblBakeHostPrepareView& prepare_view_,
                                                const IblBakeRequest& request_);

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const IblBakeHostStats& Stats() const noexcept;

private:
    [[nodiscard]] asset::TextureId AllocateTextureId() noexcept;
    void TrackOwnedTexture(asset::TextureId texture_id_);
    void RemoveOwnedTextures(VulkanContext& context_) noexcept;

private:
    asset::TextureHost* texture_host = nullptr;
    IblHost* ibl_host = nullptr;
    IblBakeHostCreateInfo create_info_cache{};
    IblBakeMcVector<asset::TextureId> owned_texture_ids{};
    asset::TextureId cached_brdf_lut_texture_id{};
    IblBakeHostStats stats{};
    std::uint32_t next_generated_texture_id = 0xFFF10000U;
    bool initialized = false;
};

} // namespace vr::render


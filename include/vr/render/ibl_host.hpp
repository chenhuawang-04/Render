#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"
#include "vr/asset/texture_host.hpp"
#include "vr/render/bindless_types.hpp"
#include "vr/render/descriptor_host.hpp"
#include "vr/render/scene_prepare_views.hpp"
#include "vr/resource/buffer_host.hpp"
#include "vr/resource/sampler_host.hpp"
#include "vr/vulkan_context.hpp"

#include <array>
#include <cstdint>

namespace vr::render {

template<typename T>
using IblMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

struct IblEnvironmentId final {
    std::uint32_t value = 0U;

    [[nodiscard]] bool IsValid() const noexcept {
        return value != 0U;
    }
};

struct alignas(16) IblGpuParams final {
    std::array<std::array<float, 4U>, 9U> sh9{};
    std::array<float, 4U> tint_intensity{1.0F, 1.0F, 1.0F, 0.0F};
    std::array<float, 4U> rotation_max_lod_flags{0.0F, 1.0F, 0.0F, 0.0F};
    std::array<std::uint32_t, 4U> texture_sampler_slots{};
};

static_assert(sizeof(IblGpuParams) == sizeof(float) * 48U,
              "IblGpuParams must remain a tightly packed 12xfloat4 payload");
static_assert((sizeof(IblGpuParams) % 16U) == 0U,
              "IblGpuParams must remain 16-byte aligned for uniform-buffer upload");

struct IblEnvironmentAssetDesc final {
    IblEnvironmentId environment_id{};
    asset::TextureId specular_cube{};
    asset::TextureId skybox_cube{};
    float intensity = 1.0F;
    float rotation_y_radians = 0.0F;
    float max_specular_lod = -1.0F;
    std::array<float, 3U> tint_color{1.0F, 1.0F, 1.0F};
    std::array<std::array<float, 4U>, 9U> sh9{};
    bool replace_existing = true;
};

struct IblHostCreateInfo final {
    bool create_default_brdf_lut = true;
    bool create_default_environment_textures = true;
    std::uint32_t frames_in_flight = 2U;
    std::uint32_t reserve_environment_count = 16U;
};

struct IblHostStats final {
    std::uint32_t environment_count = 0U;
    std::uint32_t prepared_frame_count = 0U;
    std::uint32_t descriptor_update_count = 0U;
    std::uint32_t removed_environment_count = 0U;
    std::uint32_t default_texture_build_count = 0U;
    std::uint32_t revision = 0U;
};

class IblHost final {
public:
    IblHost() = default;
    ~IblHost() = default;

    IblHost(const IblHost&) = delete;
    IblHost& operator=(const IblHost&) = delete;

    IblHost(IblHost&&) = delete;
    IblHost& operator=(IblHost&&) = delete;

    void Initialize(VulkanContext& context_,
                    asset::TextureHost& texture_host_,
                    DescriptorHost& descriptor_host_,
                    resource::SamplerHost& sampler_host_,
                    const IblHostCreateInfo& create_info_ = {});

    void Shutdown(VulkanContext& context_);

    [[nodiscard]] IblEnvironmentId RegisterEnvironment(VulkanContext& context_,
                                                       const IblEnvironmentAssetDesc& desc_);

    [[nodiscard]] bool RemoveEnvironment(IblEnvironmentId environment_id_) noexcept;

    void SetBrdfLut(asset::TextureId brdf_lut_) noexcept;

    void PrepareFrame(const IblHostPrepareView& prepare_view_);
    void PrepareEnvironmentFrame(const IblHostPrepareView& prepare_view_,
                                 IblEnvironmentId environment_id_,
                                 asset::TextureId brdf_lut_texture_id_ = {});

    [[nodiscard]] const IblEnvironmentAssetDesc* FindEnvironment(IblEnvironmentId environment_id_) const noexcept;
    [[nodiscard]] VkDescriptorSet ActiveParamsDescriptorSet(std::uint32_t frame_index_) const;
    [[nodiscard]] DescriptorBufferBindingView ActiveParamsBufferBinding(std::uint32_t frame_index_) const;
    [[nodiscard]] DescriptorSetLayoutId ParamsDescriptorLayoutId() const noexcept;
    [[nodiscard]] const IblGpuParams& ActiveParams() const noexcept;
    [[nodiscard]] asset::TextureId BrdfLut() const noexcept;
    [[nodiscard]] asset::TextureId ActiveSpecularTexture() const noexcept;
    [[nodiscard]] asset::TextureId ActiveSkyboxTexture() const noexcept;
    [[nodiscard]] std::uint32_t ActiveSpecularTextureSlot() const noexcept;
    [[nodiscard]] std::uint32_t ActiveBrdfLutTextureSlot() const noexcept;
    [[nodiscard]] std::uint32_t ActiveSkyboxTextureSlot() const noexcept;
    [[nodiscard]] std::uint32_t ActiveSamplerSlot() const noexcept;
    [[nodiscard]] const IblHostStats& Stats() const noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;

private:
    struct EnvironmentRecord final {
        IblEnvironmentAssetDesc desc{};
    };

    struct FrameResources final {
        resource::BufferResource gpu_params_buffer{};
        VkDescriptorSet params_descriptor_set = VK_NULL_HANDLE;
        IblEnvironmentId prepared_environment_id{};
        asset::TextureId prepared_brdf_lut{};
        asset::TextureId prepared_specular_texture{};
        asset::TextureId prepared_skybox_texture{};
        IblGpuParams prepared_params{};
        std::uint32_t prepared_frame_index = 0xFFFF'FFFFU;
        std::uint64_t prepared_last_submitted_value = ~std::uint64_t{0U};
        bool prepared = false;
    };

    [[nodiscard]] std::size_t LowerBoundEnvironmentIndex(IblEnvironmentId environment_id_) const noexcept;
    void EnsureFrameResources(const IblHostPrepareView& prepare_view_);
    void EnsureDefaultTextures(const IblHostPrepareView& prepare_view_);
    void UploadDefaultSpecularCube(const IblHostPrepareView& prepare_view_);
    void UploadDefaultBrdfLut(const IblHostPrepareView& prepare_view_);
    void DestroyFrameResources(VulkanContext& context_) noexcept;
    [[nodiscard]] const EnvironmentRecord* FindEnvironmentRecord(IblEnvironmentId environment_id_) const noexcept;
    [[nodiscard]] const asset::TextureHost::TextureRecord& RequireCubeTexture(asset::TextureId texture_id_,
                                                                              const char* label_) const;
    [[nodiscard]] const asset::TextureHost::TextureRecord& RequireBrdfTexture(asset::TextureId texture_id_,
                                                                              const char* label_) const;
    [[nodiscard]] IblGpuParams BuildResolvedParams(const EnvironmentRecord* record_,
                                                   const asset::TextureHost::TextureRecord& specular_record_,
                                                   bool has_explicit_skybox_) const noexcept;
    void PrepareResolvedFrame(const IblHostPrepareView& prepare_view_,
                              const EnvironmentRecord* record_,
                              IblEnvironmentId prepared_environment_id_,
                              asset::TextureId brdf_lut_texture_id_);
    void UpdateDescriptorSetsForFrame(VulkanContext& context_,
                                      std::uint32_t frame_index_,
                                      const asset::TextureHost::TextureRecord& specular_record_,
                                      const asset::TextureHost::TextureRecord& brdf_record_,
                                      const asset::TextureHost::TextureRecord& skybox_record_);

private:
    asset::TextureHost* texture_host = nullptr;
    DescriptorHost* descriptor_host = nullptr;
    resource::SamplerHost* sampler_host = nullptr;
    IblHostCreateInfo create_info_cache{};
    IblMcVector<EnvironmentRecord> environments{};
    IblMcVector<FrameResources> frame_resources{};
    IblMcVector<DescriptorBufferWrite> descriptor_buffer_write_scratch{};
    IblMcVector<DescriptorTexelBufferWrite> descriptor_texel_write_scratch{};
    DescriptorSetLayoutId params_descriptor_layout_id{};
    resource::SamplerId sampler_id{};
    asset::TextureId default_specular_cube_texture_id{};
    asset::TextureId default_brdf_lut_texture_id{};
    asset::TextureId active_brdf_lut_texture_id{};
    asset::TextureId active_specular_texture_id{};
    asset::TextureId active_skybox_texture_id{};
    std::uint32_t active_specular_texture_slot = 0U;
    std::uint32_t active_brdf_lut_texture_slot = 0U;
    std::uint32_t active_skybox_texture_slot = 0U;
    std::uint32_t active_sampler_slot = 0U;
    IblGpuParams active_params{};
    IblHostStats stats{};
    std::uint32_t next_environment_id = 1U;
    bool default_specular_cube_uploaded = false;
    bool default_brdf_lut_uploaded = false;
    bool initialized = false;
};

} // namespace vr::render


#include "vr/render/ibl_bake_host.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace vr::render {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
constexpr std::uint64_t kImmediateRetireValue = std::numeric_limits<std::uint64_t>::max();

struct Float2Value final {
    float x = 0.0F;
    float y = 0.0F;
};

struct Float3Value final {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

[[nodiscard]] float Saturate(float value_) noexcept {
    return std::clamp(value_, 0.0F, 1.0F);
}

[[nodiscard]] float Dot(const Float3Value& lhs_,
                        const Float3Value& rhs_) noexcept {
    return lhs_.x * rhs_.x + lhs_.y * rhs_.y + lhs_.z * rhs_.z;
}

[[nodiscard]] Float3Value Cross(const Float3Value& lhs_,
                                const Float3Value& rhs_) noexcept {
    return Float3Value{
        .x = lhs_.y * rhs_.z - lhs_.z * rhs_.y,
        .y = lhs_.z * rhs_.x - lhs_.x * rhs_.z,
        .z = lhs_.x * rhs_.y - lhs_.y * rhs_.x
    };
}

[[nodiscard]] Float3Value Add(const Float3Value& lhs_,
                              const Float3Value& rhs_) noexcept {
    return Float3Value{
        .x = lhs_.x + rhs_.x,
        .y = lhs_.y + rhs_.y,
        .z = lhs_.z + rhs_.z
    };
}

[[nodiscard]] Float3Value Subtract(const Float3Value& lhs_,
                                   const Float3Value& rhs_) noexcept {
    return Float3Value{
        .x = lhs_.x - rhs_.x,
        .y = lhs_.y - rhs_.y,
        .z = lhs_.z - rhs_.z
    };
}

[[nodiscard]] Float3Value Multiply(const Float3Value& value_,
                                   float scalar_) noexcept {
    return Float3Value{
        .x = value_.x * scalar_,
        .y = value_.y * scalar_,
        .z = value_.z * scalar_
    };
}

[[nodiscard]] Float3Value Multiply(const Float3Value& lhs_,
                                   const Float3Value& rhs_) noexcept {
    return Float3Value{
        .x = lhs_.x * rhs_.x,
        .y = lhs_.y * rhs_.y,
        .z = lhs_.z * rhs_.z
    };
}

[[nodiscard]] Float3Value Divide(const Float3Value& value_,
                                 float scalar_) noexcept {
    const float safe_scalar = std::abs(scalar_) > 1e-8F ? scalar_ : 1.0F;
    return Multiply(value_, 1.0F / safe_scalar);
}

[[nodiscard]] float Length(const Float3Value& value_) noexcept {
    return std::sqrt(Dot(value_, value_));
}

[[nodiscard]] Float3Value Normalize(const Float3Value& value_) noexcept {
    const float length = Length(value_);
    if (length <= 1e-8F) {
        return Float3Value{.x = 0.0F, .y = 1.0F, .z = 0.0F};
    }
    return Divide(value_, length);
}

[[nodiscard]] Float3Value Lerp(const Float3Value& lhs_,
                               const Float3Value& rhs_,
                               float t_) noexcept {
    return Float3Value{
        .x = lhs_.x + (rhs_.x - lhs_.x) * t_,
        .y = lhs_.y + (rhs_.y - lhs_.y) * t_,
        .z = lhs_.z + (rhs_.z - lhs_.z) * t_
    };
}

[[nodiscard]] Float3Value Reflect(const Float3Value& incident_,
                                  const Float3Value& normal_) noexcept {
    return Subtract(incident_, Multiply(normal_, 2.0F * Dot(normal_, incident_)));
}

[[nodiscard]] std::uint32_t ResolveRowPitchPixels(const IblCpuImageView& view_) noexcept {
    return view_.row_pitch_pixels > 0U ? view_.row_pitch_pixels : view_.width;
}

[[nodiscard]] std::uint32_t ComputeMipLevelCount(std::uint32_t base_size_) noexcept {
    std::uint32_t mip_levels = 1U;
    while (base_size_ > 1U) {
        base_size_ = std::max<std::uint32_t>(1U, base_size_ >> 1U);
        ++mip_levels;
    }
    return mip_levels;
}

[[nodiscard]] std::uint16_t FloatToHalfBits(float value_) noexcept {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value_);
    const std::uint32_t sign = (bits >> 16U) & 0x8000U;
    std::int32_t exponent = static_cast<std::int32_t>((bits >> 23U) & 0xFFU) - 127 + 15;
    std::uint32_t mantissa = bits & 0x7FFFFFU;

    if (((bits >> 23U) & 0xFFU) == 0xFFU) {
        if (mantissa != 0U) {
            return static_cast<std::uint16_t>(sign | 0x7E00U);
        }
        return static_cast<std::uint16_t>(sign | 0x7C00U);
    }

    if (exponent <= 0) {
        if (exponent < -10) {
            return static_cast<std::uint16_t>(sign);
        }
        mantissa |= 0x800000U;
        const std::uint32_t shifted = mantissa >> static_cast<std::uint32_t>(1 - exponent);
        return static_cast<std::uint16_t>(sign | ((shifted + 0x00001000U) >> 13U));
    }

    if (exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7C00U);
    }

    return static_cast<std::uint16_t>(sign |
                                      (static_cast<std::uint32_t>(exponent) << 10U) |
                                      ((mantissa + 0x00001000U) >> 13U));
}

void WriteU16(IblBakeMcVector<std::uint8_t>& bytes_,
              std::size_t byte_offset_,
              std::uint16_t value_) {
    bytes_[byte_offset_ + 0U] = static_cast<std::uint8_t>(value_ & 0xFFU);
    bytes_[byte_offset_ + 1U] = static_cast<std::uint8_t>((value_ >> 8U) & 0xFFU);
}

void WriteRgba16f(IblBakeMcVector<std::uint8_t>& bytes_,
                  std::size_t texel_index_,
                  const Float3Value& color_,
                  float alpha_) {
    const std::size_t base_offset = texel_index_ * 8U;
    WriteU16(bytes_, base_offset + 0U, FloatToHalfBits(color_.x));
    WriteU16(bytes_, base_offset + 2U, FloatToHalfBits(color_.y));
    WriteU16(bytes_, base_offset + 4U, FloatToHalfBits(color_.z));
    WriteU16(bytes_, base_offset + 6U, FloatToHalfBits(alpha_));
}

void WriteRg16f(IblBakeMcVector<std::uint8_t>& bytes_,
                std::size_t texel_index_,
                float x_,
                float y_) {
    const std::size_t base_offset = texel_index_ * 4U;
    WriteU16(bytes_, base_offset + 0U, FloatToHalfBits(x_));
    WriteU16(bytes_, base_offset + 2U, FloatToHalfBits(y_));
}

void WriteRgba8(IblBakeMcVector<std::uint8_t>& bytes_,
                std::size_t texel_index_,
                const Float3Value& color_,
                float alpha_) {
    const std::size_t base_offset = texel_index_ * 4U;
    bytes_[base_offset + 0U] = static_cast<std::uint8_t>(std::round(Saturate(color_.x) * 255.0F));
    bytes_[base_offset + 1U] = static_cast<std::uint8_t>(std::round(Saturate(color_.y) * 255.0F));
    bytes_[base_offset + 2U] = static_cast<std::uint8_t>(std::round(Saturate(color_.z) * 255.0F));
    bytes_[base_offset + 3U] = static_cast<std::uint8_t>(std::round(Saturate(alpha_) * 255.0F));
}

[[nodiscard]] Float3Value SampleImageNearest(const IblCpuImageView& view_,
                                             std::uint32_t x_,
                                             std::uint32_t y_) {
    const std::uint32_t row_pitch = ResolveRowPitchPixels(view_);
    const ecs::Float4& pixel = view_.pixels[static_cast<std::size_t>(y_) * row_pitch + x_];
    return Float3Value{.x = pixel.x, .y = pixel.y, .z = pixel.z};
}

[[nodiscard]] Float3Value SampleImageBilinear(const IblCpuImageView& view_,
                                              float u_,
                                              float v_,
                                              bool wrap_u_) {
    if (view_.pixels == nullptr || view_.width == 0U || view_.height == 0U) {
        return {};
    }

    const float wrapped_u = wrap_u_ ? (u_ - std::floor(u_)) : std::clamp(u_, 0.0F, 1.0F);
    const float clamped_v = std::clamp(v_, 0.0F, 1.0F);

    const float x = wrapped_u * static_cast<float>(view_.width) - 0.5F;
    const float y = clamped_v * static_cast<float>(view_.height) - 0.5F;

    const std::int32_t x0 = static_cast<std::int32_t>(std::floor(x));
    const std::int32_t y0 = static_cast<std::int32_t>(std::floor(y));
    const std::int32_t x1 = x0 + 1;
    const std::int32_t y1 = y0 + 1;

    const auto wrap_or_clamp_x = [&](std::int32_t value_) noexcept -> std::uint32_t {
        if (wrap_u_) {
            const std::int32_t width = static_cast<std::int32_t>(view_.width);
            std::int32_t wrapped = value_ % width;
            if (wrapped < 0) {
                wrapped += width;
            }
            return static_cast<std::uint32_t>(wrapped);
        }
        return static_cast<std::uint32_t>(std::clamp<std::int32_t>(
            value_,
            0,
            static_cast<std::int32_t>(view_.width) - 1));
    };

    const auto clamp_y = [&](std::int32_t value_) noexcept -> std::uint32_t {
        return static_cast<std::uint32_t>(std::clamp<std::int32_t>(
            value_,
            0,
            static_cast<std::int32_t>(view_.height) - 1));
    };

    const float tx = x - std::floor(x);
    const float ty = y - std::floor(y);

    const Float3Value c00 = SampleImageNearest(view_, wrap_or_clamp_x(x0), clamp_y(y0));
    const Float3Value c10 = SampleImageNearest(view_, wrap_or_clamp_x(x1), clamp_y(y0));
    const Float3Value c01 = SampleImageNearest(view_, wrap_or_clamp_x(x0), clamp_y(y1));
    const Float3Value c11 = SampleImageNearest(view_, wrap_or_clamp_x(x1), clamp_y(y1));

    const Float3Value c0 = Lerp(c00, c10, tx);
    const Float3Value c1 = Lerp(c01, c11, tx);
    return Lerp(c0, c1, ty);
}

[[nodiscard]] Float3Value CubeTexelDirection(std::uint32_t face_index_,
                                             std::uint32_t x_,
                                             std::uint32_t y_,
                                             std::uint32_t face_size_) noexcept {
    const float u = (2.0F * (static_cast<float>(x_) + 0.5F) / static_cast<float>(face_size_)) - 1.0F;
    const float v = (2.0F * (static_cast<float>(y_) + 0.5F) / static_cast<float>(face_size_)) - 1.0F;

    Float3Value direction{};
    switch (face_index_) {
    case 0U: // +X
        direction = Float3Value{.x = 1.0F, .y = -v, .z = -u};
        break;
    case 1U: // -X
        direction = Float3Value{.x = -1.0F, .y = -v, .z = u};
        break;
    case 2U: // +Y
        direction = Float3Value{.x = u, .y = 1.0F, .z = v};
        break;
    case 3U: // -Y
        direction = Float3Value{.x = u, .y = -1.0F, .z = -v};
        break;
    case 4U: // +Z
        direction = Float3Value{.x = u, .y = -v, .z = 1.0F};
        break;
    default: // -Z
        direction = Float3Value{.x = -u, .y = -v, .z = -1.0F};
        break;
    }
    return Normalize(direction);
}

[[nodiscard]] Float3Value SampleCubemapFaces(const IblCpuCubemapView& cubemap_,
                                             const Float3Value& direction_) {
    const float abs_x = std::abs(direction_.x);
    const float abs_y = std::abs(direction_.y);
    const float abs_z = std::abs(direction_.z);

    std::uint32_t face_index = 0U;
    float uc = 0.0F;
    float vc = 0.0F;

    if (abs_x >= abs_y && abs_x >= abs_z) {
        if (direction_.x >= 0.0F) {
            face_index = 0U;
            uc = -direction_.z / abs_x;
            vc = -direction_.y / abs_x;
        } else {
            face_index = 1U;
            uc = direction_.z / abs_x;
            vc = -direction_.y / abs_x;
        }
    } else if (abs_y >= abs_x && abs_y >= abs_z) {
        if (direction_.y >= 0.0F) {
            face_index = 2U;
            uc = direction_.x / abs_y;
            vc = direction_.z / abs_y;
        } else {
            face_index = 3U;
            uc = direction_.x / abs_y;
            vc = -direction_.z / abs_y;
        }
    } else {
        if (direction_.z >= 0.0F) {
            face_index = 4U;
            uc = direction_.x / abs_z;
            vc = -direction_.y / abs_z;
        } else {
            face_index = 5U;
            uc = -direction_.x / abs_z;
            vc = -direction_.y / abs_z;
        }
    }

    const float u = uc * 0.5F + 0.5F;
    const float v = vc * 0.5F + 0.5F;
    return SampleImageBilinear(cubemap_.faces[face_index], u, v, false);
}

[[nodiscard]] Float3Value SampleEquirect(const IblCpuImageView& image_,
                                         const Float3Value& direction_) {
    const Float3Value direction = Normalize(direction_);
    const float phi = std::atan2(direction.z, direction.x);
    const float theta = std::acos(std::clamp(direction.y, -1.0F, 1.0F));
    const float u = (phi / kTwoPi) + 0.5F;
    const float v = theta / kPi;
    return SampleImageBilinear(image_, u, v, true);
}

[[nodiscard]] Float3Value SampleSource(const IblBakeSourceDesc& source_,
                                       const Float3Value& direction_) {
    switch (source_.kind) {
    case IblBakeSourceKind::cubemap:
        return SampleCubemapFaces(source_.cubemap, direction_);
    case IblBakeSourceKind::equirectangular:
    default:
        return SampleEquirect(source_.equirect, direction_);
    }
}

[[nodiscard]] std::uint32_t ReverseBits32(std::uint32_t value_) noexcept {
    value_ = ((value_ & 0x55555555U) << 1U) | ((value_ & 0xAAAAAAAAU) >> 1U);
    value_ = ((value_ & 0x33333333U) << 2U) | ((value_ & 0xCCCCCCCCU) >> 2U);
    value_ = ((value_ & 0x0F0F0F0FU) << 4U) | ((value_ & 0xF0F0F0F0U) >> 4U);
    value_ = ((value_ & 0x00FF00FFU) << 8U) | ((value_ & 0xFF00FF00U) >> 8U);
    value_ = (value_ << 16U) | (value_ >> 16U);
    return value_;
}

[[nodiscard]] float RadicalInverseVdC(std::uint32_t bits_) noexcept {
    const std::uint32_t reversed = ReverseBits32(bits_);
    return static_cast<float>(static_cast<double>(reversed) * 2.3283064365386963e-10);
}

[[nodiscard]] Float2Value Hammersley(std::uint32_t sample_index_,
                                     std::uint32_t sample_count_) noexcept {
    return Float2Value{
        .x = (static_cast<float>(sample_index_) + 0.5F) / static_cast<float>(std::max(sample_count_, 1U)),
        .y = RadicalInverseVdC(sample_index_)
    };
}

[[nodiscard]] Float3Value ImportanceSampleGgx(const Float2Value& xi_,
                                              const Float3Value& normal_,
                                              float roughness_) noexcept {
    const float alpha = std::max(roughness_ * roughness_, 1e-4F);
    const float alpha_sq = alpha * alpha;
    const float phi = kTwoPi * xi_.x;
    const float cos_theta = std::sqrt((1.0F - xi_.y) / (1.0F + (alpha_sq - 1.0F) * xi_.y));
    const float sin_theta = std::sqrt(std::max(0.0F, 1.0F - cos_theta * cos_theta));

    const Float3Value tangent_space_half{
        .x = std::cos(phi) * sin_theta,
        .y = std::sin(phi) * sin_theta,
        .z = cos_theta
    };

    const Float3Value up = std::abs(normal_.z) < 0.999F
        ? Float3Value{.x = 0.0F, .y = 0.0F, .z = 1.0F}
        : Float3Value{.x = 1.0F, .y = 0.0F, .z = 0.0F};
    const Float3Value tangent = Normalize(Cross(up, normal_));
    const Float3Value bitangent = Cross(normal_, tangent);

    return Normalize(Add(Add(Multiply(tangent, tangent_space_half.x),
                             Multiply(bitangent, tangent_space_half.y)),
                         Multiply(normal_, tangent_space_half.z)));
}

[[nodiscard]] float GeometrySchlickGgx(float n_dot_v_,
                                       float roughness_) noexcept {
    const float alpha = roughness_ * roughness_;
    const float k = 0.5F * alpha;
    return n_dot_v_ / std::max(n_dot_v_ * (1.0F - k) + k, 1e-4F);
}

[[nodiscard]] float GeometrySmith(float n_dot_v_,
                                  float n_dot_l_,
                                  float roughness_) noexcept {
    return GeometrySchlickGgx(n_dot_v_, roughness_) *
           GeometrySchlickGgx(n_dot_l_, roughness_);
}

[[nodiscard]] Float2Value IntegrateBrdf(float n_dot_v_,
                                        float roughness_,
                                        std::uint32_t sample_count_) noexcept {
    const Float3Value view{
        .x = std::sqrt(std::max(0.0F, 1.0F - n_dot_v_ * n_dot_v_)),
        .y = 0.0F,
        .z = n_dot_v_
    };
    const Float3Value normal{.x = 0.0F, .y = 0.0F, .z = 1.0F};

    float scale = 0.0F;
    float bias = 0.0F;
    for (std::uint32_t sample_index = 0U; sample_index < sample_count_; ++sample_index) {
        const Float2Value xi = Hammersley(sample_index, sample_count_);
        const Float3Value half_vector = ImportanceSampleGgx(xi, normal, roughness_);
        const Float3Value light_dir = Normalize(Subtract(Multiply(half_vector, 2.0F * Dot(view, half_vector)),
                                                         view));

        const float n_dot_l = std::max(light_dir.z, 0.0F);
        const float n_dot_h = std::max(half_vector.z, 0.0F);
        const float v_dot_h = std::max(Dot(view, half_vector), 0.0F);
        if (n_dot_l <= 1e-6F || n_dot_h <= 1e-6F || v_dot_h <= 1e-6F) {
            continue;
        }

        const float geometry = GeometrySmith(n_dot_v_, n_dot_l, roughness_);
        const float geometry_visibility = (geometry * v_dot_h) /
                                          std::max(n_dot_h * n_dot_v_, 1e-4F);
        const float fresnel = std::pow(1.0F - v_dot_h, 5.0F);
        scale += (1.0F - fresnel) * geometry_visibility;
        bias += fresnel * geometry_visibility;
    }

    const float sample_scale = 1.0F / static_cast<float>(std::max(sample_count_, 1U));
    return Float2Value{
        .x = scale * sample_scale,
        .y = bias * sample_scale
    };
}

[[nodiscard]] Float3Value PrefilterEnvironment(const IblBakeSourceDesc& source_,
                                               const Float3Value& reflection_dir_,
                                               float roughness_,
                                               std::uint32_t sample_count_) {
    if (roughness_ <= 1e-4F) {
        return SampleSource(source_, reflection_dir_);
    }

    const Float3Value normal = reflection_dir_;
    const Float3Value view = reflection_dir_;

    Float3Value prefiltered{};
    float total_weight = 0.0F;
    for (std::uint32_t sample_index = 0U; sample_index < sample_count_; ++sample_index) {
        const Float2Value xi = Hammersley(sample_index, sample_count_);
        const Float3Value half_vector = ImportanceSampleGgx(xi, normal, roughness_);
        const Float3Value light_dir = Normalize(Subtract(Multiply(half_vector, 2.0F * Dot(view, half_vector)),
                                                         view));
        const float n_dot_l = std::max(Dot(normal, light_dir), 0.0F);
        if (n_dot_l <= 1e-6F) {
            continue;
        }
        prefiltered = Add(prefiltered, Multiply(SampleSource(source_, light_dir), n_dot_l));
        total_weight += n_dot_l;
    }

    if (total_weight <= 1e-6F) {
        return SampleSource(source_, reflection_dir_);
    }
    return Divide(prefiltered, total_weight);
}

void EvaluateSh9Basis(const Float3Value& direction_,
                      float out_basis_[9U]) noexcept {
    const Float3Value direction = Normalize(direction_);
    const float x = direction.x;
    const float y = direction.y;
    const float z = direction.z;

    out_basis_[0U] = 0.282095F;
    out_basis_[1U] = 0.488603F * y;
    out_basis_[2U] = 0.488603F * z;
    out_basis_[3U] = 0.488603F * x;
    out_basis_[4U] = 1.092548F * x * y;
    out_basis_[5U] = 1.092548F * y * z;
    out_basis_[6U] = 0.315392F * (3.0F * z * z - 1.0F);
    out_basis_[7U] = 1.092548F * x * z;
    out_basis_[8U] = 0.546274F * (x * x - y * y);
}

[[nodiscard]] std::array<std::array<float, 4U>, 9U> ProjectSh9(const IblBakeSourceDesc& source_,
                                                                std::uint32_t sample_count_) {
    std::array<std::array<float, 4U>, 9U> coefficients{};
    const std::uint32_t safe_sample_count = std::max(sample_count_, 64U);
    const float weight_scale = (4.0F * kPi) / static_cast<float>(safe_sample_count);

    for (std::uint32_t sample_index = 0U; sample_index < safe_sample_count; ++sample_index) {
        const Float2Value xi = Hammersley(sample_index, safe_sample_count);
        const float y = 1.0F - 2.0F * xi.x;
        const float radius = std::sqrt(std::max(0.0F, 1.0F - y * y));
        const float phi = kTwoPi * xi.y;
        const Float3Value direction{
            .x = std::cos(phi) * radius,
            .y = y,
            .z = std::sin(phi) * radius
        };
        const Float3Value radiance = SampleSource(source_, direction);
        float basis[9U]{};
        EvaluateSh9Basis(direction, basis);
        for (std::uint32_t coefficient_index = 0U; coefficient_index < 9U; ++coefficient_index) {
            coefficients[coefficient_index][0U] += radiance.x * basis[coefficient_index] * weight_scale;
            coefficients[coefficient_index][1U] += radiance.y * basis[coefficient_index] * weight_scale;
            coefficients[coefficient_index][2U] += radiance.z * basis[coefficient_index] * weight_scale;
        }
    }

    return coefficients;
}

[[nodiscard]] VkFormat ResolveBrdfLutFormat(VulkanContext& context_) noexcept {
    if (asset::TextureHost::SupportsSampledFormat(context_, VK_FORMAT_R16G16_SFLOAT)) {
        return VK_FORMAT_R16G16_SFLOAT;
    }
    if (asset::TextureHost::SupportsSampledFormat(context_, VK_FORMAT_R16G16B16A16_SFLOAT)) {
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    if (asset::TextureHost::SupportsSampledFormat(context_, VK_FORMAT_R8G8B8A8_UNORM)) {
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] VkFormat ResolveEnvironmentCubeFormat(VulkanContext& context_) noexcept {
    if (asset::TextureHost::SupportsSampledFormat(context_, VK_FORMAT_R16G16B16A16_SFLOAT)) {
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    if (asset::TextureHost::SupportsSampledFormat(context_, VK_FORMAT_R8G8B8A8_UNORM)) {
        return VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

void ValidateImageView(const IblCpuImageView& view_,
                       const char* label_) {
    if (view_.pixels == nullptr) {
        throw std::invalid_argument(std::string(label_) + " pixels must be non-null");
    }
    if (view_.width == 0U || view_.height == 0U) {
        throw std::invalid_argument(std::string(label_) + " dimensions must be > 0");
    }
    const std::uint32_t row_pitch = ResolveRowPitchPixels(view_);
    if (row_pitch < view_.width) {
        throw std::invalid_argument(std::string(label_) + " row_pitch_pixels must be >= width");
    }
}

void ValidateSource(const IblBakeSourceDesc& source_) {
    switch (source_.kind) {
    case IblBakeSourceKind::equirectangular:
        ValidateImageView(source_.equirect, "IblBakeSourceDesc.equirect");
        break;
    case IblBakeSourceKind::cubemap: {
        const IblCpuImageView& first_face = source_.cubemap.faces[0U];
        ValidateImageView(first_face, "IblBakeSourceDesc.cubemap.face0");
        if (first_face.width != first_face.height) {
            throw std::invalid_argument("IblBakeSourceDesc cubemap faces must be square");
        }
        for (std::uint32_t face_index = 1U; face_index < source_.cubemap.faces.size(); ++face_index) {
            const IblCpuImageView& face = source_.cubemap.faces[face_index];
            ValidateImageView(face, "IblBakeSourceDesc.cubemap.face");
            if (face.width != first_face.width || face.height != first_face.height) {
                throw std::invalid_argument("IblBakeSourceDesc cubemap faces must share identical dimensions");
            }
        }
        break;
    }
    default:
        throw std::invalid_argument("IblBakeSourceDesc contains an unsupported source kind");
    }
}

void BuildSkyboxFacePixels(VkFormat format_,
                           const IblBakeSourceDesc& source_,
                           std::uint32_t face_size_,
                           std::uint32_t face_index_,
                           IblBakeMcVector<std::uint8_t>& out_bytes_) {
    switch (format_) {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        out_bytes_.resize(static_cast<std::size_t>(face_size_) * face_size_ * 8U);
        break;
    case VK_FORMAT_R8G8B8A8_UNORM:
        out_bytes_.resize(static_cast<std::size_t>(face_size_) * face_size_ * 4U);
        break;
    default:
        throw std::runtime_error("IblBakeHost::BuildSkyboxFacePixels encountered unsupported format");
    }
    for (std::uint32_t y = 0U; y < face_size_; ++y) {
        for (std::uint32_t x = 0U; x < face_size_; ++x) {
            const Float3Value direction = CubeTexelDirection(face_index_, x, y, face_size_);
            const Float3Value sample = SampleSource(source_, direction);
            const std::size_t texel_index = static_cast<std::size_t>(y) * face_size_ + x;
            switch (format_) {
            case VK_FORMAT_R16G16B16A16_SFLOAT:
                WriteRgba16f(out_bytes_, texel_index, sample, 1.0F);
                break;
            case VK_FORMAT_R8G8B8A8_UNORM:
                WriteRgba8(out_bytes_, texel_index, sample, 1.0F);
                break;
            default:
                break;
            }
        }
    }
}

void BuildPrefilteredFacePixels(VkFormat format_,
                                const IblBakeSourceDesc& source_,
                                std::uint32_t face_size_,
                                std::uint32_t face_index_,
                                float roughness_,
                                std::uint32_t sample_count_,
                                IblBakeMcVector<std::uint8_t>& out_bytes_) {
    switch (format_) {
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        out_bytes_.resize(static_cast<std::size_t>(face_size_) * face_size_ * 8U);
        break;
    case VK_FORMAT_R8G8B8A8_UNORM:
        out_bytes_.resize(static_cast<std::size_t>(face_size_) * face_size_ * 4U);
        break;
    default:
        throw std::runtime_error("IblBakeHost::BuildPrefilteredFacePixels encountered unsupported format");
    }
    for (std::uint32_t y = 0U; y < face_size_; ++y) {
        for (std::uint32_t x = 0U; x < face_size_; ++x) {
            const Float3Value direction = CubeTexelDirection(face_index_, x, y, face_size_);
            const Float3Value sample = PrefilterEnvironment(source_,
                                                            direction,
                                                            roughness_,
                                                            sample_count_);
            const std::size_t texel_index = static_cast<std::size_t>(y) * face_size_ + x;
            switch (format_) {
            case VK_FORMAT_R16G16B16A16_SFLOAT:
                WriteRgba16f(out_bytes_, texel_index, sample, 1.0F);
                break;
            case VK_FORMAT_R8G8B8A8_UNORM:
                WriteRgba8(out_bytes_, texel_index, sample, 1.0F);
                break;
            default:
                break;
            }
        }
    }
}

void BuildBrdfLutPixels(VkFormat format_,
                        std::uint32_t size_,
                        std::uint32_t sample_count_,
                        IblBakeMcVector<std::uint8_t>& out_bytes_) {
    const std::size_t texel_count = static_cast<std::size_t>(size_) * size_;
    switch (format_) {
    case VK_FORMAT_R16G16_SFLOAT:
        out_bytes_.resize(texel_count * 4U);
        break;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        out_bytes_.resize(texel_count * 8U);
        break;
    case VK_FORMAT_R8G8B8A8_UNORM:
        out_bytes_.resize(texel_count * 4U);
        break;
    default:
        throw std::runtime_error("IblBakeHost::BuildBrdfLutPixels encountered unsupported format");
    }

    for (std::uint32_t y = 0U; y < size_; ++y) {
        const float roughness = (static_cast<float>(y) + 0.5F) / static_cast<float>(size_);
        for (std::uint32_t x = 0U; x < size_; ++x) {
            const float n_dot_v = (static_cast<float>(x) + 0.5F) / static_cast<float>(size_);
            const Float2Value integrated = IntegrateBrdf(n_dot_v,
                                                         roughness,
                                                         std::max(sample_count_, 64U));
            const std::size_t texel_index = static_cast<std::size_t>(y) * size_ + x;
            switch (format_) {
            case VK_FORMAT_R16G16_SFLOAT:
                WriteRg16f(out_bytes_, texel_index, integrated.x, integrated.y);
                break;
            case VK_FORMAT_R16G16B16A16_SFLOAT:
                WriteRgba16f(out_bytes_,
                             texel_index,
                             Float3Value{.x = integrated.x, .y = integrated.y, .z = 0.0F},
                             1.0F);
                break;
            case VK_FORMAT_R8G8B8A8_UNORM:
                WriteRgba8(out_bytes_,
                           texel_index,
                           Float3Value{.x = integrated.x, .y = integrated.y, .z = 0.0F},
                           1.0F);
                break;
            default:
                break;
            }
        }
    }
}

[[nodiscard]] asset::TextureCreateInfo BuildCubeTextureCreateInfo(asset::TextureId texture_id_,
                                                                  std::uint32_t face_size_,
                                                                  std::uint32_t mip_levels_,
                                                                  VkFormat format_) noexcept {
    asset::TextureCreateInfo create_info{};
    create_info.texture_id = texture_id_;
    create_info.image_flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    create_info.default_view_type = VK_IMAGE_VIEW_TYPE_CUBE;
    create_info.format = format_;
    create_info.extent = VkExtent3D{face_size_, face_size_, 1U};
    create_info.mip_levels = mip_levels_;
    create_info.array_layers = 6U;
    create_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    create_info.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    create_info.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    return create_info;
}

[[nodiscard]] std::string MakeTextureLabel(const char* kind_,
                                           asset::TextureId texture_id_) {
    return std::string("IblBakeHost ") + kind_ + " texture 0x" + std::to_string(texture_id_.value);
}

} // namespace

void IblBakeHost::Initialize(asset::TextureHost& texture_host_,
                             IblHost* ibl_host_,
                             const IblBakeHostCreateInfo& create_info_) {
    texture_host = &texture_host_;
    ibl_host = ibl_host_;
    create_info_cache = create_info_;
    owned_texture_ids.clear();
    if (create_info_cache.reserve_owned_texture_count > 0U) {
        owned_texture_ids.reserve(create_info_cache.reserve_owned_texture_count);
    }
    cached_brdf_lut_texture_id = {};
    stats = {};
    next_generated_texture_id = 0xFFF10000U;
    initialized = true;
}

void IblBakeHost::Shutdown(VulkanContext& context_) {
    if (!initialized) {
        return;
    }
    RemoveOwnedTextures(context_);
    texture_host = nullptr;
    ibl_host = nullptr;
    create_info_cache = {};
    owned_texture_ids.clear();
    cached_brdf_lut_texture_id = {};
    stats = {};
    next_generated_texture_id = 0xFFF10000U;
    initialized = false;
}

asset::TextureId IblBakeHost::EnsureBrdfLut(const IblBakeHostPrepareView& prepare_view_,
                                            asset::TextureId texture_id_,
                                            std::uint32_t lut_size_,
                                            std::uint32_t sample_count_) {
    if (!initialized || texture_host == nullptr) {
        throw std::runtime_error("IblBakeHost::EnsureBrdfLut called before Initialize");
    }

    const std::uint32_t resolved_lut_size =
        lut_size_ > 0U ? lut_size_ : std::max(create_info_cache.default_brdf_lut_size, 1U);
    const std::uint32_t resolved_sample_count =
        sample_count_ > 0U ? sample_count_ : create_info_cache.default_brdf_sample_count;

    if (!texture_id_.IsValid()) {
        texture_id_ = cached_brdf_lut_texture_id.IsValid()
            ? cached_brdf_lut_texture_id
            : AllocateTextureId();
    }

    const VkFormat brdf_lut_format = ResolveBrdfLutFormat(prepare_view_.device);
    if (brdf_lut_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("IblBakeHost::EnsureBrdfLut could not resolve a sampled BRDF LUT format");
    }

    if (const auto* existing = texture_host->FindTexture(texture_id_);
        existing != nullptr &&
        existing->default_view_type == VK_IMAGE_VIEW_TYPE_2D &&
        existing->mip_levels == 1U &&
        existing->array_layers == 1U &&
        existing->format == brdf_lut_format &&
        existing->extent.width == resolved_lut_size &&
        existing->extent.height == resolved_lut_size) {
        cached_brdf_lut_texture_id = texture_id_;
        IblHost* resolved_ibl_host = ibl_host != nullptr ? ibl_host : prepare_view_.ibl;
        if (resolved_ibl_host != nullptr && create_info_cache.auto_register_brdf_lut_with_ibl_host) {
            resolved_ibl_host->SetBrdfLut(texture_id_);
        }
        return texture_id_;
    }

    IblBakeMcVector<std::uint8_t> bytes{};
    BuildBrdfLutPixels(brdf_lut_format,
                       resolved_lut_size,
                       resolved_sample_count,
                       bytes);

    asset::TextureSubresourceUploadInfo subresource{};
    subresource.pixels = bytes.data();
    subresource.size_bytes = static_cast<VkDeviceSize>(bytes.size());
    subresource.mip_level = 0U;
    subresource.base_array_layer = 0U;
    subresource.layer_count = 1U;
    subresource.image_extent = VkExtent3D{
        resolved_lut_size,
        resolved_lut_size,
        1U
    };

    asset::TextureUploadInfo upload_info{};
    upload_info.create.texture_id = texture_id_;
    upload_info.create.default_view_type = VK_IMAGE_VIEW_TYPE_2D;
    upload_info.create.format = brdf_lut_format;
    upload_info.create.extent = subresource.image_extent;
    upload_info.create.mip_levels = 1U;
    upload_info.create.array_layers = 1U;
    upload_info.create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    upload_info.create.shader_read_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    upload_info.create.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
    upload_info.subresources = &subresource;
    upload_info.subresource_count = 1U;

    texture_host->UploadTexture(prepare_view_.device,
                                prepare_view_.upload,
                                prepare_view_.frame.frame_index,
                                prepare_view_.progress.last_submitted_value,
                                prepare_view_.progress.completed_submit_value,
                                upload_info);

    TrackOwnedTexture(texture_id_);
    cached_brdf_lut_texture_id = texture_id_;
    ++stats.baked_brdf_lut_count;
    ++stats.generated_texture_count;
    ++stats.revision;

    IblHost* resolved_ibl_host = ibl_host != nullptr ? ibl_host : prepare_view_.ibl;
    if (resolved_ibl_host != nullptr && create_info_cache.auto_register_brdf_lut_with_ibl_host) {
        resolved_ibl_host->SetBrdfLut(texture_id_);
    }

    return texture_id_;
}

IblBakeResult IblBakeHost::BakeEnvironment(const IblBakeHostPrepareView& prepare_view_,
                                           const IblBakeRequest& request_) {
    if (!initialized || texture_host == nullptr) {
        throw std::runtime_error("IblBakeHost::BakeEnvironment called before Initialize");
    }
    if (!request_.bake_specular) {
        throw std::invalid_argument("IblBakeHost::BakeEnvironment requires bake_specular == true");
    }
    ValidateSource(request_.source);

    IblBakeResult result{};
    const std::uint32_t skybox_size = std::max(request_.skybox_cube_size, 1U);
    const std::uint32_t specular_size = std::max(request_.specular_cube_size, 1U);
    const std::uint32_t specular_mip_levels = ComputeMipLevelCount(specular_size);
    const std::uint32_t specular_sample_count =
        request_.specular_sample_count > 0U
            ? request_.specular_sample_count
            : create_info_cache.default_specular_sample_count;
    const std::uint32_t sh_sample_count =
        request_.sh_sample_count > 0U
            ? request_.sh_sample_count
            : create_info_cache.default_sh_sample_count;
    const VkFormat environment_cube_format = ResolveEnvironmentCubeFormat(prepare_view_.device);
    if (environment_cube_format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("IblBakeHost::BakeEnvironment could not resolve a sampled environment cubemap format");
    }

    if (request_.bake_brdf_lut) {
        result.brdf_lut_size = request_.brdf_lut_size > 0U
            ? request_.brdf_lut_size
            : create_info_cache.default_brdf_lut_size;
        result.brdf_lut = EnsureBrdfLut(prepare_view_,
                                        request_.brdf_lut_texture_id,
                                        result.brdf_lut_size,
                                        request_.brdf_sample_count);
    } else if (request_.brdf_lut_texture_id.IsValid()) {
        result.brdf_lut = request_.brdf_lut_texture_id;
    } else {
        IblHost* resolved_ibl_host = ibl_host != nullptr ? ibl_host : prepare_view_.ibl;
        if (resolved_ibl_host != nullptr) {
            result.brdf_lut = resolved_ibl_host->BrdfLut();
        }
    }

    result.specular_cube = request_.specular_texture_id.IsValid()
        ? request_.specular_texture_id
        : AllocateTextureId();
    result.skybox_cube = request_.bake_skybox
        ? (request_.skybox_texture_id.IsValid() ? request_.skybox_texture_id : AllocateTextureId())
        : result.specular_cube;
    result.specular_mip_levels = specular_mip_levels;

    if (request_.bake_skybox) {
        IblBakeMcVector<IblBakeMcVector<std::uint8_t>> face_bytes{};
        IblBakeMcVector<asset::TextureSubresourceUploadInfo> subresources{};
        face_bytes.resize(6U);
        subresources.resize(6U);
        for (std::uint32_t face_index = 0U; face_index < 6U; ++face_index) {
            BuildSkyboxFacePixels(environment_cube_format,
                                 request_.source,
                                 skybox_size,
                                 face_index,
                                 face_bytes[face_index]);
            subresources[face_index].pixels = face_bytes[face_index].data();
            subresources[face_index].size_bytes = static_cast<VkDeviceSize>(face_bytes[face_index].size());
            subresources[face_index].mip_level = 0U;
            subresources[face_index].base_array_layer = face_index;
            subresources[face_index].layer_count = 1U;
            subresources[face_index].image_extent = VkExtent3D{skybox_size, skybox_size, 1U};
        }

        asset::TextureUploadInfo upload_info{};
        upload_info.create = BuildCubeTextureCreateInfo(result.skybox_cube,
                                                        skybox_size,
                                                        1U,
                                                        environment_cube_format);
        upload_info.subresources = subresources.data();
        upload_info.subresource_count = 6U;

            texture_host->UploadTexture(prepare_view_.device,
                                        prepare_view_.upload,
                                        prepare_view_.frame.frame_index,
                                        prepare_view_.progress.last_submitted_value,
                                        prepare_view_.progress.completed_submit_value,
                                        upload_info);
        TrackOwnedTexture(result.skybox_cube);
        ++stats.generated_texture_count;
    }

    {
        const std::uint32_t total_subresource_count = specular_mip_levels * 6U;
        IblBakeMcVector<IblBakeMcVector<std::uint8_t>> subresource_bytes{};
        IblBakeMcVector<asset::TextureSubresourceUploadInfo> subresources{};
        subresource_bytes.resize(total_subresource_count);
        subresources.resize(total_subresource_count);

        std::uint32_t subresource_index = 0U;
        for (std::uint32_t mip_level = 0U; mip_level < specular_mip_levels; ++mip_level) {
            const std::uint32_t mip_size = std::max<std::uint32_t>(1U, specular_size >> mip_level);
            const float roughness = specular_mip_levels > 1U
                ? static_cast<float>(mip_level) / static_cast<float>(specular_mip_levels - 1U)
                : 0.0F;
            for (std::uint32_t face_index = 0U; face_index < 6U; ++face_index, ++subresource_index) {
                BuildPrefilteredFacePixels(environment_cube_format,
                                           request_.source,
                                           mip_size,
                                           face_index,
                                           roughness,
                                           specular_sample_count,
                                           subresource_bytes[subresource_index]);
                subresources[subresource_index].pixels = subresource_bytes[subresource_index].data();
                subresources[subresource_index].size_bytes =
                    static_cast<VkDeviceSize>(subresource_bytes[subresource_index].size());
                subresources[subresource_index].mip_level = mip_level;
                subresources[subresource_index].base_array_layer = face_index;
                subresources[subresource_index].layer_count = 1U;
                subresources[subresource_index].image_extent = VkExtent3D{mip_size, mip_size, 1U};
            }
        }

        asset::TextureUploadInfo upload_info{};
        upload_info.create = BuildCubeTextureCreateInfo(result.specular_cube,
                                                        specular_size,
                                                        specular_mip_levels,
                                                        environment_cube_format);
        upload_info.subresources = subresources.data();
        upload_info.subresource_count = total_subresource_count;

        texture_host->UploadTexture(prepare_view_.device,
                                    prepare_view_.upload,
                                    prepare_view_.frame.frame_index,
                                    prepare_view_.progress.last_submitted_value,
                                    prepare_view_.progress.completed_submit_value,
                                    upload_info);
        TrackOwnedTexture(result.specular_cube);
        ++stats.generated_texture_count;
    }

    if (request_.bake_sh9) {
        result.environment.sh9 = ProjectSh9(request_.source, sh_sample_count);
    }

    result.environment.environment_id = request_.environment_id;
    result.environment.specular_cube = result.specular_cube;
    result.environment.skybox_cube = request_.bake_skybox ? result.skybox_cube : result.specular_cube;
    result.environment.intensity = request_.intensity;
    result.environment.rotation_y_radians = request_.rotation_y_radians;
    result.environment.max_specular_lod =
        specular_mip_levels > 0U ? static_cast<float>(specular_mip_levels - 1U) : 0.0F;
    result.environment.tint_color = request_.tint_color;
    result.environment.replace_existing = true;

    IblHost* resolved_ibl_host = ibl_host != nullptr ? ibl_host : prepare_view_.ibl;
    if (resolved_ibl_host != nullptr &&
        create_info_cache.auto_register_environment_with_ibl_host) {
        result.environment_id = resolved_ibl_host->RegisterEnvironment(prepare_view_.device,
                                                                       result.environment);
        result.environment.environment_id = result.environment_id;
        result.registered_with_ibl_host = true;
    } else {
        result.environment_id = result.environment.environment_id;
    }

    ++stats.baked_environment_count;
    stats.owned_texture_count = static_cast<std::uint32_t>(owned_texture_ids.size());
    ++stats.revision;
    return result;
}

bool IblBakeHost::IsInitialized() const noexcept {
    return initialized;
}

const IblBakeHostStats& IblBakeHost::Stats() const noexcept {
    return stats;
}

asset::TextureId IblBakeHost::AllocateTextureId() noexcept {
    const asset::TextureId texture_id{next_generated_texture_id++};
    return texture_id;
}

void IblBakeHost::TrackOwnedTexture(asset::TextureId texture_id_) {
    if (!texture_id_.IsValid()) {
        return;
    }
    for (const auto existing_texture_id : owned_texture_ids) {
        if (existing_texture_id.value == texture_id_.value) {
            return;
        }
    }
    owned_texture_ids.push_back(texture_id_);
    stats.owned_texture_count = static_cast<std::uint32_t>(owned_texture_ids.size());
}

void IblBakeHost::RemoveOwnedTextures(VulkanContext& context_) noexcept {
    if (texture_host == nullptr) {
        owned_texture_ids.clear();
        stats.owned_texture_count = 0U;
        return;
    }

    for (const auto texture_id : owned_texture_ids) {
        (void)texture_host->RemoveTexture(context_,
                                          texture_id,
                                          kImmediateRetireValue,
                                          kImmediateRetireValue);
    }
    texture_host->BeginFrame(context_, kImmediateRetireValue);
    owned_texture_ids.clear();
    stats.owned_texture_count = 0U;
}

} // namespace vr::render


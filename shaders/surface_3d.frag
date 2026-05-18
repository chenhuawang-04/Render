#version 460
#extension GL_GOOGLE_include_directive : require

#include "vr/render/bindless.glsl"
#include "vr/render/pbr.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 1) flat in uint in_appearance_record_index;
layout(location = 2) in vec3 in_world_position;
layout(location = 3) in vec3 in_normal_world;
layout(location = 4) in vec3 in_tangent_world;
layout(location = 5) in vec3 in_bitangent_world;

layout(push_constant) uniform Surface3DPushConstants {
    mat4 view_projection;
    vec4 camera_position;
    uint params;
    uint ibl_specular_texture_slot;
    uint ibl_brdf_lut_texture_slot;
    uint ibl_sampler_slot;
} pc;

struct AppearanceGpuRecord {
    vec4 base_rgba;
    vec4 emissive_rgba;
    vec4 appearance_params;
    vec4 extras;
    uvec4 flags_u32;
    uvec4 textures0_u32;
    uvec4 textures1_u32;
};

layout(set = 2, binding = 8, std430) readonly buffer AppearanceRecordBuffer {
    AppearanceGpuRecord appearance_records[];
};

layout(set = 3, binding = 0, std140) uniform IblParamsBuffer {
    vec4 ibl_sh9[9];
    vec4 ibl_tint_intensity;
    vec4 ibl_rotation_max_lod_flags;
    uvec4 texture_sampler_slots;
} ibl_params;

layout(location = 0) out vec4 out_color;

const uint k_invalid_appearance_record_index = 0xFFFFFFFFu;
const uint k_appearance_alpha_mode_shift = 3u;
const uint k_appearance_shading_model_shift = 5u;
const uint k_appearance_alpha_mode_masked = 1u;
const uint k_appearance_shading_model_unlit = 0u;
const uint k_appearance_texture_presence_base_color = 1u << 0u;
const uint k_appearance_texture_presence_normal = 1u << 1u;
const uint k_appearance_texture_presence_metal_rough = 1u << 2u;
const uint k_appearance_texture_presence_occlusion = 1u << 3u;
const uint k_appearance_texture_presence_emissive = 1u << 4u;

#include "vr/render/appearance_decode_3d.glsl"

vec3 rotate_environment_direction(vec3 direction_) {
    float sin_y = ibl_params.ibl_rotation_max_lod_flags.x;
    float cos_y = ibl_params.ibl_rotation_max_lod_flags.y;
    vec3 rotated = vec3(
        cos_y * direction_.x + sin_y * direction_.z,
        direction_.y,
        -sin_y * direction_.x + cos_y * direction_.z);
    float length_sq = dot(rotated, rotated);
    if (length_sq <= 1e-6) {
        return direction_;
    }
    return rotated * inversesqrt(length_sq);
}

vec3 evaluate_sh9_irradiance(vec3 normal_) {
    vec3 n = rotate_environment_direction(normalize(normal_));
    float x = n.x;
    float y = n.y;
    float z = n.z;

    float basis[9];
    basis[0] = 0.282095;
    basis[1] = 0.488603 * y;
    basis[2] = 0.488603 * z;
    basis[3] = 0.488603 * x;
    basis[4] = 1.092548 * x * y;
    basis[5] = 1.092548 * y * z;
    basis[6] = 0.315392 * (3.0 * z * z - 1.0);
    basis[7] = 1.092548 * x * z;
    basis[8] = 0.546274 * (x * x - y * y);

    vec3 irradiance = vec3(0.0);
    for (int i = 0; i < 9; ++i) {
        irradiance += ibl_params.ibl_sh9[i].rgb * basis[i];
    }
    return max(irradiance, vec3(0.0));
}

DecodedAppearance3D decode_surface_appearance(vec3 normal_world_) {
    AppearanceGpuRecord appearance_record = appearance_records[in_appearance_record_index];
    return decode_appearance_record_3d(appearance_record,
                                       in_uv,
                                       normal_world_,
                                       in_tangent_world,
                                       in_bitangent_world);
}

vec3 evaluate_surface_ibl(AppearanceSample3D appearance_,
                          vec3 view_dir_) {
    float ibl_intensity = max(ibl_params.ibl_tint_intensity.w, 0.0);
    if (ibl_intensity <= 1e-6) {
        return vec3(0.0);
    }

    vec3 diffuse_irradiance = evaluate_sh9_irradiance(appearance_.normal_world);
    vec3 reflection_dir = rotate_environment_direction(reflect(-normalize(view_dir_),
                                                               normalize(appearance_.normal_world)));
    float max_specular_lod = max(ibl_params.ibl_rotation_max_lod_flags.z, 0.0);
    vec3 prefiltered_specular =
        SampleTextureCubeLod(pc.ibl_specular_texture_slot,
                             pc.ibl_sampler_slot,
                             reflection_dir,
                             appearance_.roughness * max_specular_lod).rgb;
    float n_dot_v = max(dot(normalize(appearance_.normal_world), normalize(view_dir_)), 0.0);
    vec2 brdf = SampleTexture2D(pc.ibl_brdf_lut_texture_slot,
                                pc.ibl_sampler_slot,
                                vec2(n_dot_v, appearance_.roughness)).rg;
    return EvaluatePbrIblFromTerms(appearance_,
                                   view_dir_,
                                   diffuse_irradiance,
                                   prefiltered_specular,
                                   brdf) *
           ibl_params.ibl_tint_intensity.rgb *
           ibl_intensity;
}

void main() {
    DecodedAppearance3D appearance_state = decode_surface_appearance(in_normal_world);
    AppearanceSample3D appearance = appearance_state.appearance;
    if (appearance_state.alpha_test_enabled && appearance.alpha < appearance_state.alpha_cutoff) {
        discard;
    }
    if (appearance.alpha <= 1e-5) {
        discard;
    }

    if (appearance_state.unlit) {
        out_color = vec4(appearance.base_color + appearance.emissive, appearance.alpha);
        return;
    }

    vec3 camera_position = pc.camera_position.xyz;
    vec3 view_dir = normalize(camera_position - in_world_position);
    vec3 ibl_lighting = evaluate_surface_ibl(appearance, view_dir);
    vec3 shaded_rgb = ibl_lighting * appearance.occlusion + appearance.emissive;
    out_color = vec4(shaded_rgb, appearance.alpha);
}

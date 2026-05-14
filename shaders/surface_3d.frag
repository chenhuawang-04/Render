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

layout(set = 2, binding = 0, std430) readonly buffer AppearanceRecordBuffer {
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

struct DecodedSurfaceAppearance {
    MaterialSample material;
    bool alpha_test_enabled;
    float alpha_cutoff;
    bool unlit;
};

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

bool appearance_texture_present(uint presence_mask_, uint flag_) {
    return (presence_mask_ & flag_) != 0u;
}

uint appearance_alpha_mode(AppearanceGpuRecord record_) {
    return (record_.flags_u32.x >> k_appearance_alpha_mode_shift) & 0x3u;
}

uint appearance_shading_model(AppearanceGpuRecord record_) {
    return (record_.flags_u32.x >> k_appearance_shading_model_shift) & 0x3u;
}

bool has_valid_tangent_basis() {
    return dot(in_tangent_world, in_tangent_world) > 1e-6 &&
           dot(in_bitangent_world, in_bitangent_world) > 1e-6;
}

DecodedSurfaceAppearance decode_surface_material(vec3 normal_world_) {
    DecodedSurfaceAppearance decoded;
    AppearanceGpuRecord appearance_record = appearance_records[in_appearance_record_index];
    uint presence_mask = appearance_record.textures1_u32.w;
    uint sampler_slot = appearance_record.textures1_u32.y;
    vec4 base_factor = appearance_record.base_rgba;
    vec4 emissive_factor = appearance_record.emissive_rgba;
    vec4 appearance_params = appearance_record.appearance_params;
    vec4 extras = appearance_record.extras;
    float occlusion_strength = clamp(appearance_params.w, 0.0, 1.0);

    vec4 base_sample = vec4(1.0);
    if (appearance_texture_present(presence_mask, k_appearance_texture_presence_base_color)) {
        base_sample = SampleTexture2D(appearance_record.textures0_u32.x,
                                      sampler_slot,
                                      in_uv);
    }

    decoded.material.base_color = base_sample.rgb * base_factor.rgb;
    decoded.material.alpha = base_sample.a * base_factor.a * clamp(extras.z, 0.0, 1.0);
    decoded.material.metallic = clamp(appearance_params.x, 0.0, 1.0);
    decoded.material.roughness = clamp(appearance_params.y, 0.04, 1.0);
    decoded.material.normal_scale = max(appearance_params.z, 0.0);
    decoded.material.occlusion = occlusion_strength;
    decoded.material.normal_world = normalize(normal_world_);

    vec3 orm_sample = vec3(1.0);
    if (appearance_texture_present(presence_mask, k_appearance_texture_presence_metal_rough)) {
        orm_sample = SampleTexture2D(appearance_record.textures0_u32.z,
                                     sampler_slot,
                                     in_uv).rgb;
        decoded.material.roughness = clamp(orm_sample.g * decoded.material.roughness, 0.04, 1.0);
        decoded.material.metallic = clamp(orm_sample.b * decoded.material.metallic, 0.0, 1.0);
    }

    float occlusion_value = 1.0;
    if (appearance_texture_present(presence_mask, k_appearance_texture_presence_occlusion)) {
        occlusion_value = SampleTexture2D(appearance_record.textures0_u32.w,
                                          sampler_slot,
                                          in_uv).r;
    } else if (appearance_texture_present(presence_mask, k_appearance_texture_presence_metal_rough)) {
        occlusion_value = orm_sample.r;
    }
    decoded.material.occlusion = mix(1.0, occlusion_value, occlusion_strength);

    if (appearance_texture_present(presence_mask, k_appearance_texture_presence_normal) &&
        has_valid_tangent_basis()) {
        vec3 tangent_normal =
            SampleTexture2D(appearance_record.textures0_u32.y,
                            sampler_slot,
                            in_uv).xyz * 2.0 - 1.0;
        tangent_normal.xy *= decoded.material.normal_scale;
        tangent_normal = normalize(tangent_normal);

        vec3 tangent_world = normalize(in_tangent_world);
        vec3 bitangent_world = normalize(in_bitangent_world);
        mat3 tbn = mat3(tangent_world, bitangent_world, normalize(normal_world_));
        decoded.material.normal_world = normalize(tbn * tangent_normal);
    }

    vec3 emissive_sample = vec3(1.0);
    if (appearance_texture_present(presence_mask, k_appearance_texture_presence_emissive)) {
        emissive_sample =
            SampleTexture2D(appearance_record.textures1_u32.x,
                            sampler_slot,
                            in_uv).rgb;
    }
    decoded.material.emissive = emissive_sample * emissive_factor.rgb * max(extras.x, 0.0);
    decoded.alpha_test_enabled =
        appearance_alpha_mode(appearance_record) == k_appearance_alpha_mode_masked;
    decoded.alpha_cutoff = clamp(extras.y, 0.0, 1.0);
    decoded.unlit = appearance_shading_model(appearance_record) == k_appearance_shading_model_unlit;
    return decoded;
}

vec3 evaluate_surface_ibl(MaterialSample material_,
                          vec3 view_dir_) {
    float ibl_intensity = max(ibl_params.ibl_tint_intensity.w, 0.0);
    if (ibl_intensity <= 1e-6) {
        return vec3(0.0);
    }

    vec3 diffuse_irradiance = evaluate_sh9_irradiance(material_.normal_world);
    vec3 reflection_dir = rotate_environment_direction(reflect(-normalize(view_dir_),
                                                               normalize(material_.normal_world)));
    float max_specular_lod = max(ibl_params.ibl_rotation_max_lod_flags.z, 0.0);
    vec3 prefiltered_specular =
        SampleTextureCubeLod(pc.ibl_specular_texture_slot,
                             pc.ibl_sampler_slot,
                             reflection_dir,
                             material_.roughness * max_specular_lod).rgb;
    float n_dot_v = max(dot(normalize(material_.normal_world), normalize(view_dir_)), 0.0);
    vec2 brdf = SampleTexture2D(pc.ibl_brdf_lut_texture_slot,
                                pc.ibl_sampler_slot,
                                vec2(n_dot_v, material_.roughness)).rg;
    return EvaluatePbrIblFromTerms(material_,
                                   view_dir_,
                                   diffuse_irradiance,
                                   prefiltered_specular,
                                   brdf) *
           ibl_params.ibl_tint_intensity.rgb *
           ibl_intensity;
}

void main() {
    DecodedSurfaceAppearance surface_state = decode_surface_material(in_normal_world);
    MaterialSample material = surface_state.material;
    if (surface_state.alpha_test_enabled && material.alpha < surface_state.alpha_cutoff) {
        discard;
    }
    if (material.alpha <= 1e-5) {
        discard;
    }

    if (surface_state.unlit) {
        out_color = vec4(material.base_color + material.emissive, material.alpha);
        return;
    }

    vec3 camera_position = pc.camera_position.xyz;
    vec3 view_dir = normalize(camera_position - in_world_position);
    vec3 ibl_lighting = evaluate_surface_ibl(material, view_dir);
    vec3 shaded_rgb = ibl_lighting * material.occlusion + material.emissive;
    out_color = vec4(shaded_rgb, material.alpha);
}

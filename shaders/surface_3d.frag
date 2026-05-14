#version 460
#extension GL_GOOGLE_include_directive : require

#include "vr/render/bindless.glsl"
#include "vr/render/pbr.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 2) flat in uint in_params;
layout(location = 3) flat in uint in_texture_slot;
layout(location = 4) flat in uint in_sampler_slot;
layout(location = 5) flat in uint in_material_id;
layout(location = 6) flat in uint in_component_index;
layout(location = 7) flat in uint in_user_data;
layout(location = 8) flat in uint in_uv_set;
layout(location = 9) flat in uint in_texture_flags;
layout(location = 10) in vec3 in_world_position;
layout(location = 11) in vec3 in_normal_world;

layout(push_constant) uniform Surface3DPushConstants {
    mat4 view_projection;
    vec4 camera_position;
    uint params;
    uint ibl_specular_texture_slot;
    uint ibl_brdf_lut_texture_slot;
    uint ibl_sampler_slot;
} pc;

layout(set = 2, binding = 0, std140) uniform IblParamsBuffer {
    vec4 ibl_sh9[9];
    vec4 ibl_tint_intensity;
    vec4 ibl_rotation_max_lod_flags;
    uvec4 texture_sampler_slots;
} ibl_params;

layout(location = 0) out vec4 out_color;

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

MaterialSample decode_surface_material(vec4 sampled_color_,
                                       vec3 normal_world_) {
    MaterialSample material;
    material.base_color = sampled_color_.rgb;
    material.alpha = sampled_color_.a;
    material.metallic = 0.0;
    material.roughness = 0.55;
    material.normal_scale = 1.0;
    material.occlusion = 1.0;
    material.emissive = vec3(0.0);
    material.normal_world = normalize(normal_world_);
    return material;
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
    vec4 sampled_color = SampleTexture2D(in_texture_slot, in_sampler_slot, in_uv) * in_color;
    if (sampled_color.a <= 1e-5) {
        discard;
    }

    vec3 camera_position = pc.camera_position.xyz;
    vec3 view_dir = normalize(camera_position - in_world_position);
    MaterialSample material = decode_surface_material(sampled_color, in_normal_world);
    vec3 ibl_lighting = evaluate_surface_ibl(material, view_dir);
    vec3 shaded_rgb = material.base_color + ibl_lighting * 0.35 + material.emissive;
    out_color = vec4(shaded_rgb, material.alpha);
}

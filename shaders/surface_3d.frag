#version 460

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 2) flat in uint in_params;
layout(location = 3) flat in uint in_texture_id;
layout(location = 4) flat in uint in_sampler_id;
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
    uint reserved0;
    uint reserved1;
    uint reserved2;
} pc;

layout(set = 0, binding = 0) uniform sampler2D in_surface_texture;
layout(set = 1, binding = 0, std140) uniform IblParamsBuffer {
    vec4 ibl_sh9[9];
    vec4 ibl_tint_intensity;
    vec4 ibl_rotation_max_lod_flags;
} ibl_params;

layout(set = 1, binding = 1) uniform samplerCube ibl_specular_cube;
layout(set = 1, binding = 2) uniform sampler2D ibl_brdf_lut;
layout(set = 1, binding = 3) uniform samplerCube ibl_skybox_cube;

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

vec3 evaluate_surface_ibl(vec3 base_albedo_,
                          vec3 normal_world_,
                          vec3 view_dir_) {
    float ibl_intensity = max(ibl_params.ibl_tint_intensity.w, 0.0);
    if (ibl_intensity <= 1e-6) {
        return vec3(0.0);
    }

    vec3 tint = ibl_params.ibl_tint_intensity.rgb;
    float roughness = 0.55;
    float metallic = 0.0;
    vec3 diffuse_color = base_albedo_ * (1.0 - metallic);
    vec3 f0 = mix(vec3(0.04), base_albedo_, metallic);

    vec3 diffuse_irradiance = evaluate_sh9_irradiance(normal_world_);
    vec3 diffuse_ibl = diffuse_irradiance * diffuse_color;

    vec3 reflection_dir = rotate_environment_direction(reflect(-view_dir_, normalize(normal_world_)));
    float max_specular_lod = max(ibl_params.ibl_rotation_max_lod_flags.z, 0.0);
    vec3 prefiltered_specular = textureLod(ibl_specular_cube,
                                           reflection_dir,
                                           roughness * max_specular_lod).rgb;
    float n_dot_v = max(dot(normalize(normal_world_), normalize(view_dir_)), 0.0);
    vec2 brdf = texture(ibl_brdf_lut, vec2(n_dot_v, roughness)).rg;
    vec3 fresnel = f0 + (vec3(1.0) - f0) * pow(1.0 - n_dot_v, 5.0);
    vec3 specular_ibl = prefiltered_specular * (fresnel * brdf.x + brdf.y);

    return (diffuse_ibl + specular_ibl) * tint * ibl_intensity;
}

void main() {
    vec4 color = texture(in_surface_texture, in_uv) * in_color;
    if (color.a <= 1e-5) {
        discard;
    }

    vec3 camera_position = pc.camera_position.xyz;
    vec3 view_dir = normalize(camera_position - in_world_position);
    vec3 normal_world = normalize(in_normal_world);
    vec3 ibl_lighting = evaluate_surface_ibl(color.rgb, normal_world, view_dir);
    vec3 shaded_rgb = color.rgb + ibl_lighting * 0.35;
    out_color = vec4(shaded_rgb, color.a);
}

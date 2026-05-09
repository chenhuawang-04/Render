#version 460

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0, std140) uniform IblParamsBuffer {
    vec4 ibl_sh9[9];
    vec4 ibl_tint_intensity;
    vec4 ibl_rotation_max_lod_flags;
} ibl_params;

layout(set = 0, binding = 3) uniform samplerCube ibl_skybox_cube;

layout(push_constant) uniform EnvironmentImagePushConstants {
    vec4 camera_right_scale_x;
    vec4 camera_up_scale_y;
    vec4 camera_forward_reserved;
} pc;

vec3 rotate_environment_direction(vec3 direction_) {
    float sin_y = ibl_params.ibl_rotation_max_lod_flags.x;
    float cos_y = ibl_params.ibl_rotation_max_lod_flags.y;
    return vec3(
        cos_y * direction_.x + sin_y * direction_.z,
        direction_.y,
        -sin_y * direction_.x + cos_y * direction_.z);
}

void main() {
    vec2 ndc = vec2(in_uv.x * 2.0 - 1.0,
                    1.0 - in_uv.y * 2.0);
    vec3 ray_direction = normalize(pc.camera_forward_reserved.xyz +
                                   pc.camera_right_scale_x.xyz * (ndc.x * pc.camera_right_scale_x.w) +
                                   pc.camera_up_scale_y.xyz * (ndc.y * pc.camera_up_scale_y.w));
    ray_direction = rotate_environment_direction(ray_direction);

    float intensity = max(ibl_params.ibl_tint_intensity.w, 0.0);
    vec3 tint = ibl_params.ibl_tint_intensity.rgb;
    vec3 sky_color = texture(ibl_skybox_cube, ray_direction).rgb * tint * intensity;
    out_color = vec4(sky_color, 1.0);
}

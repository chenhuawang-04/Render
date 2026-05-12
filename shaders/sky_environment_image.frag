#version 460
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform textureCube g_TexturesCube[];
layout(set = 1, binding = 0) uniform sampler g_Samplers[];

layout(push_constant) uniform EnvironmentImagePushConstants {
    vec4 camera_right_scale_x;
    vec4 camera_up_scale_y;
    vec4 camera_forward_reserved;
    vec4 tint_intensity;
    vec4 rotation_sin_cos_reserved;
    uint texture_slot;
    uint sampler_slot;
    uint reserved0;
    uint reserved1;
} pc;

vec3 rotate_environment_direction(vec3 direction_) {
    float sin_y = pc.rotation_sin_cos_reserved.x;
    float cos_y = pc.rotation_sin_cos_reserved.y;
    return vec3(
        cos_y * direction_.x + sin_y * direction_.z,
        direction_.y,
        -sin_y * direction_.x + cos_y * direction_.z);
}

vec4 SampleTextureCube(uint texture_slot, uint sampler_slot, vec3 direction_) {
    return texture(
        samplerCube(
            g_TexturesCube[nonuniformEXT(texture_slot)],
            g_Samplers[nonuniformEXT(sampler_slot)]
        ),
        direction_);
}

void main() {
    vec2 ndc = vec2(in_uv.x * 2.0 - 1.0,
                    1.0 - in_uv.y * 2.0);
    vec3 ray_direction = normalize(pc.camera_forward_reserved.xyz +
                                   pc.camera_right_scale_x.xyz * (ndc.x * pc.camera_right_scale_x.w) +
                                   pc.camera_up_scale_y.xyz * (ndc.y * pc.camera_up_scale_y.w));
    ray_direction = rotate_environment_direction(ray_direction);

    float intensity = max(pc.tint_intensity.w, 0.0);
    vec3 tint = pc.tint_intensity.rgb;
    vec3 sky_color = SampleTextureCube(pc.texture_slot, pc.sampler_slot, ray_direction).rgb * tint * intensity;
    out_color = vec4(sky_color, 1.0);
}

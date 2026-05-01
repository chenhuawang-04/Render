#version 460

layout(set = 0, binding = 0) uniform sampler2D scene_sampler;
layout(set = 0, binding = 1) uniform sampler2D bloom_sampler;

layout(push_constant) uniform BloomCombinePushConstants {
    float exposure;
    float inv_gamma;
    float bloom_intensity;
    uint flags;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

vec3 reinhard_tonemap(vec3 value) {
    return value / (value + vec3(1.0));
}

void main() {
    vec4 scene_sample = texture(scene_sampler, in_uv);
    vec3 scene_color = scene_sample.rgb;
    vec3 bloom_color = texture(bloom_sampler, in_uv).rgb;

    const bool scene_is_srgb = (pc.flags & 0x4u) != 0u;
    if (scene_is_srgb) {
        scene_color = pow(max(scene_color, vec3(0.0)), vec3(2.2));
    }

    const bool bloom_is_srgb = (pc.flags & 0x8u) != 0u;
    if (bloom_is_srgb) {
        bloom_color = pow(max(bloom_color, vec3(0.0)), vec3(2.2));
    }

    vec3 color = scene_color + bloom_color * max(pc.bloom_intensity, 0.0);
    color *= max(pc.exposure, 0.0);

    const bool enable_tonemap = (pc.flags & 0x1u) != 0u;
    if (enable_tonemap) {
        color = reinhard_tonemap(color);
    }

    const bool apply_manual_gamma = (pc.flags & 0x2u) != 0u;
    if (apply_manual_gamma) {
        color = pow(max(color, vec3(0.0)), vec3(pc.inv_gamma));
    }

    out_color = vec4(color, scene_sample.a);
}

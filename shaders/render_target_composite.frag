#version 460

layout(set = 0, binding = 0) uniform sampler2D source_sampler;

layout(push_constant) uniform CompositePushConstants {
    float exposure;
    float inv_gamma;
    uint flags;
    uint reserved0;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

vec3 reinhard_tonemap(vec3 value) {
    return value / (value + vec3(1.0));
}

void main() {
    vec4 sampled = texture(source_sampler, in_uv);
    vec3 color = sampled.rgb;

    const bool source_is_srgb = (pc.flags & 0x4u) != 0u;
    if (source_is_srgb) {
        color = pow(max(color, vec3(0.0)), vec3(2.2));
    }

    color *= max(pc.exposure, 0.0);

    const bool enable_tonemap = (pc.flags & 0x1u) != 0u;
    if (enable_tonemap) {
        color = reinhard_tonemap(color);
    }

    const bool apply_manual_gamma = (pc.flags & 0x2u) != 0u;
    if (apply_manual_gamma) {
        color = pow(max(color, vec3(0.0)), vec3(pc.inv_gamma));
    }

    out_color = vec4(color, sampled.a);
}

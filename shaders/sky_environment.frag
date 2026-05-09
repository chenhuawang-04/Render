#version 460

layout(push_constant) uniform SkyEnvironmentPushConstants {
    vec4 color0;
    vec4 color1;
    float exposure;
    uint mode;
    float reserved0;
    float reserved1;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 color = pc.color0;
    if (pc.mode == 2u) {
        const float t = clamp(in_uv.y, 0.0, 1.0);
        color = mix(pc.color1, pc.color0, t);
    }
    color.rgb *= max(pc.exposure, 0.0);
    out_color = color;
}

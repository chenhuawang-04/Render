#version 460

layout(set = 0, binding = 0) uniform sampler2D source_sampler;

layout(push_constant) uniform BloomPrefilterPushConstants {
    float threshold;
    float knee;
    float reserved0;
    float reserved1;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec4 sampled = texture(source_sampler, in_uv);
    vec3 color = max(sampled.rgb, vec3(0.0));
    float brightness = max(max(color.r, color.g), color.b);
    float knee = max(pc.knee, 0.0001);
    float soft = clamp(brightness - pc.threshold + knee, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 0.0001);
    float hard = max(brightness - pc.threshold, 0.0);
    float contribution = max(hard, soft) / max(brightness, 0.0001);
    out_color = vec4(color * contribution, sampled.a * contribution);
}

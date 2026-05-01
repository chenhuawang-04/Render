#version 460

layout(set = 0, binding = 0) uniform sampler2D source_sampler;

layout(push_constant) uniform BloomBlurPushConstants {
    float texel_offset_x;
    float texel_offset_y;
    float filter_scale;
    float reserved0;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 delta = vec2(pc.texel_offset_x, pc.texel_offset_y) * max(pc.filter_scale, 0.0);

    vec4 result = texture(source_sampler, in_uv) * 0.2270270270;
    result += texture(source_sampler, in_uv + delta * 1.3846153846) * 0.3162162162;
    result += texture(source_sampler, in_uv - delta * 1.3846153846) * 0.3162162162;
    result += texture(source_sampler, in_uv + delta * 3.2307692308) * 0.0702702703;
    result += texture(source_sampler, in_uv - delta * 3.2307692308) * 0.0702702703;

    out_color = result;
}

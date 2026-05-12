#version 460
#extension GL_GOOGLE_include_directive : require
#include "vr/render/bindless.glsl"

layout(push_constant) uniform BloomBlurPushConstants {
    float texel_offset_x;
    float texel_offset_y;
    float filter_scale;
    uint texture_slot;
    uint sampler_slot;
    uint reserved0;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 delta = vec2(pc.texel_offset_x, pc.texel_offset_y) * max(pc.filter_scale, 0.0);

    vec4 result = SampleTexture2D(pc.texture_slot, pc.sampler_slot, in_uv) * 0.2270270270;
    result += SampleTexture2D(pc.texture_slot, pc.sampler_slot, in_uv + delta * 1.3846153846) * 0.3162162162;
    result += SampleTexture2D(pc.texture_slot, pc.sampler_slot, in_uv - delta * 1.3846153846) * 0.3162162162;
    result += SampleTexture2D(pc.texture_slot, pc.sampler_slot, in_uv + delta * 3.2307692308) * 0.0702702703;
    result += SampleTexture2D(pc.texture_slot, pc.sampler_slot, in_uv - delta * 3.2307692308) * 0.0702702703;

    out_color = result;
}

#version 460
#extension GL_GOOGLE_include_directive : require

#include "vr/render/bindless.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 2) flat in uint in_texture_slot;
layout(location = 3) flat in uint in_sampler_slot;

layout(location = 0) out vec4 out_color;

void main() {
    const vec4 sampled = SampleTexture2D(in_texture_slot, in_sampler_slot, in_uv);
    const vec4 shaded = sampled * in_color;
    if (shaded.a <= 1e-5) {
        discard;
    }
    out_color = shaded;
}

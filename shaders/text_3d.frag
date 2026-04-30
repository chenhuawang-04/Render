#version 460
#extension GL_GOOGLE_include_directive : require
#include "vr/text/text_shading.glsl"

layout(set = 0, binding = 0) uniform sampler2D atlas_sampler;

layout(push_constant) uniform Text3DPushConstants {
    mat4 view_projection;
    float sdf_smooth;
    float bitmap_gamma;
    float bitmap_edge_sharpness;
    float reserved0;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec4 in_outline_color;
layout(location = 3) flat in uint in_params;

layout(location = 0) out vec4 out_color;

void main() {
    float glyph_sample = texture(atlas_sampler, in_uv).r;
    vec4 shaded = vr_evaluate_text_fragment(glyph_sample,
                                            in_color,
                                            in_outline_color,
                                            in_params,
                                            pc.sdf_smooth,
                                            pc.bitmap_gamma,
                                            pc.bitmap_edge_sharpness);
    if (shaded.a <= 1e-5) {
        discard;
    }

    out_color = shaded;
}


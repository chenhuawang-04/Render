#version 460

layout(set = 0, binding = 0) uniform sampler2D atlas_sampler;

layout(push_constant) uniform TextPushConstants {
    vec2 inv_viewport;
    float depth;
    float sdf_smooth;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec4 in_outline_color;
layout(location = 3) flat in uint in_params;

layout(location = 0) out vec4 out_color;

void main() {
    const bool sdf_enabled = (in_params & 0x1u) != 0u;
    const bool outline_enabled = (in_params & 0x2u) != 0u;
    const float outline_width_px = float((in_params >> 8u) & 0xFFu);

    const float glyph_sample = texture(atlas_sampler, in_uv).r;

    vec3 rgb = in_color.rgb;
    float alpha = glyph_sample * in_color.a;

    if (sdf_enabled) {
        const float smooth_base = max(fwidth(glyph_sample), 1e-5);
        const float smoothing = max(smooth_base * max(pc.sdf_smooth, 0.25), 1e-5);
        const float fill_coverage = smoothstep(0.5 - smoothing, 0.5 + smoothing, glyph_sample);

        rgb = in_color.rgb;
        alpha = fill_coverage * in_color.a;

        if (outline_enabled && outline_width_px > 0.0) {
            const float outline_range = outline_width_px * smoothing;
            const float outline_coverage = smoothstep((0.5 - outline_range) - smoothing,
                                                      (0.5 - outline_range) + smoothing,
                                                      glyph_sample);
            const float ring = clamp(outline_coverage - fill_coverage, 0.0, 1.0);
            const float fill_mix = clamp(fill_coverage, 0.0, 1.0);
            rgb = mix(in_outline_color.rgb, in_color.rgb, fill_mix);
            alpha = max(fill_coverage * in_color.a, ring * in_outline_color.a);
        }
    }

    if (alpha <= 1e-5) {
        discard;
    }

    out_color = vec4(rgb, alpha);
}

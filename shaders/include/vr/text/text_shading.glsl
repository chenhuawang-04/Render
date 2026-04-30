#ifndef VR_TEXT_TEXT_SHADING_GLSL
#define VR_TEXT_TEXT_SHADING_GLSL

vec4 vr_evaluate_text_fragment(float glyph_sample_,
                               vec4 in_color_,
                               vec4 in_outline_color_,
                               uint in_params_,
                               float sdf_smooth_,
                               float bitmap_gamma_,
                               float bitmap_edge_sharpness_) {
    const bool sdf_enabled = (in_params_ & 0x1u) != 0u;
    const bool outline_enabled = (in_params_ & 0x2u) != 0u;
    const float outline_width_px = float((in_params_ >> 8u) & 0xFFu);

    vec3 rgb = in_color_.rgb;
    const float edge_sharpness = max(bitmap_edge_sharpness_, 0.01);
    const float bitmap_gamma = max(bitmap_gamma_, 0.01);
    float bitmap_alpha = glyph_sample_;
    if (abs(edge_sharpness - 1.0) > 1e-4) {
        bitmap_alpha = clamp((bitmap_alpha - 0.5) * edge_sharpness + 0.5, 0.0, 1.0);
    } else {
        bitmap_alpha = clamp(bitmap_alpha, 0.0, 1.0);
    }
    if (abs(bitmap_gamma - 1.0) > 1e-4) {
        bitmap_alpha = pow(bitmap_alpha, bitmap_gamma);
    }
    float alpha = bitmap_alpha * in_color_.a;

    if (sdf_enabled) {
        const float smooth_base = max(fwidth(glyph_sample_), 1e-5);
        const float smoothing = max(smooth_base * max(sdf_smooth_, 0.25), 1e-5);
        const float fill_coverage = smoothstep(0.5 - smoothing, 0.5 + smoothing, glyph_sample_);

        rgb = in_color_.rgb;
        alpha = fill_coverage * in_color_.a;

        if (outline_enabled && outline_width_px > 0.0) {
            const float outline_range = outline_width_px * smoothing;
            const float outline_coverage = smoothstep((0.5 - outline_range) - smoothing,
                                                      (0.5 - outline_range) + smoothing,
                                                      glyph_sample_);
            const float ring = clamp(outline_coverage - fill_coverage, 0.0, 1.0);
            const float fill_mix = clamp(fill_coverage, 0.0, 1.0);
            rgb = mix(in_outline_color_.rgb, in_color_.rgb, fill_mix);
            alpha = max(fill_coverage * in_color_.a, ring * in_outline_color_.a);
        }
    }

    return vec4(rgb, alpha);
}

#endif // VR_TEXT_TEXT_SHADING_GLSL


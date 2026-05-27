#version 460
#extension GL_GOOGLE_include_directive : require
#include "vr/render/bindless.glsl"

layout(push_constant) uniform TemporalResolvePushConstants {
    float current_weight;
    float previous_weight;
    float motion_rejection_begin_pixels;
    float motion_rejection_end_pixels;
    float depth_rejection_begin;
    float depth_rejection_end;
    uint flags;
    uint current_texture_slot;
    uint previous_texture_slot;
    uint previous_depth_texture_slot;
    uint motion_texture_slot;
    uint sampler_slot;
    uint target_width;
    uint target_height;
    uint reserved0;
    uint reserved1;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

vec3 DecodeColor(vec3 color_, bool srgb_) {
    if (!srgb_) {
        return color_;
    }
    return pow(max(color_, vec3(0.0)), vec3(2.2));
}

float Luma(vec3 color_) {
    return dot(color_, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec4 current_sample =
        SampleTexture2D(pc.current_texture_slot, pc.sampler_slot, in_uv);

    const bool current_is_srgb = (pc.flags & 0x1u) != 0u;
    const bool previous_is_srgb = (pc.flags & 0x2u) != 0u;
    vec3 current_color = DecodeColor(current_sample.rgb, current_is_srgb);

    vec2 previous_uv = in_uv;
    float history_confidence = 0.0;
    if ((pc.flags & 0x4u) != 0u) {
        vec4 motion_sample =
            SampleTexture2D(pc.motion_texture_slot, pc.sampler_slot, in_uv);
        previous_uv = in_uv - motion_sample.xy;
        bool in_bounds =
            previous_uv.x >= 0.0 && previous_uv.x <= 1.0 &&
            previous_uv.y >= 0.0 && previous_uv.y <= 1.0;
        history_confidence = (motion_sample.w > 0.0 && in_bounds)
            ? clamp(motion_sample.w, 0.0, 1.0)
            : 0.0;

        if (history_confidence > 0.0) {
            const vec2 motion_pixels_uv =
                motion_sample.xy * vec2(float(pc.target_width), float(pc.target_height));
            const float motion_pixels = length(motion_pixels_uv);
            const float rejection =
                smoothstep(pc.motion_rejection_begin_pixels,
                           pc.motion_rejection_end_pixels,
                           motion_pixels);
            history_confidence *= (1.0 - rejection);

            const float previous_depth_sample =
                SampleTexture2D(pc.previous_depth_texture_slot,
                                pc.sampler_slot,
                                previous_uv).r;
            const float depth_delta =
                abs(motion_sample.z - previous_depth_sample);
            const float depth_confidence =
                1.0 - smoothstep(pc.depth_rejection_begin,
                                 pc.depth_rejection_end,
                                 depth_delta);
            history_confidence *= depth_confidence;
        }
    }

    vec3 previous_color = current_color;
    if (history_confidence > 0.0) {
        const vec4 previous_sample =
            SampleTexture2D(pc.previous_texture_slot, pc.sampler_slot, previous_uv);
        previous_color = DecodeColor(previous_sample.rgb, previous_is_srgb);

        const vec2 texel = vec2(1.0 / float(max(pc.target_width, 1u)),
                                1.0 / float(max(pc.target_height, 1u)));
        vec3 current_min = current_color;
        vec3 current_max = current_color;
        vec3 neighborhood_sum = vec3(0.0);
        vec3 neighborhood_sum_sq = vec3(0.0);
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                const vec2 sample_uv =
                    clamp(in_uv + vec2(float(x), float(y)) * texel,
                          vec2(0.0),
                          vec2(1.0));
                const vec3 sample_color =
                    DecodeColor(SampleTexture2D(pc.current_texture_slot,
                                                pc.sampler_slot,
                                                sample_uv).rgb,
                                current_is_srgb);
                current_min = min(current_min, sample_color);
                current_max = max(current_max, sample_color);
                neighborhood_sum += sample_color;
                neighborhood_sum_sq += sample_color * sample_color;
            }
        }

        const float luma_delta = abs(Luma(current_color) - Luma(previous_color));
        const float luma_confidence =
            1.0 - smoothstep(0.02, 0.20, luma_delta);
        history_confidence *= luma_confidence;

        const vec3 neighborhood_mean = neighborhood_sum / 9.0;
        const vec3 neighborhood_variance =
            max(neighborhood_sum_sq / 9.0 - neighborhood_mean * neighborhood_mean,
                vec3(0.0));
        const vec3 neighborhood_sigma = sqrt(neighborhood_variance);
        const vec3 sigma_clip = max(neighborhood_sigma * 1.25, vec3(0.002));
        const vec3 clamp_expand =
            max((current_max - current_min) * 0.05, vec3(0.001));
        vec3 clip_min =
            max(current_min - clamp_expand,
                neighborhood_mean - sigma_clip);
        vec3 clip_max =
            min(current_max + clamp_expand,
                neighborhood_mean + sigma_clip);
        const bvec3 invalid_clip_range = greaterThan(clip_min, clip_max);
        clip_min = mix(clip_min,
                       current_min - clamp_expand,
                       invalid_clip_range);
        clip_max = mix(clip_max,
                       current_max + clamp_expand,
                       invalid_clip_range);
        previous_color = clamp(previous_color,
                               clip_min,
                               clip_max);
    }

    float previous_weight = max(pc.previous_weight, 0.0) * history_confidence;
    float current_weight = max(pc.current_weight, 0.0);
    float weight_sum = current_weight + previous_weight;
    if (weight_sum > 1.0e-6) {
        current_weight /= weight_sum;
        previous_weight /= weight_sum;
    } else {
        current_weight = 1.0;
        previous_weight = 0.0;
    }

    vec3 resolved =
        current_color * current_weight +
        previous_color * previous_weight;
    out_color = vec4(resolved, current_sample.a);
}

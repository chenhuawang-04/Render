#version 460
#extension GL_GOOGLE_include_directive : require
#include "vr/render/bindless.glsl"

layout(push_constant) uniform TemporalMotionPushConstants {
    mat4 current_clip_to_previous_clip;
    float current_jitter_uv_x;
    float current_jitter_uv_y;
    float previous_jitter_uv_x;
    float previous_jitter_uv_y;
    uint flags;
    uint depth_texture_slot;
    uint sampler_slot;
    uint reserved0;
} pc;

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_motion;

void main() {
    if ((pc.flags & 0x1u) == 0u) {
        out_motion = vec4(0.0);
        return;
    }

    const float depth =
        SampleTexture2D(pc.depth_texture_slot, pc.sampler_slot, in_uv).r;
    const vec4 current_clip = vec4(in_uv * 2.0 - 1.0, depth, 1.0);
    const vec4 previous_clip =
        pc.current_clip_to_previous_clip * current_clip;

    if (abs(previous_clip.w) <= 1.0e-6) {
        out_motion = vec4(0.0);
        return;
    }

    const vec3 previous_ndc = previous_clip.xyz / previous_clip.w;
    const vec2 jitter_delta = vec2(pc.previous_jitter_uv_x - pc.current_jitter_uv_x,
                                   pc.previous_jitter_uv_y - pc.current_jitter_uv_y);
    const vec2 previous_uv = previous_ndc.xy * 0.5 + 0.5 + jitter_delta;
    const bool in_bounds =
        previous_uv.x >= 0.0 && previous_uv.x <= 1.0 &&
        previous_uv.y >= 0.0 && previous_uv.y <= 1.0 &&
        previous_ndc.z >= 0.0 && previous_ndc.z <= 1.0;

    const vec2 motion_uv = in_uv - previous_uv;
    out_motion = vec4(motion_uv,
                      previous_ndc.z,
                      in_bounds ? 1.0 : 0.0);
}

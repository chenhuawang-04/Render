#version 460

layout(location = 0) in vec4 in_current_clip;
layout(location = 1) in vec4 in_previous_clip;
layout(location = 2) flat in float in_confidence_seed;

layout(location = 0) out vec4 out_motion;

void main() {
    if (in_confidence_seed <= 0.0) {
        discard;
    }

    const float current_w = abs(in_current_clip.w);
    const float previous_w = abs(in_previous_clip.w);
    if (current_w <= 1.0e-6 || previous_w <= 1.0e-6) {
        discard;
    }

    const vec3 current_ndc = in_current_clip.xyz / in_current_clip.w;
    const vec3 previous_ndc = in_previous_clip.xyz / in_previous_clip.w;
    const vec2 current_uv = current_ndc.xy * 0.5 + 0.5;
    const vec2 previous_uv = previous_ndc.xy * 0.5 + 0.5;
    const bool previous_in_bounds =
        previous_uv.x >= 0.0 && previous_uv.x <= 1.0 &&
        previous_uv.y >= 0.0 && previous_uv.y <= 1.0 &&
        previous_ndc.z >= 0.0 && previous_ndc.z <= 1.0;
    if (!previous_in_bounds) {
        discard;
    }

    out_motion = vec4(current_uv - previous_uv,
                      previous_ndc.z,
                      clamp(in_confidence_seed, 0.0, 1.0));
}

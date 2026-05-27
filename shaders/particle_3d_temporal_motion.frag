#version 460

layout(location = 0) flat in vec2 in_local_motion_uv;
layout(location = 1) flat in float in_confidence_seed;

layout(location = 0) out vec4 out_motion;

void main() {
    out_motion = vec4(in_local_motion_uv,
                      0.0,
                      clamp(in_confidence_seed, 0.0, 1.0));
}

#version 460

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_size;
layout(location = 2) in float in_rotation_radians;
layout(location = 3) in vec4 in_color;
layout(location = 4) in uint in_texture_slot;
layout(location = 5) in uint in_sampler_slot;

layout(push_constant) uniform Particle2DPushConstants {
    vec4 viewport;
    uint params;
    uint reserved0;
    uint reserved1;
    uint reserved2;
} pc;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;
layout(location = 2) flat out uint out_texture_slot;
layout(location = 3) flat out uint out_sampler_slot;

vec2 corner01_for_vertex(uint vertex_index) {
    switch (vertex_index) {
    case 0u: return vec2(0.0, 0.0);
    case 1u: return vec2(1.0, 0.0);
    case 2u: return vec2(1.0, 1.0);
    case 3u: return vec2(1.0, 1.0);
    case 4u: return vec2(0.0, 1.0);
    default: return vec2(0.0, 0.0);
    }
}

vec2 to_ndc(vec2 position) {
    const bool pixel_space = (pc.params & 0x1u) != 0u;
    const bool y_down = (pc.params & 0x2u) != 0u;
    if (!pixel_space) {
        return position;
    }

    vec2 ndc;
    ndc.x = position.x * pc.viewport.z - 1.0;
    ndc.y = position.y * pc.viewport.w;
    ndc.y = y_down ? (1.0 - ndc.y) : (ndc.y - 1.0);
    return ndc;
}

void main() {
    const vec2 corner01 = corner01_for_vertex(uint(gl_VertexIndex));
    const vec2 local = (corner01 - vec2(0.5, 0.5)) * in_size;
    const float s = sin(in_rotation_radians);
    const float c = cos(in_rotation_radians);
    const vec2 rotated = vec2(c * local.x - s * local.y,
                              s * local.x + c * local.y);
    const vec2 world = in_position + rotated;

    gl_Position = vec4(to_ndc(world), 0.0, 1.0);
    out_uv = corner01;
    out_color = in_color;
    out_texture_slot = in_texture_slot;
    out_sampler_slot = in_sampler_slot;
}

#version 460

layout(location = 0) in vec3 in_world_row0;
layout(location = 1) in vec3 in_world_row1;
layout(location = 2) in vec2 in_size;
layout(location = 3) in vec2 in_pivot;
layout(location = 4) in vec4 in_uv_rect;
layout(location = 5) in float in_opacity;
layout(location = 6) in vec4 in_tint_color;
layout(location = 7) in uint in_params;
layout(location = 8) in uint in_image_slot;
layout(location = 9) in uint in_sampler_slot;

layout(push_constant) uniform Surface2DPushConstants {
    vec4 viewport;
    uint params;
    uint reserved0;
    uint reserved1;
    uint reserved2;
} pc;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;
layout(location = 2) flat out uint out_params;
layout(location = 3) flat out uint out_image_slot;
layout(location = 4) flat out uint out_sampler_slot;
layout(location = 5) out vec2 out_world_position;

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
    vec2 corner01 = corner01_for_vertex(uint(gl_VertexIndex));

    const bool flip_x = (in_params & 0x4u) != 0u;
    const bool flip_y = (in_params & 0x8u) != 0u;
    if (flip_x) {
        corner01.x = 1.0 - corner01.x;
    }
    if (flip_y) {
        corner01.y = 1.0 - corner01.y;
    }

    vec2 local = (corner01 - in_pivot) * in_size;
    vec2 world;
    world.x = dot(in_world_row0.xy, local) + in_world_row0.z;
    world.y = dot(in_world_row1.xy, local) + in_world_row1.z;

    gl_Position = vec4(to_ndc(world), 0.0, 1.0);
    out_uv = mix(in_uv_rect.xy, in_uv_rect.zw, corner01);
    out_color = vec4(in_tint_color.rgb, in_tint_color.a * clamp(in_opacity, 0.0, 1.0));
    out_params = in_params;
    out_image_slot = in_image_slot;
    out_sampler_slot = in_sampler_slot;
    out_world_position = world;
}

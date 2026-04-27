#version 460

layout(location = 0) in vec3 in_world_row0;
layout(location = 1) in vec3 in_world_row1;
layout(location = 2) in vec2 in_size;
layout(location = 3) in vec2 in_pivot;
layout(location = 4) in vec4 in_uv_rect;
layout(location = 5) in float in_opacity;
layout(location = 6) in uint in_tint_rgba8;
layout(location = 7) in uint in_params;
layout(location = 8) in uint in_surface_id;
layout(location = 9) in uint in_material_id;
layout(location = 10) in uint in_atlas_page_id;
layout(location = 11) in uint in_component_index;
layout(location = 12) in uint in_user_data;
layout(location = 13) in uint in_source_kind;

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
layout(location = 3) flat out uint out_surface_id;
layout(location = 4) flat out uint out_material_id;
layout(location = 5) flat out uint out_atlas_page_id;
layout(location = 6) flat out uint out_component_index;
layout(location = 7) flat out uint out_user_data;
layout(location = 8) flat out uint out_source_kind;

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

vec4 unpack_rgba8(uint packed) {
    vec4 color;
    color.r = float((packed >> 0u) & 0xFFu);
    color.g = float((packed >> 8u) & 0xFFu);
    color.b = float((packed >> 16u) & 0xFFu);
    color.a = float((packed >> 24u) & 0xFFu);
    return color / 255.0;
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
    vec2 ndc = to_ndc(world);

    gl_Position = vec4(ndc, 0.0, 1.0);
    out_uv = mix(in_uv_rect.xy, in_uv_rect.zw, corner01);

    vec4 tint = unpack_rgba8(in_tint_rgba8);
    out_color = vec4(tint.rgb, tint.a * clamp(in_opacity, 0.0, 1.0));
    out_params = in_params;
    out_surface_id = in_surface_id;
    out_material_id = in_material_id;
    out_atlas_page_id = in_atlas_page_id;
    out_component_index = in_component_index;
    out_user_data = in_user_data;
    out_source_kind = in_source_kind;
}


#version 460

layout(location = 0) in vec4 in_world_row0;
layout(location = 1) in vec4 in_world_row1;
layout(location = 2) in vec4 in_world_row2;
layout(location = 3) in vec4 in_world_row3;
layout(location = 4) in vec4 in_uv_transform;
layout(location = 5) in float in_opacity;
layout(location = 6) in uint in_tint_rgba8;
layout(location = 7) in uint in_params;
layout(location = 8) in uint in_texture_id;
layout(location = 9) in uint in_sampler_id;
layout(location = 10) in uint in_material_id;
layout(location = 11) in uint in_component_index;
layout(location = 12) in uint in_user_data;
layout(location = 13) in uint in_uv_set;
layout(location = 14) in uint in_texture_flags;

layout(push_constant) uniform Surface3DPushConstants {
    mat4 view_projection;
    vec4 camera_position;
    uint params;
    uint reserved0;
    uint reserved1;
    uint reserved2;
} pc;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;
layout(location = 2) flat out uint out_params;
layout(location = 3) flat out uint out_texture_id;
layout(location = 4) flat out uint out_sampler_id;
layout(location = 5) flat out uint out_material_id;
layout(location = 6) flat out uint out_component_index;
layout(location = 7) flat out uint out_user_data;
layout(location = 8) flat out uint out_uv_set;
layout(location = 9) flat out uint out_texture_flags;
layout(location = 10) out vec3 out_world_position;
layout(location = 11) out vec3 out_normal_world;

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
    vec2 local = corner01 - vec2(0.5, 0.5);
    vec2 uv = corner01 * in_uv_transform.xy + in_uv_transform.zw;

    mat4 world = mat4(in_world_row0, in_world_row1, in_world_row2, in_world_row3);
    vec4 world_position = world * vec4(local, 0.0, 1.0);
    gl_Position = pc.view_projection * world_position;
    vec3 normal_world = mat3(world) * vec3(0.0, 0.0, 1.0);

    vec4 tint = unpack_rgba8(in_tint_rgba8);
    out_uv = uv;
    out_color = vec4(tint.rgb, tint.a * clamp(in_opacity, 0.0, 1.0));
    out_params = in_params;
    out_texture_id = in_texture_id;
    out_sampler_id = in_sampler_id;
    out_material_id = in_material_id;
    out_component_index = in_component_index;
    out_user_data = in_user_data;
    out_uv_set = in_uv_set;
    out_texture_flags = in_texture_flags;
    out_world_position = world_position.xyz;
    out_normal_world = normal_world;
}

#version 460

layout(location = 0) in vec4 in_world_row0;
layout(location = 1) in vec4 in_world_row1;
layout(location = 2) in vec4 in_world_row2;
layout(location = 3) in vec4 in_world_row3;
layout(location = 4) in vec4 in_uv_transform;
layout(location = 5) in uint in_appearance_record_index;

layout(push_constant) uniform Surface3DPushConstants {
    mat4 view_projection;
    vec4 camera_position;
    uint params;
    uint reserved0;
    uint reserved1;
    uint reserved2;
} pc;

layout(location = 0) out vec2 out_uv;
layout(location = 1) flat out uint out_appearance_record_index;
layout(location = 2) out vec3 out_world_position;
layout(location = 3) out vec3 out_normal_world;
layout(location = 4) out vec3 out_tangent_world;
layout(location = 5) out vec3 out_bitangent_world;

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

void main() {
    vec2 corner01 = corner01_for_vertex(uint(gl_VertexIndex));
    vec2 local = corner01 - vec2(0.5, 0.5);
    vec2 uv = corner01 * in_uv_transform.xy + in_uv_transform.zw;

    mat4 world = mat4(in_world_row0, in_world_row1, in_world_row2, in_world_row3);
    vec4 world_position = world * vec4(local, 0.0, 1.0);
    gl_Position = pc.view_projection * world_position;

    mat3 world3x3 = mat3(world);
    vec3 normal_world = world3x3 * vec3(0.0, 0.0, 1.0);
    vec3 tangent_world = world3x3 * vec3(1.0, 0.0, 0.0);
    vec3 bitangent_world = world3x3 * vec3(0.0, 1.0, 0.0);

    out_uv = uv;
    out_appearance_record_index = in_appearance_record_index;
    out_world_position = world_position.xyz;
    out_normal_world = normalize(normal_world);
    out_tangent_world = normalize(tangent_world);
    out_bitangent_world = normalize(bitangent_world);
}

#version 460

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 3) in vec4 in_world_row0;
layout(location = 4) in vec4 in_world_row1;
layout(location = 5) in vec4 in_world_row2;
layout(location = 6) in vec4 in_world_row3;
layout(location = 7) in vec4 in_material_params;
layout(location = 8) in vec4 in_albedo;
layout(location = 9) in uint in_instance_params;

layout(push_constant) uniform Geometry3DPushConstants {
    mat4 view_projection;
    vec4 light_direction_intensity;
} pc;

layout(location = 0) out vec3 out_normal_world;
layout(location = 1) out vec4 out_albedo;
layout(location = 2) out vec4 out_material_params;
layout(location = 3) flat out uint out_instance_params;
layout(location = 4) out vec2 out_uv;

void main() {
    mat4 world = mat4(in_world_row0, in_world_row1, in_world_row2, in_world_row3);
    vec4 world_position = world * vec4(in_position, 1.0);
    gl_Position = pc.view_projection * world_position;

    mat3 world3x3 = mat3(world);
    vec3 normal_world = world3x3 * in_normal;
    float normal_length = length(normal_world);
    out_normal_world = (normal_length > 1e-6) ? (normal_world / normal_length) : vec3(0.0, 0.0, 1.0);
    out_albedo = in_albedo;
    out_material_params = in_material_params;
    out_instance_params = in_instance_params;
    out_uv = in_uv;
}


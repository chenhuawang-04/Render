#version 460

layout(location = 0) in vec3 in_position;

layout(push_constant) uniform ShadowPushConstants {
    mat4 view_projection;
    mat4 world;
} pc;

void main() {
    vec4 world_position = pc.world * vec4(in_position, 1.0);
    gl_Position = pc.view_projection * world_position;
    gl_PointSize = 1.0;
}

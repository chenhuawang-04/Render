#version 460

layout(push_constant) uniform Shadow2DPushConstants {
    mat4 view_projection;
    vec4 rect_min_max;
} pc;

vec2 corner_for_vertex(uint vertex_index_) {
    switch (vertex_index_) {
    case 0u: return vec2(0.0, 0.0);
    case 1u: return vec2(1.0, 0.0);
    case 2u: return vec2(1.0, 1.0);
    case 3u: return vec2(1.0, 1.0);
    case 4u: return vec2(0.0, 1.0);
    default: return vec2(0.0, 0.0);
    }
}

void main() {
    vec2 corner = corner_for_vertex(uint(gl_VertexIndex));
    vec2 world_xy = mix(pc.rect_min_max.xy, pc.rect_min_max.zw, corner);
    gl_Position = pc.view_projection * vec4(world_xy, 0.0, 1.0);
}


#version 460

layout(location = 0) in vec4 in_rect;
layout(location = 1) in vec4 in_uv_rect;
layout(location = 2) in vec3 in_origin_world;
layout(location = 3) in vec3 in_basis_right_world;
layout(location = 4) in vec3 in_basis_up_world;
layout(location = 5) in vec4 in_color;
layout(location = 6) in vec4 in_outline_color;
layout(location = 7) in uint in_params;

layout(push_constant) uniform Text3DPushConstants {
    mat4 view_projection;
    float sdf_smooth;
    float bitmap_gamma;
    float bitmap_edge_sharpness;
    float reserved0;
} pc;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;
layout(location = 2) out vec4 out_outline_color;
layout(location = 3) flat out uint out_params;

void main() {
    const vec2 corner = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    const vec2 local = mix(in_rect.xy, in_rect.zw, corner);
    const vec2 uv = mix(in_uv_rect.xy, in_uv_rect.zw, corner);

    const vec3 world = in_origin_world +
                       in_basis_right_world * local.x +
                       in_basis_up_world * local.y;

    gl_Position = pc.view_projection * vec4(world, 1.0);
    out_uv = uv;
    out_color = in_color;
    out_outline_color = in_outline_color;
    out_params = in_params;
}

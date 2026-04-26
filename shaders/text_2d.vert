#version 460

layout(location = 0) in vec4 in_rect;
layout(location = 1) in vec4 in_uv_rect;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec4 in_outline_color;
layout(location = 4) in uint in_params;

layout(push_constant) uniform TextPushConstants {
    vec2 inv_viewport;
    float depth;
    float sdf_smooth;
    float bitmap_gamma;
    float bitmap_edge_sharpness;
} pc;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;
layout(location = 2) out vec4 out_outline_color;
layout(location = 3) flat out uint out_params;

void main() {
    const vec2 corner = vec2(float(gl_VertexIndex & 1), float((gl_VertexIndex >> 1) & 1));
    const vec2 pos_px = mix(in_rect.xy, in_rect.zw, corner);
    const vec2 uv = mix(in_uv_rect.xy, in_uv_rect.zw, corner);

    // Vulkan 视口在正高度下采用 framebuffer Y 向下约定：
    // NDC.y = -1 对应顶部，+1 对应底部。
    // 运行时文本坐标使用左上角为原点（像素坐标，Y 向下），
    // 因此这里应直接线性映射到 [-1, +1]，不能再做 OpenGL 风格的 Y 翻转。
    const vec2 ndc = vec2(pos_px.x * pc.inv_viewport.x * 2.0 - 1.0,
                          pos_px.y * pc.inv_viewport.y * 2.0 - 1.0);

    gl_Position = vec4(ndc, pc.depth, 1.0);
    out_uv = uv;
    out_color = in_color;
    out_outline_color = in_outline_color;
    out_params = in_params;
}

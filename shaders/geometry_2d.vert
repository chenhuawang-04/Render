#version 460

layout(location = 0) in vec4 in_segment;
layout(location = 1) in uint in_fill_color;
layout(location = 2) in uint in_stroke_color;
layout(location = 3) in float in_stroke_width;
layout(location = 4) in uint in_params;
layout(location = 5) in uint in_component_index;
layout(location = 6) in uint in_user_data;

layout(push_constant) uniform Geometry2DPushConstants {
    vec4 viewport;
    uint params;
    uint reserved0;
    uint reserved1;
    uint reserved2;
} pc;

layout(location = 0) out vec4 out_color;
layout(location = 1) flat out uint out_params;
layout(location = 2) flat out uint out_component_index;
layout(location = 3) flat out uint out_user_data;

vec2 corner_for_vertex(uint vertex_index) {
    // triangles: 0,1,2 and 2,3,0
    switch (vertex_index) {
    case 0u: return vec2(-1.0, -1.0);
    case 1u: return vec2(1.0, -1.0);
    case 2u: return vec2(1.0, 1.0);
    case 3u: return vec2(1.0, 1.0);
    case 4u: return vec2(-1.0, 1.0);
    default: return vec2(-1.0, -1.0);
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

vec2 convert_to_ndc(vec2 position) {
    bool pixel_space = (pc.params & 0x1u) != 0u;
    bool y_down = (pc.params & 0x2u) != 0u;
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
    vec2 p0 = convert_to_ndc(in_segment.xy);
    vec2 p1 = convert_to_ndc(in_segment.zw);
    vec2 delta = p1 - p0;

    float len2 = dot(delta, delta);
    vec2 dir = (len2 > 1e-12) ? normalize(delta) : vec2(1.0, 0.0);
    vec2 normal = vec2(-dir.y, dir.x);

    vec2 corner = corner_for_vertex(uint(gl_VertexIndex));
    float edge_t = corner.x * 0.5 + 0.5;
    float half_width = max(in_stroke_width, 1.0) * 0.5;

    bool pixel_space = (pc.params & 0x1u) != 0u;
    vec2 width_scale = pixel_space ? pc.viewport.zw : vec2(1.0, 1.0);
    vec2 position = mix(p0, p1, edge_t) + normal * corner.y * half_width * width_scale;
    gl_Position = vec4(position, 0.0, 1.0);

    uint topology = (in_params >> 1u) & 0x3u;
    uint packed_color = (topology == 0u) ? in_fill_color : in_stroke_color;
    out_color = unpack_rgba8(packed_color);
    out_params = in_params;
    out_component_index = in_component_index;
    out_user_data = in_user_data;
}


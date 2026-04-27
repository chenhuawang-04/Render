#version 460

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 2) flat in uint in_params;
layout(location = 3) flat in uint in_texture_id;
layout(location = 4) flat in uint in_sampler_id;
layout(location = 5) flat in uint in_material_id;
layout(location = 6) flat in uint in_component_index;
layout(location = 7) flat in uint in_user_data;
layout(location = 8) flat in uint in_uv_set;
layout(location = 9) flat in uint in_texture_flags;

layout(set = 0, binding = 0) uniform sampler2D in_surface_texture;

layout(location = 0) out vec4 out_color;

void main() {
    vec4 color = texture(in_surface_texture, in_uv) * in_color;
    if (color.a <= 1e-5) {
        discard;
    }
    out_color = color;
}

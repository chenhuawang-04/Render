#version 460

layout(location = 0) in vec4 in_color;
layout(location = 1) flat in uint in_params;
layout(location = 2) flat in uint in_component_index;
layout(location = 3) flat in uint in_user_data;

layout(location = 0) out vec4 out_color;

void main() {
    out_color = in_color;
}


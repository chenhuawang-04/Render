#version 460

layout(set = 0, binding = 0) uniform sampler2D in_particle_texture;

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec4 out_color;

void main() {
    const vec4 sampled = texture(in_particle_texture, in_uv);
    const vec4 shaded = sampled * in_color;
    if (shaded.a <= 1e-5) {
        discard;
    }
    out_color = shaded;
}

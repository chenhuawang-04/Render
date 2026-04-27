#version 460

layout(push_constant) uniform Geometry3DPushConstants {
    mat4 view_projection;
    vec4 light_direction_intensity;
} pc;

layout(location = 0) in vec3 in_normal_world;
layout(location = 1) in vec4 in_albedo;
layout(location = 2) in vec4 in_material_params;
layout(location = 3) flat in uint in_instance_params;
layout(location = 4) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal_world = normalize(in_normal_world);
    vec3 light_dir = normalize(pc.light_direction_intensity.xyz);
    float light_intensity = max(pc.light_direction_intensity.w, 0.0);
    float n_dot_l = max(dot(normal_world, -light_dir), 0.0);

    bool unlit = ((in_instance_params >> 7u) & 0x1u) != 0u;
    float shading = unlit ? 1.0 : (0.2 + n_dot_l * light_intensity);

    vec3 lit_color = in_albedo.rgb * shading;
    out_color = vec4(lit_color, in_albedo.a);
}

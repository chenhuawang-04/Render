#version 460

layout(push_constant) uniform Geometry3DPushConstants {
    mat4 view_projection;
    vec4 light_direction_intensity;
    vec4 material_uv_transform;
    uint material_flags;
    float alpha_cutoff;
    vec2 material_reserved;
} pc;

layout(location = 0) in vec3 in_normal_world;
layout(location = 1) in vec4 in_albedo;
layout(location = 2) in vec4 in_material_params;
layout(location = 3) flat in uint in_instance_params;
layout(location = 4) in vec2 in_uv;

layout(set = 0, binding = 0) uniform sampler2D in_albedo_texture;

layout(location = 0) out vec4 out_color;

void main() {
    vec3 normal_world = normalize(in_normal_world);
    vec3 light_dir = normalize(pc.light_direction_intensity.xyz);
    float light_intensity = max(pc.light_direction_intensity.w, 0.0);
    float n_dot_l = max(dot(normal_world, -light_dir), 0.0);

    bool unlit = ((in_instance_params >> 7u) & 0x1u) != 0u;
    float shading = unlit ? 1.0 : (0.2 + n_dot_l * light_intensity);

    vec2 uv = in_uv * pc.material_uv_transform.xy + pc.material_uv_transform.zw;
    vec4 sampled_albedo = texture(in_albedo_texture, uv);
    vec4 base_albedo = vec4(in_albedo.rgb, in_albedo.a) * sampled_albedo;
    bool alpha_test_enabled = (pc.material_flags & 0x1u) != 0u;
    if (alpha_test_enabled && base_albedo.a < clamp(pc.alpha_cutoff, 0.0, 1.0)) {
        discard;
    }

    vec3 lit_color = base_albedo.rgb * shading;
    out_color = vec4(lit_color, base_albedo.a);
}

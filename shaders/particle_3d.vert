#version 460

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_size;
layout(location = 2) in float in_rotation_radians;
layout(location = 3) in float in_stretch_factor;
layout(location = 4) in vec4 in_color;
layout(location = 5) in vec3 in_velocity;
layout(location = 6) in uint in_texture_slot;
layout(location = 7) in uint in_sampler_slot;

layout(push_constant) uniform Particle3DPushConstants {
    mat4 view_projection;
    vec4 camera_right;
    vec4 camera_up;
    vec4 camera_forward;
    uint params;
    uint reserved0;
    uint reserved1;
    uint reserved2;
} pc;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;
layout(location = 2) flat out uint out_texture_slot;
layout(location = 3) flat out uint out_sampler_slot;

vec2 corner01_for_vertex(uint vertex_index) {
    switch (vertex_index) {
    case 0u: return vec2(0.0, 0.0);
    case 1u: return vec2(1.0, 0.0);
    case 2u: return vec2(1.0, 1.0);
    case 3u: return vec2(1.0, 1.0);
    case 4u: return vec2(0.0, 1.0);
    default: return vec2(0.0, 0.0);
    }
}

vec3 safe_normalize(vec3 value, vec3 fallback_value) {
    float len_sq = dot(value, value);
    if (len_sq <= 1e-10) {
        return fallback_value;
    }
    return value * inversesqrt(len_sq);
}

void main() {
    const uint render_mode = pc.params & 0xFFu;
    const uint facing_mode = (pc.params >> 8u) & 0xFFu;

    const vec2 corner01 = corner01_for_vertex(uint(gl_VertexIndex));
    const vec2 corner_signed = corner01 - vec2(0.5, 0.5);

    vec3 basis_right = vec3(1.0, 0.0, 0.0);
    vec3 basis_up = vec3(0.0, 1.0, 0.0);

    if (render_mode == 0u || render_mode == 2u || render_mode == 3u) {
        basis_right = safe_normalize(pc.camera_right.xyz, vec3(1.0, 0.0, 0.0));
        basis_up = safe_normalize(pc.camera_up.xyz, vec3(0.0, 1.0, 0.0));

        if (facing_mode == 1u) {
            const vec3 view_normal = safe_normalize(pc.camera_forward.xyz, vec3(0.0, 0.0, -1.0));
            vec3 velocity_plane = in_velocity - view_normal * dot(in_velocity, view_normal);
            basis_right = safe_normalize(velocity_plane, basis_right);
            basis_up = safe_normalize(cross(view_normal, basis_right), basis_up);
        }
    }

    const float stretch_factor = max(in_stretch_factor, 1.0);
    const vec2 scaled_size = vec2(in_size.x * stretch_factor, in_size.y);
    vec2 local = corner_signed * scaled_size;

    const float s = sin(in_rotation_radians);
    const float c = cos(in_rotation_radians);
    local = vec2(c * local.x - s * local.y,
                 s * local.x + c * local.y);

    const vec3 world = in_position +
                       basis_right * local.x +
                       basis_up * local.y;

    gl_Position = pc.view_projection * vec4(world, 1.0);
    out_uv = corner01;
    out_color = in_color;
    out_texture_slot = in_texture_slot;
    out_sampler_slot = in_sampler_slot;
}

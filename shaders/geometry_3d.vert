#version 460

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 12) in vec3 in_morph0_position_delta;
layout(location = 13) in vec3 in_morph0_normal_delta;
layout(location = 14) in vec3 in_morph1_position_delta;
layout(location = 15) in vec3 in_morph1_normal_delta;

layout(location = 3) in vec4 in_world_row0;
layout(location = 4) in vec4 in_world_row1;
layout(location = 5) in vec4 in_world_row2;
layout(location = 6) in vec4 in_world_row3;
layout(location = 7) in vec4 in_material_params;
layout(location = 8) in vec4 in_albedo;
layout(location = 9) in uint in_instance_params;
layout(location = 10) in vec4 in_deform_param0;
layout(location = 11) in vec4 in_deform_param1;
layout(location = 16) in vec4 in_morph_weights;
layout(location = 17) in uvec4 in_joint_indices;
layout(location = 18) in vec4 in_joint_weights;
layout(location = 21) in uint in_component_index;

layout(push_constant) uniform Geometry3DPushConstants {
    mat4 view_projection;
    vec4 light_direction_intensity;
} pc;

struct SkeletalComponentGpu {
    uint matrix_offset;
    uint joint_count;
    uint flags;
    uint reserved0;
};

layout(std430, set = 2, binding = 5) readonly buffer SkeletalComponentBuffer {
    SkeletalComponentGpu skeletal_components[];
};

layout(std430, set = 2, binding = 6) readonly buffer SkeletalMatrixBuffer {
    mat4 skeletal_matrices[];
};

layout(location = 0) out vec3 out_normal_world;
layout(location = 1) out vec4 out_albedo;
layout(location = 2) out vec4 out_material_params;
layout(location = 3) flat out uint out_instance_params;
layout(location = 4) out vec2 out_uv;
layout(location = 5) out vec3 out_world_position;

vec3 apply_vertex_deform(vec3 local_position, vec3 local_normal) {
    if (all(equal(in_deform_param0, vec4(0.0))) &&
        all(equal(in_deform_param1, vec4(0.0)))) {
        return local_position;
    }

    vec3 axis = vec3(in_deform_param0.y, in_deform_param0.z, in_deform_param0.w);
    float axis_length = length(axis);
    if (axis_length <= 1e-6) {
        axis = local_normal;
        axis_length = length(axis);
    }
    axis = (axis_length > 1e-6) ? (axis / axis_length) : vec3(0.0, 0.0, 1.0);

    float amplitude = in_deform_param0.x;
    float frequency = max(abs(in_deform_param1.y), 1e-4);
    float phase = in_deform_param1.x;
    float bias = in_deform_param1.z;
    float normal_offset = in_deform_param1.w;
    float wave = sin(dot(local_position, axis) * frequency + phase) * amplitude + bias;
    return local_position + axis * wave + local_normal * normal_offset;
}

uint skeletal_component_count() {
    return uint(skeletal_components.length());
}

uint skeletal_matrix_count() {
    return uint(skeletal_matrices.length());
}

bool has_valid_skeletal_component() {
    return in_component_index < skeletal_component_count();
}

vec3 apply_morph_position(vec3 local_position) {
    return local_position +
           in_morph0_position_delta * in_morph_weights.x +
           in_morph1_position_delta * in_morph_weights.y;
}

vec3 apply_morph_normal(vec3 local_normal) {
    vec3 morphed_normal = local_normal +
                          in_morph0_normal_delta * in_morph_weights.x +
                          in_morph1_normal_delta * in_morph_weights.y;
    float normal_length = length(morphed_normal);
    return (normal_length > 1e-6) ? (morphed_normal / normal_length) : local_normal;
}

bool skeletal_enabled() {
    return has_valid_skeletal_component() &&
           (skeletal_components[in_component_index].flags & 1u) != 0u;
}

mat4 fetch_joint_matrix(uint joint_index) {
    if (!has_valid_skeletal_component()) {
        return mat4(1.0);
    }
    SkeletalComponentGpu component = skeletal_components[in_component_index];
    uint matrix_count = skeletal_matrix_count();
    if ((component.flags & 1u) == 0u ||
        joint_index >= component.joint_count ||
        component.matrix_offset >= matrix_count) {
        return mat4(1.0);
    }
    uint matrix_index = component.matrix_offset + joint_index;
    if (matrix_index < component.matrix_offset || matrix_index >= matrix_count) {
        return mat4(1.0);
    }
    return skeletal_matrices[matrix_index];
}

vec3 apply_skinning_position(vec3 local_position) {
    if (!skeletal_enabled() || dot(in_joint_weights, vec4(1.0)) <= 1e-6) {
        return local_position;
    }

    vec4 skinned = vec4(0.0);
    skinned += fetch_joint_matrix(in_joint_indices.x) * vec4(local_position, 1.0) * in_joint_weights.x;
    skinned += fetch_joint_matrix(in_joint_indices.y) * vec4(local_position, 1.0) * in_joint_weights.y;
    skinned += fetch_joint_matrix(in_joint_indices.z) * vec4(local_position, 1.0) * in_joint_weights.z;
    skinned += fetch_joint_matrix(in_joint_indices.w) * vec4(local_position, 1.0) * in_joint_weights.w;
    return skinned.xyz;
}

vec3 apply_skinning_normal(vec3 local_normal) {
    if (!skeletal_enabled() || dot(in_joint_weights, vec4(1.0)) <= 1e-6) {
        return local_normal;
    }

    vec3 skinned = vec3(0.0);
    skinned += mat3(fetch_joint_matrix(in_joint_indices.x)) * local_normal * in_joint_weights.x;
    skinned += mat3(fetch_joint_matrix(in_joint_indices.y)) * local_normal * in_joint_weights.y;
    skinned += mat3(fetch_joint_matrix(in_joint_indices.z)) * local_normal * in_joint_weights.z;
    skinned += mat3(fetch_joint_matrix(in_joint_indices.w)) * local_normal * in_joint_weights.w;
    float skinned_length = length(skinned);
    return (skinned_length > 1e-6) ? (skinned / skinned_length) : local_normal;
}

void main() {
    vec3 morphed_normal = apply_morph_normal(in_normal);
    vec3 morphed_position = apply_morph_position(in_position);
    vec3 skinned_normal = apply_skinning_normal(morphed_normal);
    vec3 skinned_position = apply_skinning_position(morphed_position);
    vec3 local_position = apply_vertex_deform(skinned_position, skinned_normal);
    mat4 world = mat4(in_world_row0, in_world_row1, in_world_row2, in_world_row3);
    vec4 world_position = world * vec4(local_position, 1.0);
    gl_Position = pc.view_projection * world_position;
    gl_PointSize = 1.0;
    out_world_position = world_position.xyz;

    mat3 world3x3 = mat3(world);
    vec3 normal_world = world3x3 * skinned_normal;
    float normal_length = length(normal_world);
    out_normal_world = (normal_length > 1e-6) ? (normal_world / normal_length) : vec3(0.0, 0.0, 1.0);
    out_albedo = in_albedo;
    out_material_params = in_material_params;
    out_instance_params = in_instance_params;
    out_uv = in_uv;
}

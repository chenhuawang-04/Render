#version 460

layout(location = 0) in vec3 in_position;
layout(location = 12) in vec3 in_morph0_position_delta;
layout(location = 14) in vec3 in_morph1_position_delta;
layout(location = 17) in uvec4 in_joint_indices;
layout(location = 18) in vec4 in_joint_weights;

layout(push_constant) uniform ShadowPushConstants {
    mat4 view_projection;
    float world_affine[12];
    vec4 morph_weights;
} pc;

struct SkeletalComponentGpu {
    uint matrix_offset;
    uint joint_count;
    uint flags;
    uint reserved0;
};

layout(std430, set = 0, binding = 0) readonly buffer SkeletalComponentBuffer {
    SkeletalComponentGpu skeletal_components[];
};

layout(std430, set = 0, binding = 1) readonly buffer SkeletalMatrixBuffer {
    mat4 skeletal_matrices[];
};

vec3 apply_morph_position(vec3 local_position) {
    return local_position +
           in_morph0_position_delta * pc.morph_weights.x +
           in_morph1_position_delta * pc.morph_weights.y;
}

uint skeletal_component_index() {
    return uint(pc.morph_weights.z + 0.5);
}

bool skeletal_enabled() {
    return (skeletal_components[skeletal_component_index()].flags & 1u) != 0u;
}

mat4 fetch_joint_matrix(uint joint_index) {
    SkeletalComponentGpu component = skeletal_components[skeletal_component_index()];
    if ((component.flags & 1u) == 0u || joint_index >= component.joint_count) {
        return mat4(1.0);
    }
    return skeletal_matrices[component.matrix_offset + joint_index];
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

vec3 apply_affine_world(vec3 local_position) {
    return vec3(
        pc.world_affine[0] * local_position.x +
            pc.world_affine[3] * local_position.y +
            pc.world_affine[6] * local_position.z +
            pc.world_affine[9],
        pc.world_affine[1] * local_position.x +
            pc.world_affine[4] * local_position.y +
            pc.world_affine[7] * local_position.z +
            pc.world_affine[10],
        pc.world_affine[2] * local_position.x +
            pc.world_affine[5] * local_position.y +
            pc.world_affine[8] * local_position.z +
            pc.world_affine[11]);
}

void main() {
    vec3 morphed_position = apply_morph_position(in_position);
    vec3 skinned_position = apply_skinning_position(morphed_position);
    vec4 world_position = vec4(apply_affine_world(skinned_position), 1.0);
    gl_Position = pc.view_projection * world_position;
    gl_PointSize = 1.0;
}

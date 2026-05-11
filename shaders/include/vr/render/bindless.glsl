#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D g_Textures2D[];
layout(set = 1, binding = 0) uniform sampler g_Samplers[];

vec4 SampleTexture2D(uint texture_slot, uint sampler_slot, vec2 uv) {
    return texture(
        sampler2D(
            g_Textures2D[nonuniformEXT(texture_slot)],
            g_Samplers[nonuniformEXT(sampler_slot)]
        ),
        uv
    );
}

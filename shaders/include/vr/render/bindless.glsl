#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D g_Textures2D[];
layout(set = 0, binding = 0) uniform texture2DArray g_Textures2DArray[];
layout(set = 0, binding = 0) uniform textureCube g_TexturesCube[];
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

vec4 SampleTextureCube(uint texture_slot, uint sampler_slot, vec3 direction) {
    return texture(
        samplerCube(
            g_TexturesCube[nonuniformEXT(texture_slot)],
            g_Samplers[nonuniformEXT(sampler_slot)]
        ),
        direction
    );
}

vec4 SampleTexture2DArray(uint texture_slot, uint sampler_slot, vec3 uvw) {
    return texture(
        sampler2DArray(
            g_Textures2DArray[nonuniformEXT(texture_slot)],
            g_Samplers[nonuniformEXT(sampler_slot)]
        ),
        uvw
    );
}

vec4 SampleTextureCubeLod(uint texture_slot, uint sampler_slot, vec3 direction, float lod) {
    return textureLod(
        samplerCube(
            g_TexturesCube[nonuniformEXT(texture_slot)],
            g_Samplers[nonuniformEXT(sampler_slot)]
        ),
        direction,
        lod
    );
}

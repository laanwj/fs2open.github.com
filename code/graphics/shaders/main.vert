#version 450
#extension GL_ARB_separate_shader_objects : enable

// Vertex inputs - match FSO vertex layout
layout(location = 0) in vec4 vertPosition;
layout(location = 1) in vec4 vertColor;       // May not always be present
layout(location = 2) in vec4 vertTexCoord;
layout(location = 3) in vec3 vertNormal;
layout(location = 4) in vec4 vertTangent;
layout(location = 5) in float vertModelID;

// Outputs to fragment shader
layout(location = 0) out vec4 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPosition;

// Model data uniform block - simplified version
// Binding in Material set (set 1) to match FSO's uniform_block_type::ModelData
layout(set = 1, binding = 0, std140) uniform modelData {
    mat4 modelViewMatrix;
    mat4 modelMatrix;
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 textureMatrix;
    mat4 shadow_mv_matrix;
    mat4 shadow_proj_matrix[4];

    vec4 color;

    // Lights array (8 lights, each light is 48 bytes)
    // struct: vec4 position, vec3 diffuse_color, int light_type, vec3 direction, float attenuation, float sourceRadius + padding
    vec4 lights_data[8 * 4];  // Simplified - 4 vec4s per light

    float outlineWidth;
    float fogStart;
    float fogScale;
    int buffer_matrix_offset;

    vec4 clip_equation;

    float thruster_scale;
    int use_clip_plane;
    int n_lights;
    float defaultGloss;

    vec3 ambientFactor;
    int desaturate;

    vec3 diffuseFactor;
    int blend_alpha;

    vec3 emissionFactor;
    int alphaGloss;

    int gammaSpec;
    int envGloss;
    int effect_num;
    int sBasemapIndex;

    vec4 fogColor;

    vec3 base_color;
    float anim_timer;

    vec3 stripe_color;
    float vpwidth;

    float vpheight;
    int team_glow_enabled;
    float znear;
    float zfar;

    float veryneardist;
    float neardist;
    float middist;
    float fardist;

    int sGlowmapIndex;
    int sSpecmapIndex;
    int sNormalmapIndex;
    int sAmbientmapIndex;

    int sMiscmapIndex;
    float alphaMult;
    int flags;
    int _pad0;
};

void main() {
    // Transform vertex position
    vec4 worldPos = modelMatrix * vertPosition;
    vec4 viewPos = viewMatrix * worldPos;
    gl_Position = projMatrix * viewPos;

    // Pass texture coordinates (apply texture matrix if needed)
    fragTexCoord = textureMatrix * vertTexCoord;

    // Pass color (use uniform color, multiplied by vertex color if present)
    fragColor = color;

    // Transform normal to view space for lighting
    mat3 normalMatrix = mat3(transpose(inverse(modelViewMatrix)));
    fragNormal = normalize(normalMatrix * vertNormal);

    // Pass view-space position for lighting calculations
    fragPosition = viewPos.xyz;
}

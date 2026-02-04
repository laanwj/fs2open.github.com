#version 450
#extension GL_ARB_separate_shader_objects : enable

// Inputs from vertex shader
layout(location = 0) in vec4 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPosition;

// Output color
layout(location = 0) out vec4 outColor;

// Textures - Material set (set 1)
layout(set = 1, binding = 1) uniform sampler2D sBasemap;
layout(set = 1, binding = 2) uniform sampler2D sGlowmap;
layout(set = 1, binding = 3) uniform sampler2D sSpecmap;
layout(set = 1, binding = 4) uniform sampler2D sNormalmap;
layout(set = 1, binding = 5) uniform sampler2D sHeightmap;
layout(set = 1, binding = 6) uniform sampler2D sAmbientmap;
layout(set = 1, binding = 7) uniform sampler2D sMiscmap;

// Model data uniform block - must match vertex shader
layout(set = 1, binding = 0, std140) uniform modelData {
    mat4 modelViewMatrix;
    mat4 modelMatrix;
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 textureMatrix;
    mat4 shadow_mv_matrix;
    mat4 shadow_proj_matrix[4];

    vec4 color;

    vec4 lights_data[8 * 4];

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

// Simple directional light calculation
vec3 calculateLighting(vec3 normal, vec3 viewDir) {
    // Simplified lighting - just ambient + one directional light
    vec3 ambient = ambientFactor * 0.3;

    // Simple directional light from above-front
    vec3 lightDir = normalize(vec3(0.0, 0.5, 1.0));
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diffuseFactor * diff * 0.7;

    return ambient + diffuse;
}

void main() {
    // Sample base texture
    vec4 baseColor = texture(sBasemap, fragTexCoord.xy);

    // Apply vertex/uniform color
    baseColor *= fragColor;

    // Apply alpha multiplier
    baseColor.a *= alphaMult;

    // Simple lighting
    vec3 normal = normalize(fragNormal);
    vec3 viewDir = normalize(-fragPosition);
    vec3 lighting = calculateLighting(normal, viewDir);

    // Apply lighting to color
    vec3 finalColor = baseColor.rgb * lighting;

    // Add emission/glow if present
    finalColor += emissionFactor * baseColor.rgb * 0.1;

    // Output final color
    outColor = vec4(finalColor, baseColor.a);

    // Discard fully transparent pixels
    if (outColor.a < 0.01) {
        discard;
    }
}

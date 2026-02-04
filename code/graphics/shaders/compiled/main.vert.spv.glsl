#version 150

layout(std140) uniform modelData
{
    mat4 modelViewMatrix;
    mat4 modelMatrix;
    mat4 viewMatrix;
    mat4 projMatrix;
    mat4 textureMatrix;
    mat4 shadow_mv_matrix;
    mat4 shadow_proj_matrix[4];
    vec4 color;
    vec4 lights_data[32];
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
} _21;

in vec4 vertPosition;
out vec4 fragTexCoord;
in vec4 vertTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
in vec3 vertNormal;
out vec3 fragPosition;

void main()
{
    vec4 _35 = _21.viewMatrix * (_21.modelMatrix * vertPosition);
    gl_Position = _21.projMatrix * _35;
    fragTexCoord = _21.textureMatrix * vertTexCoord;
    fragColor = _21.color;
    mat4 _67 = transpose(inverse(_21.modelViewMatrix));
    fragNormal = normalize(mat3(_67[0].xyz, _67[1].xyz, _67[2].xyz) * vertNormal);
    fragPosition = _35.xyz;
}


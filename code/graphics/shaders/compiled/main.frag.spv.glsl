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
} _26;

uniform sampler2DArray sBasemap;

in vec4 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
out vec4 outColor;

void main()
{
    vec4 _81 = texture(sBasemap, vec3(fragTexCoord.xy, float(_26.sBasemapIndex))) * fragColor;
    vec3 _109 = _81.xyz;
    outColor = vec4((_109 * ((_26.ambientFactor * 0.300000011920928955078125) + ((_26.diffuseFactor * max(dot(normalize(fragNormal), vec3(0.0, 0.447213590145111083984375, 0.89442718029022216796875)), 0.0)) * 0.699999988079071044921875))) + ((_26.emissionFactor * _109) * 0.100000001490116119384765625), _81.w * _26.alphaMult);
    if (outColor.w < 0.00999999977648258209228515625)
    {
        discard;
    }
}


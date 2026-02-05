#version 150

struct model_light
{
    vec4 position;
    vec3 diffuse_color;
    int light_type;
    vec3 direction;
    float attenuation;
    float ml_sourceRadius;
};

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
    model_light lights[8];
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
    float _pad0;
} _249;

uniform sampler2DArray materialTextures[16];

in vec4 fragPosition;
in vec4 fragTexCoord;
out vec4 fragOut0;

vec4 _2207;

void main()
{
    vec2 _1977;
    if (_249.effect_num >= 0)
    {
        float _609 = ((fragPosition.x * fragPosition.w) * 0.004999999888241291046142578125) + (_249.anim_timer * 20.0);
        float _616 = (fragPosition.y * fragPosition.w) * 0.004999999888241291046142578125;
        _1977 = vec2(cos(_609) * sin(_616), sin(_609) * cos(_616)) * 0.02999999932944774627685546875;
    }
    else
    {
        _1977 = vec2(0.0);
    }
    if ((_249.flags & 8) != 0)
    {
        vec2 _1978;
        if (_249.effect_num == 2)
        {
            _1978 = fragTexCoord.xy + (_1977 * (1.0 - _249.anim_timer));
        }
        else
        {
            _1978 = fragTexCoord.xy;
        }
        vec4 _671 = texture(materialTextures[0], vec3(_1978, float(_249.sBasemapIndex)));
        vec4 _1980;
        if ((_249.flags & 16384) != 0)
        {
            vec4 _1812 = _2207;
            _1812.w = _671.w * _249.alphaMult;
            _1980 = _1812;
        }
        else
        {
            _1980 = _671;
        }
        bool _705 = _249.blend_alpha == 0;
        bool _712;
        if (_705)
        {
            _712 = _1980.w < 0.949999988079071044921875;
        }
        else
        {
            _712 = _705;
        }
        if (_712)
        {
            discard;
        }
    }
    fragOut0 = texture(materialTextures[0], vec3(fragTexCoord.xy, 0.0));
}


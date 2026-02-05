#version 150

out float gl_ClipDistance[1];

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
} _31;

in vec4 vertTexCoord;
in vec4 vertPosition;
in vec3 vertNormal;
in vec4 vertTangent;
out vec3 outTangent;
out vec3 outBitangent;
out vec3 outTangentNormal;
out float outFogDist;
out vec4 outPosition;
out vec3 outNormal;
out vec4 outTexCoord;

void main()
{
    vec4 _212;
    if ((_31.flags & 8192) != 0)
    {
        vec4 _213;
        if (vertPosition.z < (-1.5))
        {
            vec4 _211 = vertPosition;
            _211.z = vertPosition.z * _31.thruster_scale;
            _213 = _211;
        }
        else
        {
            _213 = vertPosition;
        }
        _212 = _213;
    }
    else
    {
        _212 = vertPosition;
    }
    mat3 _90 = mat3(_31.modelViewMatrix[0].xyz, _31.modelViewMatrix[1].xyz, _31.modelViewMatrix[2].xyz) * mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));
    vec3 _95 = normalize(_90 * vertNormal);
    vec4 _102 = (_31.modelViewMatrix * mat4(vec4(1.0, 0.0, 0.0, 0.0), vec4(0.0, 1.0, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0))) * _212;
    gl_Position = _31.projMatrix * _102;
    vec3 _138 = normalize(_90 * vertTangent.xyz);
    outTangent = _138;
    outBitangent = cross(_95, _138) * vertTangent.w;
    outTangentNormal = _95;
    if ((_31.flags & 1024) != 0)
    {
        outFogDist = clamp(((gl_Position.z - _31.fogStart) * 0.75) * _31.fogScale, 0.0, 1.0);
    }
    else
    {
        outFogDist = 0.0;
    }
    if (_31.use_clip_plane != 0)
    {
        gl_ClipDistance[0] = dot(_31.clip_equation, (_31.modelMatrix * mat4(vec4(1.0, 0.0, 0.0, 0.0), vec4(0.0, 1.0, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0))) * _212);
    }
    else
    {
        gl_ClipDistance[0] = 1.0;
    }
    outPosition = _102;
    outNormal = _95;
    outTexCoord = _31.textureMatrix * vertTexCoord;
}


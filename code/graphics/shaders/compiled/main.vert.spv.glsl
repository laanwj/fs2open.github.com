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
} _33;

layout(std430) readonly buffer TransformBuffer
{
    mat4 transforms[];
} transformBuf;

in float vertModelID;
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
    bool _41 = (_33.flags & 2048) != 0;
    mat4 _257;
    bool _262;
    if (_41)
    {
        int _58 = _33.buffer_matrix_offset + int(vertModelID);
        mat4 _251 = transformBuf.transforms[_58];
        _251[3].w = 1.0;
        _262 = transformBuf.transforms[_58][3].w >= 0.89999997615814208984375;
        _257 = _251;
    }
    else
    {
        _262 = false;
        _257 = mat4(vec4(1.0, 0.0, 0.0, 0.0), vec4(0.0, 1.0, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0));
    }
    vec4 _259;
    if ((_33.flags & 8192) != 0)
    {
        vec4 _260;
        if (vertPosition.z < (-1.5))
        {
            vec4 _255 = vertPosition;
            _255.z = vertPosition.z * _33.thruster_scale;
            _260 = _255;
        }
        else
        {
            _260 = vertPosition;
        }
        _259 = _260;
    }
    else
    {
        _259 = vertPosition;
    }
    mat3 _124 = mat3(_33.modelViewMatrix[0].xyz, _33.modelViewMatrix[1].xyz, _33.modelViewMatrix[2].xyz) * mat3(_257[0].xyz, _257[1].xyz, _257[2].xyz);
    vec3 _129 = normalize(_124 * vertNormal);
    vec4 _136 = (_33.modelViewMatrix * _257) * _259;
    gl_Position = _33.projMatrix * _136;
    if (_41 && _262)
    {
        gl_Position = vec4(-2.0, -2.0, -2.0, 1.0);
    }
    vec3 _182 = normalize(_124 * vertTangent.xyz);
    outTangent = _182;
    outBitangent = cross(_129, _182) * vertTangent.w;
    outTangentNormal = _129;
    if ((_33.flags & 1024) != 0)
    {
        outFogDist = clamp(((gl_Position.z - _33.fogStart) * 0.75) * _33.fogScale, 0.0, 1.0);
    }
    else
    {
        outFogDist = 0.0;
    }
    if (_33.use_clip_plane != 0)
    {
        gl_ClipDistance[0] = dot(_33.clip_equation, (_33.modelMatrix * _257) * _259);
    }
    else
    {
        gl_ClipDistance[0] = 1.0;
    }
    outPosition = _136;
    outNormal = _129;
    outTexCoord = _33.textureMatrix * vertTexCoord;
}


#version 150
#ifdef GL_ARB_shader_draw_parameters
#extension GL_ARB_shader_draw_parameters : enable
#endif

out float gl_ClipDistance[1];

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
    vec4 _light_pad[24];
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
} _32;

layout(std430) readonly buffer TransformBuffer
{
    mat4 transforms[];
} transformBuf;

in float vertModelID;
in vec4 vertPosition;
#ifdef GL_ARB_shader_draw_parameters
#define SPIRV_Cross_BaseInstance gl_BaseInstanceARB
#else
uniform int SPIRV_Cross_BaseInstance;
#endif

void main()
{
    bool _40 = (_32.flags & 2048) != 0;
    mat4 _149;
    bool _154;
    if (_40)
    {
        int _57 = _32.buffer_matrix_offset + int(vertModelID);
        mat4 _143 = transformBuf.transforms[_57];
        _143[3].w = 1.0;
        _154 = transformBuf.transforms[_57][3].w >= 0.89999997615814208984375;
        _149 = _143;
    }
    else
    {
        _154 = false;
        _149 = mat4(vec4(1.0, 0.0, 0.0, 0.0), vec4(0.0, 1.0, 0.0, 0.0), vec4(0.0, 0.0, 1.0, 0.0), vec4(0.0, 0.0, 0.0, 1.0));
    }
    vec4 _151;
    if ((_32.flags & 8192) != 0)
    {
        vec4 _152;
        if (vertPosition.z < (-1.5))
        {
            vec4 _147 = vertPosition;
            _147.z = vertPosition.z * _32.thruster_scale;
            _152 = _147;
        }
        else
        {
            _152 = vertPosition;
        }
        _151 = _152;
    }
    else
    {
        _151 = vertPosition;
    }
    gl_Position = _32.shadow_proj_matrix[(gl_InstanceID + SPIRV_Cross_BaseInstance)] * ((_32.modelViewMatrix * _149) * _151);
    gl_Position.z = clamp(gl_Position.z, 0.0, gl_Position.w);
    gl_Layer = (gl_InstanceID + SPIRV_Cross_BaseInstance);
    gl_ClipDistance[0] = 1.0;
    if (_40 && _154)
    {
        gl_Position = vec4(-2.0, -2.0, -2.0, 1.0);
    }
}


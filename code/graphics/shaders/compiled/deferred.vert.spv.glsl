#version 150

layout(std140) uniform lightData
{
    vec3 diffuseLightColor;
    float coneAngle;
    vec3 lightDir;
    float coneInnerAngle;
    vec3 coneDir;
    float dualCone;
    vec3 scale;
    float lightRadius;
    int lightType;
    int enable_shadows;
    float sourceRadius;
    float pad0;
} _14;

layout(std140) uniform matrixData
{
    mat4 modelViewMatrix;
    mat4 projMatrix;
} _69;

in vec4 vertPosition;

void main()
{
    bool _20 = _14.lightType == 0;
    bool _28;
    if (!_20)
    {
        _28 = _14.lightType == 4;
    }
    else
    {
        _28 = _20;
    }
    if (_28)
    {
        gl_Position = vec4((vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2)) * 2.0) - vec2(1.0), 0.0, 1.0);
    }
    else
    {
        gl_Position = (_69.projMatrix * _69.modelViewMatrix) * vec4(vertPosition.xyz * _14.scale, 1.0);
    }
}


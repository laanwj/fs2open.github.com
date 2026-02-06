#version 150

layout(std140) uniform genericData
{
    mat4 modelMatrix;
    vec4 color;
    vec4 clipEquation;
    int baseMapIndex;
    int alphaTexture;
    int noTexturing;
    int srgb;
    float intensity;
    float alphaThreshold;
    uint clipEnabled;
} _19;

layout(std140) uniform matrixData
{
    mat4 modelViewMatrix;
    mat4 projMatrix;
} _33;

out vec4 fragColor;
in vec4 vertColor;
in vec4 vertPosition;
out vec4 fragTexCoord;
in vec4 vertTexCoord;

void main()
{
    fragColor = vertColor * _19.color;
    gl_Position = (_33.projMatrix * _33.modelViewMatrix) * vertPosition;
    fragTexCoord = vertTexCoord;
}


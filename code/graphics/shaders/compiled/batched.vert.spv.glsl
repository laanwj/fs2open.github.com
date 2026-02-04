#version 150

layout(std140) uniform genericData
{
    vec4 color;
    float intensity;
    float pad[3];
} _19;

layout(std140) uniform matrixData
{
    mat4 modelViewMatrix;
    mat4 projMatrix;
} _34;

out vec4 fragColor;
in vec4 vertColor;
in vec4 vertPosition;
out vec4 fragTexCoord;
in vec4 vertTexCoord;

void main()
{
    fragColor = vertColor * _19.color;
    gl_Position = (_34.projMatrix * _34.modelViewMatrix) * vertPosition;
    fragTexCoord = vertTexCoord;
}


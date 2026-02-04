#version 150

layout(std140) uniform matrixData
{
    mat4 modelViewMatrix;
    mat4 projMatrix;
} _25;

out vec4 fragTexCoord;
in vec4 vertTexCoord;
in vec4 vertPosition;

void main()
{
    fragTexCoord = vertTexCoord;
    gl_Position = (_25.projMatrix * _25.modelViewMatrix) * vertPosition;
}


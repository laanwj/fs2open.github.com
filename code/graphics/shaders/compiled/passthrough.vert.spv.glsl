#version 150

layout(std140) uniform matrixData
{
    mat4 modelViewMatrix;
    mat4 projMatrix;
} _28;

out vec4 fragTexCoord;
in vec4 vertTexCoord;
out vec4 fragColor;
in vec4 vertPosition;

void main()
{
    fragTexCoord = vertTexCoord;
    fragColor = vec4(1.0);
    gl_Position = (_28.projMatrix * _28.modelViewMatrix) * vertPosition;
}


#version 150

layout(std140) uniform genericData
{
    mat4 projMatrix;
    vec2 offset;
    int textured;
    int baseMapIndex;
    float horizontalSwipeOffset;
    float pad[3];
} _31;

out vec2 fragTexCoord;
in vec2 vertTexCoord;
out vec4 fragColor;
in vec4 vertColor;
in vec2 vertPosition;
out vec2 fragScreenPosition;

void main()
{
    fragTexCoord = vertTexCoord;
    fragColor = vertColor;
    vec4 _41 = vec4(vertPosition + _31.offset, 0.0, 1.0);
    fragScreenPosition = _41.xy;
    gl_Position = _31.projMatrix * _41;
}


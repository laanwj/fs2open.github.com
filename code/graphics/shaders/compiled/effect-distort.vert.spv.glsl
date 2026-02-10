#version 150

layout(std140) uniform GenericData
{
    float window_width;
    float window_height;
    float use_offset;
    float pad;
} _24;

layout(std140) uniform Matrices
{
    mat4 modelViewMatrix;
    mat4 projMatrix;
} _41;

out vec4 fragTexCoord;
in vec4 vertTexCoord;
out vec4 fragColor;
in vec4 vertColor;
out float fragOffset;
in float vertRadius;
in vec3 vertPosition;

void main()
{
    fragTexCoord = vertTexCoord;
    fragColor = vertColor;
    fragOffset = vertRadius * _24.use_offset;
    gl_Position = (_41.projMatrix * _41.modelViewMatrix) * vec4(vertPosition, 1.0);
}


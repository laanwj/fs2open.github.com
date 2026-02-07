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
} _20;

out vec4 fragTexCoord;
in vec4 vertTexCoord;
out vec4 fragColor;
in vec4 vertPosition;

void main()
{
    fragTexCoord = vertTexCoord;
    fragColor = _20.color;
    gl_Position = vec4((vertPosition.x * 0.001041666720993816852569580078125) - 1.0, 1.0 - (vertPosition.y * 0.00185185181908309459686279296875), 0.0, 1.0);
}


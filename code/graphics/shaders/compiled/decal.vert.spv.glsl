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
    int clipEnabled;
} _19;

out vec4 fragTexCoord;
in vec4 vertTexCoord;
out vec4 fragColor;
in vec4 vertPosition;

void main()
{
    fragTexCoord = vertTexCoord;
    fragColor = _19.color;
    gl_Position = vec4(((vertPosition.xy * vec2(0.0005208333604969084262847900390625, 0.000925925909541547298431396484375)) * 2.0) - vec2(1.0), 0.0, 1.0);
}


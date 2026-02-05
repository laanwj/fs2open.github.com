#version 150

out float gl_ClipDistance[1];

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

layout(std140) uniform matrixData
{
    mat4 modelViewMatrix;
    mat4 projMatrix;
} _36;

out vec4 fragTexCoord;
in vec4 vertTexCoord;
out vec4 fragColor;
out float debugMatrixVal;
in vec4 vertPosition;

void main()
{
    fragTexCoord = vertTexCoord;
    fragColor = _20.color;
    debugMatrixVal = 0.5;
    gl_Position = (_36.projMatrix * _36.modelViewMatrix) * vertPosition;
    if (_20.clipEnabled != 0u)
    {
        gl_ClipDistance[0] = dot(_20.clipEquation, _20.modelMatrix * vertPosition);
    }
}


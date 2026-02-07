#version 150

layout(std140) uniform NanoVGUniformData
{
    mat3 scissorMat;
    mat3 paintMat;
    vec4 innerCol;
    vec4 outerCol;
    vec2 scissorExt;
    vec2 scissorScale;
    vec2 extent;
    float radius;
    float feather;
    float strokeMult;
    float strokeThr;
    int texType;
    int type;
    vec2 viewSize;
    int texArrayIndex;
} _38;

out vec2 ftcoord;
in vec4 vertTexCoord;
out vec2 fpos;
in vec4 vertPosition;

void main()
{
    ftcoord = vertTexCoord.xy;
    fpos = vertPosition.xy;
    gl_Position = vec4(((2.0 * vertPosition.x) / _38.viewSize.x) - 1.0, 1.0 - ((2.0 * vertPosition.y) / _38.viewSize.y), 0.0, 1.0);
}


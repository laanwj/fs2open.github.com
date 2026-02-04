#version 150

layout(std140) uniform genericData
{
    mat4 projMatrix;
    vec2 offset;
    int textured;
    int baseMapIndex;
    float horizontalSwipeOffset;
    float pad[3];
} _23;

uniform sampler2DArray baseMap;

in vec2 fragScreenPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 fragOut0;

void main()
{
    if (fragScreenPosition.x > _23.horizontalSwipeOffset)
    {
        discard;
    }
    vec4 _96;
    if (_23.textured != 0)
    {
        _96 = texture(baseMap, vec3(fragTexCoord, float(_23.baseMapIndex))) * fragColor;
    }
    else
    {
        _96 = fragColor;
    }
    vec4 _97;
    if ((_23.horizontalSwipeOffset - fragScreenPosition.x) < 10.0)
    {
        vec4 _91 = _96;
        _91.x = 1.0;
        vec4 _93 = _91;
        _93.y = 1.0;
        vec4 _95 = _93;
        _95.z = 1.0;
        _97 = _95;
    }
    else
    {
        _97 = _96;
    }
    fragOut0 = _97;
}


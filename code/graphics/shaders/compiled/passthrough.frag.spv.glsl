#version 150

layout(std140) uniform genericData
{
    int noTexturing;
    int srgb;
    int baseMapIndex;
    float pad;
} _37;

uniform sampler2DArray baseMap;

in vec4 fragTexCoord;
in vec4 fragColor;
out vec4 fragOut0;

void main()
{
    vec4 _46 = texture(baseMap, vec3(fragTexCoord.xy, float(_37.baseMapIndex)));
    bool _51 = _37.srgb == 1;
    vec3 _124;
    if (_51)
    {
        _124 = pow(_46.xyz, vec3(2.2000000476837158203125));
    }
    else
    {
        _124 = _46.xyz;
    }
    vec4 _119 = _46;
    _119.x = _124.x;
    vec4 _121 = _119;
    _121.y = _124.y;
    vec4 _123 = _121;
    _123.z = _124.z;
    vec4 _126;
    if (_51)
    {
        _126 = vec4(pow(fragColor.xyz, vec3(2.2000000476837158203125)), fragColor.w);
    }
    else
    {
        _126 = fragColor;
    }
    fragOut0 = mix(_123 * _126, _126, vec4(float(_37.noTexturing)));
}


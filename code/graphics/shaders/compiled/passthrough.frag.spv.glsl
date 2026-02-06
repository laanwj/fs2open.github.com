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
} _39;

uniform sampler2DArray baseMap;

in vec4 fragTexCoord;
in vec4 fragColor;
out vec4 fragOut0;

void main()
{
    vec4 _48 = texture(baseMap, vec3(fragTexCoord.xy, float(_39.baseMapIndex)));
    bool _54 = _39.srgb == 1;
    vec3 _126;
    if (_54)
    {
        _126 = pow(_48.xyz, vec3(2.2000000476837158203125));
    }
    else
    {
        _126 = _48.xyz;
    }
    vec4 _121 = _48;
    _121.x = _126.x;
    vec4 _123 = _121;
    _123.y = _126.y;
    vec4 _125 = _123;
    _125.z = _126.z;
    vec4 _128;
    if (_54)
    {
        _128 = vec4(pow(fragColor.xyz, vec3(2.2000000476837158203125)), fragColor.w);
    }
    else
    {
        _128 = fragColor;
    }
    fragOut0 = mix(_125 * _128, _128, vec4(float(_39.noTexturing)));
}


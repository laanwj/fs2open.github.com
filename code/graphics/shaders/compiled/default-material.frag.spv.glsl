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

uniform sampler2D baseMap;

in vec4 fragTexCoord;
in vec4 fragColor;
out vec4 fragOut0;

void main()
{
    vec4 _33 = texture(baseMap, fragTexCoord.xy);
    if (_39.alphaThreshold > _33.w)
    {
        discard;
    }
    bool _58 = _39.srgb == 1;
    vec4 _160;
    if (_58)
    {
        vec3 _139 = pow(_33.xyz, vec3(2.2000000476837158203125));
        vec4 _146 = _33;
        _146.x = _139.x;
        vec4 _148 = _146;
        _148.y = _139.y;
        vec4 _150 = _148;
        _150.z = _139.z;
        _160 = _150;
    }
    else
    {
        _160 = _33;
    }
    vec4 _161;
    if (_58)
    {
        vec3 _143 = pow(fragColor.xyz, vec3(2.2000000476837158203125));
        vec4 _152 = fragColor;
        _152.x = _143.x;
        vec4 _154 = _152;
        _154.y = _143.y;
        vec4 _156 = _154;
        _156.z = _143.z;
        _161 = _156;
    }
    else
    {
        _161 = fragColor;
    }
    if (_39.noTexturing != 0)
    {
        fragOut0 = _161 * _39.intensity;
    }
    else
    {
        if (_39.alphaTexture != 0)
        {
            fragOut0 = vec4(_161.xyz, _160.x * _161.w) * _39.intensity;
        }
        else
        {
            fragOut0 = (_160 * _161) * _39.intensity;
        }
    }
}


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
    vec4 _158;
    if (_58)
    {
        vec3 _137 = pow(_33.xyz, vec3(2.2000000476837158203125));
        vec4 _144 = _33;
        _144.x = _137.x;
        vec4 _146 = _144;
        _146.y = _137.y;
        vec4 _148 = _146;
        _148.z = _137.z;
        _158 = _148;
    }
    else
    {
        _158 = _33;
    }
    vec4 _159;
    if (_58)
    {
        vec3 _141 = pow(fragColor.xyz, vec3(2.2000000476837158203125));
        vec4 _150 = fragColor;
        _150.x = _141.x;
        vec4 _152 = _150;
        _152.y = _141.y;
        vec4 _154 = _152;
        _154.z = _141.z;
        _159 = _154;
    }
    else
    {
        _159 = fragColor;
    }
    if (_39.noTexturing != 0)
    {
        fragOut0 = _159 * _39.intensity;
    }
    else
    {
        if (_39.alphaTexture != 0)
        {
            fragOut0 = vec4(_159.xyz, _158.x * _159.w) * _39.intensity;
        }
        else
        {
            fragOut0 = (_158 * _159) * _39.intensity;
        }
    }
}


#version 150

layout(std140) uniform genericData
{
    int noTexturing;
    int srgb;
    float pad[2];
} _41;

uniform sampler2D baseMap;

in vec4 fragTexCoord;
in vec4 fragColor;
out vec4 fragOut0;

void main()
{
    vec4 _34 = texture(baseMap, fragTexCoord.xy);
    bool _47 = _41.srgb == 1;
    vec3 _118;
    if (_47)
    {
        _118 = pow(_34.xyz, vec3(2.2000000476837158203125));
    }
    else
    {
        _118 = _34.xyz;
    }
    vec4 _113 = _34;
    _113.x = _118.x;
    vec4 _115 = _113;
    _115.y = _118.y;
    vec4 _117 = _115;
    _117.z = _118.z;
    vec4 _120;
    if (_47)
    {
        _120 = vec4(pow(fragColor.xyz, vec3(2.2000000476837158203125)), fragColor.w);
    }
    else
    {
        _120 = fragColor;
    }
    fragOut0 = mix(_117 * _120, _120, vec4(float(_41.noTexturing)));
}


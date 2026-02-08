#version 150

layout(std140) uniform genericData
{
    float bloom_intensity;
    int levels;
} _28;

uniform sampler2D bloomed;

in vec2 fragTexCoord;
out vec4 fragOut0;

void main()
{
    float _119;
    vec4 _120;
    _120 = vec4(0.0, 0.0, 0.0, 1.0);
    _119 = 0.0;
    for (int _118 = 0; _118 < _28.levels; )
    {
        float _37 = float(_118);
        float _39 = 1.0 / exp2(_37);
        vec3 _61 = _120.xyz + (textureLod(bloomed, fragTexCoord, _37).xyz * _39);
        vec4 _101 = _120;
        _101.x = _61.x;
        vec4 _103 = _101;
        _103.y = _61.y;
        vec4 _105 = _103;
        _105.z = _61.z;
        _120 = _105;
        _119 += _39;
        _118++;
        continue;
    }
    vec3 _78 = _120.xyz / vec3(_119);
    vec4 _107 = _120;
    _107.x = _78.x;
    vec4 _109 = _107;
    _109.y = _78.y;
    vec4 _111 = _109;
    _111.z = _78.z;
    vec3 _90 = _111.xyz * _28.bloom_intensity;
    vec4 _113 = _111;
    _113.x = _90.x;
    vec4 _115 = _113;
    _115.y = _90.y;
    vec4 _117 = _115;
    _117.z = _90.z;
    fragOut0 = _117;
}


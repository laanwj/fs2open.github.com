#version 150

layout(std140) uniform genericData
{
    vec2 sun_pos;
    float density;
    float weight;
    float falloff;
    float intensity;
    float cp_intensity;
    float pad0;
} _16;

uniform sampler2D scene;

in vec2 fragTexCoord;
out vec4 fragOut0;

void main()
{
    vec2 _32 = (fragTexCoord - _16.sun_pos) * (0.0199999995529651641845703125 * _16.density);
    vec4 _98;
    vec2 _99;
    _99 = fragTexCoord;
    _98 = vec4(0.0);
    vec2 _54;
    float _83;
    vec4 _103;
    int _97 = 0;
    float _100 = 1.0;
    for (; _97 < 50; _100 = _83, _99 = _54, _98 = _103, _97++)
    {
        _54 = _99 - _32;
        if (texture(scene, _54).x >= 0.999000012874603271484375)
        {
            _103 = _98 + vec4(_100 * _16.weight);
        }
        else
        {
            _103 = _98;
        }
        _83 = _100 * _16.falloff;
    }
    fragOut0 = _98 * _16.intensity;
    fragOut0.w = 1.0;
}


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
    vec4 _97;
    vec2 _98;
    _98 = fragTexCoord;
    _97 = vec4(0.0);
    vec2 _54;
    float _82;
    vec4 _102;
    int _96 = 0;
    float _99 = 1.0;
    for (; _96 < 50; _99 = _82, _98 = _54, _97 = _102, _96++)
    {
        _54 = _98 - _32;
        if (texture(scene, _54).x == 1.0)
        {
            _102 = _97 + vec4(_99 * _16.weight);
        }
        else
        {
            _102 = _97;
        }
        _82 = _99 * _16.falloff;
    }
    fragOut0 = _97 * _16.intensity;
    fragOut0.w = 1.0;
}


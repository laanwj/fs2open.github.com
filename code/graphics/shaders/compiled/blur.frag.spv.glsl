#version 150

layout(std140) uniform genericData
{
    float texSize;
    int level;
    int direction;
} _47;

uniform sampler2D tex;

in vec2 fragTexCoord;
out vec4 fragOut0;

void main()
{
    float BlurWeights[6];
    BlurWeights[0] = 0.13619999587535858154296875;
    BlurWeights[1] = 0.129700005054473876953125;
    BlurWeights[2] = 0.11200000345706939697265625;
    BlurWeights[3] = 0.087700001895427703857421875;
    BlurWeights[4] = 0.0623000003397464752197265625;
    BlurWeights[5] = 0.0401999987661838531494140625;
    float _51 = float(_47.level);
    vec4 _167;
    _167 = textureLod(tex, fragTexCoord, _51) * BlurWeights[0];
    vec4 _169;
    for (int _166 = 1; _166 < 6; _167 = _169, _166++)
    {
        float _73 = float(_166) * _47.texSize;
        if (_47.direction == 0)
        {
            _169 = (_167 + (textureLod(tex, vec2(clamp(fragTexCoord.x - _73, 0.0, 1.0), fragTexCoord.y), _51) * BlurWeights[_166])) + (textureLod(tex, vec2(clamp(fragTexCoord.x + _73, 0.0, 1.0), fragTexCoord.y), _51) * BlurWeights[_166]);
        }
        else
        {
            _169 = (_167 + (textureLod(tex, vec2(fragTexCoord.x, clamp(fragTexCoord.y - _73, 0.0, 1.0)), _51) * BlurWeights[_166])) + (textureLod(tex, vec2(fragTexCoord.x, clamp(fragTexCoord.y + _73, 0.0, 1.0)), _51) * BlurWeights[_166]);
        }
    }
    fragOut0 = _167;
}


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
    int clipEnabled;
} _26;

uniform sampler2D baseMap;

in vec4 fragTexCoord;
out vec4 fragOut0;
in vec4 fragColor;

void main()
{
    vec4 _21 = texture(baseMap, fragTexCoord.xy);
    if (_26.noTexturing != 0)
    {
        fragOut0 = fragColor * _26.intensity;
    }
    else
    {
        if (_26.alphaTexture != 0)
        {
            fragOut0 = vec4(fragColor.xyz, _21.x * fragColor.w) * _26.intensity;
        }
        else
        {
            fragOut0 = (_21 * fragColor) * _26.intensity;
        }
    }
}


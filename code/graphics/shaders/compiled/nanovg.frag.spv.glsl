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
} _26;

uniform sampler2DArray baseMap;

in vec4 fragTexCoord;
out vec4 fragOut0;
in vec4 fragColor;

void main()
{
    vec4 _36 = texture(baseMap, vec3(fragTexCoord.xy, float(_26.baseMapIndex)));
    if (_26.noTexturing != 0)
    {
        fragOut0 = fragColor * _26.intensity;
    }
    else
    {
        if (_26.alphaTexture != 0)
        {
            fragOut0 = vec4(fragColor.xyz, _36.x * fragColor.w) * _26.intensity;
        }
        else
        {
            fragOut0 = (_36 * fragColor) * _26.intensity;
        }
    }
}


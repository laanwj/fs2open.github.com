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
} _25;

uniform sampler2DArray baseMap;

in vec4 fragTexCoord;
out vec4 fragOut0;
in vec4 fragColor;

void main()
{
    vec4 _35 = texture(baseMap, vec3(fragTexCoord.xy, float(_25.baseMapIndex)));
    if (_25.noTexturing != 0)
    {
        fragOut0 = fragColor * _25.intensity;
    }
    else
    {
        if (_25.alphaTexture != 0)
        {
            fragOut0 = vec4(fragColor.xyz, _35.x * fragColor.w) * _25.intensity;
        }
        else
        {
            fragOut0 = (_35 * fragColor) * _25.intensity;
        }
    }
}


#version 150

layout(std140) uniform genericData
{
    vec4 color;
    float intensity;
    float pad[3];
} _81;

uniform sampler2DArray baseMap;

in vec4 fragTexCoord;
in vec4 fragColor;
out vec4 fragOut0;

void main()
{
    vec4 _50 = texture(baseMap, vec3(fragTexCoord.x, fragTexCoord.y / fragTexCoord.w, fragTexCoord.z));
    vec3 _92 = pow(_50.xyz, vec3(2.2000000476837158203125));
    vec4 _98 = _50;
    _98.x = _92.x;
    vec4 _100 = _98;
    _100.y = _92.y;
    vec4 _102 = _100;
    _102.z = _92.z;
    fragOut0 = (_102 * vec4(pow(fragColor.xyz, vec3(2.2000000476837158203125)), fragColor.w)) * _81.intensity;
}


#version 150

layout(std140) uniform genericData
{
    vec3 fog_color;
    float fog_start;
    float fog_density;
    float zNear;
    float zFar;
    float pad0;
} _46;

uniform sampler2D tex;
uniform sampler2D depth_tex;

in vec2 fragTexCoord;
out vec4 fragOut0;

void main()
{
    vec4 _33 = texture(tex, fragTexCoord);
    vec4 _39 = texture(depth_tex, fragTexCoord);
    float _66 = (_46.zNear * _46.zFar) / (_46.zFar - (_39.x * (_46.zFar - _46.zNear)));
    if (isinf(_66))
    {
        fragOut0.x = _33.x;
        fragOut0.y = _33.y;
        fragOut0.z = _33.z;
    }
    else
    {
        vec3 _112 = mix(_33.xyz, pow(_46.fog_color, vec3(2.2000000476837158203125)), vec3(clamp(1.0 - pow(_46.fog_density, _66 - _46.fog_start), 0.0, 1.0)));
        fragOut0.x = _112.x;
        fragOut0.y = _112.y;
        fragOut0.z = _112.z;
    }
    fragOut0.w = 1.0;
}


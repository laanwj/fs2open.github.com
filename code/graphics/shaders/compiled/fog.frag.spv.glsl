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
    float _66 = (_46.zNear * _46.zFar) / (_46.zFar - (texture(depth_tex, fragTexCoord).x * (_46.zFar - _46.zNear)));
    vec3 _102 = mix(texture(tex, fragTexCoord).xyz, pow(_46.fog_color, vec3(2.2000000476837158203125)), vec3(clamp(1.0 - pow(_46.fog_density, (isinf(_66) ? _46.zFar : _66) - _46.fog_start), 0.0, 1.0)));
    fragOut0.x = _102.x;
    fragOut0.y = _102.y;
    fragOut0.z = _102.z;
    fragOut0.w = 1.0;
}


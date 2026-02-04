#version 150

layout(std140) uniform movieData
{
    float alpha;
    float pad[3];
} _87;

uniform sampler2DArray ytex;
uniform sampler2DArray utex;
uniform sampler2DArray vtex;

in vec4 fragTexCoord;
out vec4 fragOut0;

void main()
{
    vec3 _25 = vec3(fragTexCoord.xy, 0.0);
    vec3 _60 = vec3(texture(ytex, _25).x - 0.0625, texture(utex, _25).x - 0.5, texture(vtex, _25).x - 0.5);
    fragOut0.x = dot(_60, vec3(1.1640625, 0.0, 1.59765625));
    fragOut0.y = dot(_60, vec3(1.1640625, -0.390625, -0.8125));
    fragOut0.z = dot(_60, vec3(1.1640625, 2.015625, 0.0));
    fragOut0.w = _87.alpha;
}


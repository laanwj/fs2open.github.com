#version 150

layout(std140) uniform movieData
{
    float alpha;
    float pad[3];
} _95;

uniform sampler2DArray textures[16];

in vec4 fragTexCoord;
out vec4 fragOut0;

void main()
{
    vec3 _32 = vec3(fragTexCoord.xy, 0.0);
    vec3 _68 = vec3(texture(textures[0], _32).x - 0.0625, texture(textures[1], _32).x - 0.5, texture(textures[2], _32).x - 0.5);
    fragOut0.x = dot(_68, vec3(1.1640625, 0.0, 1.59765625));
    fragOut0.y = dot(_68, vec3(1.1640625, -0.390625, -0.8125));
    fragOut0.z = dot(_68, vec3(1.1640625, 2.015625, 0.0));
    fragOut0.w = _95.alpha;
}


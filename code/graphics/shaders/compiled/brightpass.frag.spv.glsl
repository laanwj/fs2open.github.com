#version 150

uniform sampler2D tex;

in vec2 fragTexCoord;
out vec4 fragOut0;

void main()
{
    fragOut0 = vec4(max(vec3(0.0), texture(tex, fragTexCoord).xyz - vec3(1.0)), 1.0);
}


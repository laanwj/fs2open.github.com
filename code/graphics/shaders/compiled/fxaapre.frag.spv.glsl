#version 150

uniform sampler2D tex;

in vec2 fragTexCoord;
out vec4 fragOut0;

void main()
{
    vec4 _20 = texture(tex, fragTexCoord);
    fragOut0 = vec4(_20.xyz, dot(_20.xyz, vec3(0.2989999949932098388671875, 0.58700001239776611328125, 0.114000000059604644775390625)));
}


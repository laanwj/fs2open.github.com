#version 150

out vec2 fragTexCoord;

void main()
{
    vec2 _23 = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    fragTexCoord = _23;
    gl_Position = vec4((_23 * 2.0) - vec2(1.0), 0.0, 1.0);
}


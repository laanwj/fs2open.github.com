#version 150

out vec4 fragColor;

void main()
{
    fragColor = vec4(gl_FragCoord.z, (gl_FragCoord.z * gl_FragCoord.z) * 9.9999999747524270787835121154785e-07, 0.0, 1.0);
}


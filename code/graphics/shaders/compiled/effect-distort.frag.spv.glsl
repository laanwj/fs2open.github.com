#version 150

layout(std140) uniform GenericData
{
    float window_width;
    float window_height;
    float use_offset;
    float pad;
} _21;

uniform sampler2D distMap;
uniform sampler2DArray baseMap;
uniform sampler2D frameBuffer;

in vec4 fragTexCoord;
in float fragOffset;
in vec4 fragColor;
out vec4 fragOut0;

void main()
{
    float _78 = clamp(dot((texture(baseMap, fragTexCoord.xyz) * fragColor.w).xyz, vec3(0.33329999446868896484375)) * 10.0, 0.0, 1.0);
    fragOut0 = texture(frameBuffer, vec2(gl_FragCoord.x / _21.window_width, gl_FragCoord.y / _21.window_height) + (((texture(distMap, fragTexCoord.xy + vec2(0.0, fragOffset)).xy - vec2(0.5)) * 0.00999999977648258209228515625) * _78));
    fragOut0.w = _78;
}


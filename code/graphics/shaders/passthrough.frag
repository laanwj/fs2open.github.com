#version 450
#extension GL_ARB_separate_shader_objects : enable

#include "gamma.sdr"

// Inputs from vertex shader
layout (location = 0) in vec4 fragTexCoord;
layout (location = 1) in vec4 fragColor;

// Output
layout (location = 0) out vec4 fragOut0;

// Texture sampler (binding 0 in Material set texture array)
layout (set = 1, binding = 1) uniform sampler2D baseMap;

// Uniform buffer: GenericData (binding 0 in PerDraw set)
layout (set = 2, binding = 0, std140) uniform genericData {
	int noTexturing;
	int srgb;
	float pad[2];
};

void main()
{
	vec4 baseColor = texture(baseMap, fragTexCoord.xy);

	baseColor.rgb = (srgb == 1) ? srgb_to_linear(baseColor.rgb) : baseColor.rgb;
	vec4 blendColor = (srgb == 1) ? vec4(srgb_to_linear(fragColor.rgb), fragColor.a) : fragColor;
	fragOut0 = mix(baseColor * blendColor, blendColor, float(noTexturing));
}

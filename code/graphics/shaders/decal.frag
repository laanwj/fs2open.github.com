#version 450
#extension GL_ARB_separate_shader_objects : enable

// Decal fragment shader - simple texture/color rendering
layout (location = 0) in vec4 fragTexCoord;
layout (location = 1) in vec4 fragColor;

layout (location = 0) out vec4 fragOut0;

// Set 2 = PerDraw, Binding 0 = GenericData
layout (set = 2, binding = 0, std140) uniform genericData {
	mat4 modelMatrix;
	vec4 color;
	vec4 clipEquation;
	int baseMapIndex;
	int alphaTexture;
	int noTexturing;
	int srgb;
	float intensity;
	float alphaThreshold;
	int clipEnabled;
};

// Set 1 = Material, Binding 1 = texture
layout (set = 1, binding = 1) uniform sampler2DArray baseMap;

void main()
{
	vec4 baseColor = texture(baseMap, vec3(fragTexCoord.xy, float(baseMapIndex)));

	if (alphaThreshold > baseColor.a) discard;

	if (noTexturing != 0) {
		fragOut0 = fragColor * intensity;
	} else {
		fragOut0 = baseColor * fragColor * intensity;
	}
}

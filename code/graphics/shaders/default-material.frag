#version 450
#extension GL_ARB_separate_shader_objects : enable

// Inline gamma conversion (from gamma.sdr)
const float SRGB_GAMMA = 2.2;
vec3 srgb_to_linear(vec3 val) {
	return pow(val, vec3(SRGB_GAMMA));
}

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
	uint clipEnabled;  // Use uint instead of bool for std140 compatibility
};

// Set 1 = Material, Binding 1 = textures (first texture in array is base map)
layout (set = 1, binding = 1) uniform sampler2D baseMap;

void main()
{
	// Sample base texture
	vec4 baseColor = texture(baseMap, fragTexCoord.xy);

	// Apply alpha threshold
	if (alphaThreshold > baseColor.a) discard;

	// Apply sRGB conversion if needed
	if (srgb == 1) {
		baseColor.rgb = srgb_to_linear(baseColor.rgb);
	}

	// Blend with material color
	vec4 blendColor = fragColor;
	if (srgb == 1) {
		blendColor.rgb = srgb_to_linear(blendColor.rgb);
	}

	// Mix based on texturing mode
	if (noTexturing != 0) {
		fragOut0 = blendColor * intensity;
	} else if (alphaTexture != 0) {
		fragOut0 = vec4(blendColor.rgb, baseColor.r * blendColor.a) * intensity;
	} else {
		fragOut0 = baseColor * blendColor * intensity;
	}
}

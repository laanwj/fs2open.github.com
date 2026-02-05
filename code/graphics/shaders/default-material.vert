#version 450
#extension GL_ARB_separate_shader_objects : enable

// Vertex inputs - color is optional (use uniform color instead)
layout (location = 0) in vec4 vertPosition;
layout (location = 2) in vec4 vertTexCoord;

layout (location = 0) out vec4 fragTexCoord;
layout (location = 1) out vec4 fragColor;

// Set 2 = PerDraw, Binding 1 = Matrices
layout (set = 2, binding = 1, std140) uniform matrixData {
	mat4 modelViewMatrix;
	mat4 projMatrix;
};

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

void main()
{
	fragTexCoord = vertTexCoord;
	fragColor = color;

	gl_Position = projMatrix * modelViewMatrix * vertPosition;

	if (clipEnabled != 0u) {
		gl_ClipDistance[0] = dot(clipEquation, modelMatrix * vertPosition);
	}
}

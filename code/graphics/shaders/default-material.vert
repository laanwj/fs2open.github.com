#version 450
#extension GL_ARB_separate_shader_objects : enable

// Vertex inputs - color is optional (use uniform color instead)
layout (location = 0) in vec4 vertPosition;
layout (location = 2) in vec4 vertTexCoord;

layout (location = 0) out vec4 fragTexCoord;
layout (location = 1) out vec4 fragColor;
layout (location = 2) out float debugMatrixVal;

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
	fragColor = color;  // Use uniform color instead of vertex color
	debugMatrixVal = 0.5;  // Not used anymore

	// WORKAROUND: Matrix uniforms aren't working correctly yet
	// Convert screen coordinates (0-width, 0-height) to NDC (-1 to 1)
	// FSO uses Y=0 at top, same as Vulkan, so no Y flip needed
	vec2 screenSize = vec2(1920.0, 1080.0);
	vec2 ndc = (vertPosition.xy / screenSize) * 2.0 - 1.0;
	gl_Position = vec4(ndc.x, ndc.y, 0.0, 1.0);
}

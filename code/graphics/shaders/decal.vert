#version 450
#extension GL_ARB_separate_shader_objects : enable

// Decal vertex shader - simple passthrough
layout (location = 0) in vec4 vertPosition;
layout (location = 2) in vec4 vertTexCoord;

layout (location = 0) out vec4 fragTexCoord;
layout (location = 1) out vec4 fragColor;

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

void main()
{
	fragTexCoord = vertTexCoord;
	fragColor = color;

	// Simple screen coordinate to NDC conversion
	vec2 screenSize = vec2(1920.0, 1080.0);
	vec2 ndc = (vertPosition.xy / screenSize) * 2.0 - 1.0;
	gl_Position = vec4(ndc.x, ndc.y, 0.0, 1.0);
}

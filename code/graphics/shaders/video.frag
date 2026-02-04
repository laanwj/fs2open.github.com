#version 450
#extension GL_ARB_separate_shader_objects : enable

// Inputs from vertex shader
layout (location = 0) in vec4 fragTexCoord;

// Output
layout (location = 0) out vec4 fragOut0;

// YUV texture samplers (in Material set)
layout (set = 1, binding = 1) uniform sampler2DArray ytex;
layout (set = 1, binding = 2) uniform sampler2DArray utex;
layout (set = 1, binding = 3) uniform sampler2DArray vtex;

// Uniform buffer: MovieData (binding 4 in PerDraw set)
layout (set = 2, binding = 4, std140) uniform movieData {
	float alpha;
	float pad[3];
};

void main()
{
	float y = texture(ytex, vec3(fragTexCoord.st, 0.0)).r;
	float u = texture(utex, vec3(fragTexCoord.st, 0.0)).r;
	float v = texture(vtex, vec3(fragTexCoord.st, 0.0)).r;
	vec3 val = vec3(y - 0.0625, u - 0.5, v - 0.5);
	fragOut0.r = dot(val, vec3(1.1640625, 0.0, 1.59765625));
	fragOut0.g = dot(val, vec3(1.1640625, -0.390625, -0.8125));
	fragOut0.b = dot(val, vec3(1.1640625, 2.015625, 0.0));
	fragOut0.a = alpha;
}

#version 450
#extension GL_ARB_separate_shader_objects : enable
#include "lighting.sdr"
#include "gamma.sdr"

layout(location = 0) out vec4 fragOut0;

layout(set = 1, binding = 1) uniform sampler2D sTextures[16];
// sTextures[0] = ColorBuffer
// sTextures[1] = NormalBuffer
// sTextures[2] = PositionBuffer
// sTextures[3] = SpecBuffer

layout(set = 0, binding = 0, std140) uniform lightData {
	vec3 diffuseLightColor;
	float coneAngle;

	vec3 lightDir;
	float coneInnerAngle;

	vec3 coneDir;
	float dualCone;

	vec3 scale;
	float lightRadius;

	int lightType;
	int enable_shadows;
	float sourceRadius;

	float pad0;
};

layout(set = 0, binding = 1, std140) uniform globalDeferredData {
	mat4 shadow_mv_matrix;
	mat4 shadow_proj_matrix[4];

	mat4 inv_view_matrix;

	float veryneardist;
	float neardist;
	float middist;
	float fardist;

	float invScreenWidth;
	float invScreenHeight;

	float nearPlane;

	float globalPad;
};

layout(set = 2, binding = 1, std140) uniform matrixData {
	mat4 modelViewMatrix;
	mat4 projMatrix;
};

// Nearest point sphere and tube light calculations taken from
// "Real Shading in Unreal Engine 4" by Brian Karis, Epic Games
// Part of SIGGRAPH 2013 Course: Physically Based Shading in Theory and Practice

vec3 ExpandLightSize(in vec3 lightDirIn, in vec3 reflectDir) {
	vec3 centerToRay = max(dot(lightDirIn, reflectDir),sourceRadius) * reflectDir - lightDirIn;
	return lightDirIn + centerToRay * clamp(sourceRadius/length(centerToRay), 0.0, 1.0);
}

void GetLightInfo(vec3 position, in float alpha, in vec3 reflectDir, out vec3 lightDirOut, out float attenuation, out float area_normalisation)
{
	if (lightType == LT_DIRECTIONAL) {
		lightDirOut = normalize(lightDir);
		attenuation = 1.0;
		area_normalisation = 1.0;
	} else {
		vec3 lightPosition = modelViewMatrix[3].xyz;
		if (lightType == LT_POINT) {
			lightDirOut = lightPosition - position.xyz;
			float dist = length(lightDirOut);

			lightDirOut = ExpandLightSize(lightDirOut, reflectDir);
			dist = length(lightDirOut);
			float alpha_adjust = clamp(alpha + (sourceRadius/(2*dist)), 0.0, 1.0);
			area_normalisation = alpha/alpha_adjust;
			area_normalisation *= area_normalisation;

			if(dist > lightRadius) {
				discard;
			}
			attenuation = 1.0 - clamp(sqrt(dist / lightRadius), 0.0, 1.0);
		}
		else if (lightType == LT_TUBE) {
			vec3 beamVec = vec3(modelViewMatrix * vec4(0.0, 0.0, -scale.z, 0.0));
			vec3 beamDir = normalize(beamVec);
			vec3 adjustedLightPos = lightPosition - (beamDir * lightRadius);
			vec3 adjustedbeamVec = beamVec - 2.0 * lightRadius * beamDir;
			float beamLength = length(adjustedbeamVec);
			vec3 sourceDir = adjustedLightPos - position.xyz;

			vec3 a_t = reflectDir;
			vec3 b_t = beamDir;
			vec3 b_0 = sourceDir;
			vec3 c = cross(a_t, b_t);
			vec3 d = b_0;
			vec3 r = d - a_t * dot(d, a_t) - c * dot(d,c);
			float tubeneardist = dot(r, r)/dot(b_t, r);
			lightDirOut = sourceDir - beamDir * clamp(tubeneardist, 0.0, beamLength);

			lightDirOut = ExpandLightSize(lightDirOut, reflectDir);
			float dist = length(lightDirOut);
			float alpha_adjust = min(alpha + (sourceRadius/(2*dist)), 1.0);
			area_normalisation = alpha/alpha_adjust;

			if(dist > lightRadius) {
				discard;
			}
			attenuation = 1.0 - clamp(sqrt(dist / lightRadius), 0.0, 1.0);
		}
		else if (lightType == LT_CONE) {
			lightDirOut = lightPosition - position.xyz;
			float coneDot = dot(normalize(-lightDirOut), coneDir);
			float dist = length(lightDirOut);
			attenuation = 1.0 - clamp(sqrt(dist / lightRadius), 0.0, 1.0);
			area_normalisation = 1.0;

			if(dualCone > 0.5) {
				if(abs(coneDot) < coneAngle) {
					discard;
				} else {
					attenuation *= smoothstep(coneAngle, coneInnerAngle, abs(coneDot));
				}
			} else {
				if (coneDot < coneAngle) {
					discard;
				} else {
					attenuation *= smoothstep(coneAngle, coneInnerAngle, coneDot);
				}
			}
		}
		attenuation *= attenuation;
		lightDirOut = normalize(lightDirOut);
	}
}

void main()
{
	vec2 screenPos = gl_FragCoord.xy * vec2(invScreenWidth, invScreenHeight);
	vec4 position_buffer = texture(sTextures[2], screenPos);
	vec3 position = position_buffer.xyz;

	if(abs(dot(position, position)) < nearPlane * nearPlane)
		discard;

	vec4 diffuse = texture(sTextures[0], screenPos);
	vec3 diffColor = diffuse.rgb;
	vec4 normalData = texture(sTextures[1], screenPos);
	vec3 normal = normalize(normalData.xyz);
	float gloss = normalData.a;
	float roughness = clamp(1.0f - gloss, 0.0f, 1.0f);
	float alpha = roughness * roughness;
	vec3 eyeDir = normalize(-position);
	vec3 reflectDir = reflect(-eyeDir, normal);
	vec4 specColor = texture(sTextures[3], screenPos);

	vec4 fragmentColor = vec4(1.0);

	if (lightType == LT_AMBIENT) {
		float ao = position_buffer.w;
		fragmentColor.rgb = diffuseLightColor * diffColor * ao;
	}
	else {
		float fresnel = specColor.a;

		vec3 lightDirCalc;
		float attenuation;
		float area_normalisation;
		GetLightInfo(position, alpha, reflectDir, lightDirCalc, attenuation, area_normalisation);

		vec3 halfVec = normalize(lightDirCalc + eyeDir);
		float NdotL = clamp(dot(normal, lightDirCalc), 0.0, 1.0);
		fragmentColor.rgb = computeLighting(specColor.rgb, diffColor, lightDirCalc, normal.xyz, halfVec, eyeDir, roughness, fresnel, NdotL).rgb * diffuseLightColor * attenuation * area_normalisation;
	}

	fragOut0 = max(fragmentColor, vec4(0.0));
}


#pragma once

#include <cstdint>
#include <array>

struct decalGlobalData_decal_frag {
	SPIRV_FLOAT_MAT_4x4 viewMatrix;
	SPIRV_FLOAT_MAT_4x4 projMatrix;
	SPIRV_FLOAT_MAT_4x4 invViewMatrix;
	SPIRV_FLOAT_MAT_4x4 invProjMatrix;
	SPIRV_FLOAT_VEC2 viewportSize;
};
static_assert(sizeof(decalGlobalData_decal_frag) == 264, "Size of struct decalGlobalData_decal_frag does not match what is expected for the uniform block!");
static_assert(offsetof(decalGlobalData_decal_frag, viewMatrix) == 0, "Offset of member viewMatrix does not match the uniform buffer offset!");
static_assert(offsetof(decalGlobalData_decal_frag, projMatrix) == 64, "Offset of member projMatrix does not match the uniform buffer offset!");
static_assert(offsetof(decalGlobalData_decal_frag, invViewMatrix) == 128, "Offset of member invViewMatrix does not match the uniform buffer offset!");
static_assert(offsetof(decalGlobalData_decal_frag, invProjMatrix) == 192, "Offset of member invProjMatrix does not match the uniform buffer offset!");
static_assert(offsetof(decalGlobalData_decal_frag, viewportSize) == 256, "Offset of member viewportSize does not match the uniform buffer offset!");
struct decalInfoData_decal_frag {
	std::int32_t diffuse_index;
	std::int32_t glow_index;
	std::int32_t normal_index;
	std::int32_t diffuse_blend_mode;
	std::int32_t glow_blend_mode;
};
static_assert(sizeof(decalInfoData_decal_frag) == 20, "Size of struct decalInfoData_decal_frag does not match what is expected for the uniform block!");
static_assert(offsetof(decalInfoData_decal_frag, diffuse_index) == 0, "Offset of member diffuse_index does not match the uniform buffer offset!");
static_assert(offsetof(decalInfoData_decal_frag, glow_index) == 4, "Offset of member glow_index does not match the uniform buffer offset!");
static_assert(offsetof(decalInfoData_decal_frag, normal_index) == 8, "Offset of member normal_index does not match the uniform buffer offset!");
static_assert(offsetof(decalInfoData_decal_frag, diffuse_blend_mode) == 12, "Offset of member diffuse_blend_mode does not match the uniform buffer offset!");
static_assert(offsetof(decalInfoData_decal_frag, glow_blend_mode) == 16, "Offset of member glow_blend_mode does not match the uniform buffer offset!");

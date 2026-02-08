#include "VulkanShader.h"
#include "def_files/def_files.h"

namespace graphics {
namespace vulkan {

// Global shader manager pointer
static VulkanShaderManager* g_shaderManager = nullptr;

VulkanShaderManager* getShaderManager()
{
	Assertion(g_shaderManager != nullptr, "Vulkan ShaderManager not initialized!");
	return g_shaderManager;
}

void setShaderManager(VulkanShaderManager* manager)
{
	g_shaderManager = manager;
}

// ========== gr_screen function pointer implementations ==========

int vulkan_maybe_create_shader(shader_type shader_t, unsigned int flags)
{
	auto* shaderManager = getShaderManager();
	return shaderManager->maybeCreateShader(shader_t, flags);
}

void vulkan_recompile_all_shaders(const std::function<void(size_t, size_t)>& progressCallback)
{
	auto* shaderManager = getShaderManager();
	shaderManager->recompileAllShaders(progressCallback);
}

// Shader type definitions - maps shader_type to SPIR-V filenames
// Based on GL_shader_types in gropenglshader.cpp
// Filenames match the compiled SPIR-V files: {basename}.{stage}.spv
const VulkanShaderTypeInfo VULKAN_SHADER_TYPES[] = {
	{ SDR_TYPE_MODEL,                              "main",           "main",           nullptr,         "Model rendering" },
	{ SDR_TYPE_EFFECT_PARTICLE,                    "effect",         "effect",         "effect",        "Particle effects" },
	{ SDR_TYPE_EFFECT_DISTORTION,                  "effect-distort", "effect-distort", nullptr,         "Distortion effects" },
	{ SDR_TYPE_POST_PROCESS_MAIN,                  "post",           "post",           nullptr,         "Post-processing main" },
	{ SDR_TYPE_POST_PROCESS_BLUR,                  "blur",           "blur",           nullptr,         "Gaussian blur" },
	{ SDR_TYPE_POST_PROCESS_BLOOM_COMP,            "bloom-comp",     "bloom-comp",     nullptr,         "Bloom composition" },
	{ SDR_TYPE_POST_PROCESS_BRIGHTPASS,            "brightpass",     "brightpass",     nullptr,         "Bright pass filter" },
	{ SDR_TYPE_POST_PROCESS_FXAA,                  "fxaa",           "fxaa",           nullptr,         "FXAA anti-aliasing" },
	{ SDR_TYPE_POST_PROCESS_FXAA_PREPASS,          "fxaapre",        "fxaapre",        nullptr,         "FXAA luma prepass" },
	{ SDR_TYPE_POST_PROCESS_LIGHTSHAFTS,           "lightshafts",    "lightshafts",    nullptr,         "Light shafts" },
	{ SDR_TYPE_POST_PROCESS_TONEMAPPING,           "tonemapping",    "tonemapping",    nullptr,         "Tonemapping" },
	{ SDR_TYPE_DEFERRED_LIGHTING,                  "deferred",       "deferred",       nullptr,         "Deferred lighting" },
	{ SDR_TYPE_DEFERRED_CLEAR,                     "deferred-clear", "deferred-clear", nullptr,         "Deferred clear" },
	{ SDR_TYPE_VIDEO_PROCESS,                      "video",          "video",          nullptr,         "Video playback" },
	{ SDR_TYPE_PASSTHROUGH_RENDER,                 "passthrough",    "passthrough",    nullptr,         "Passthrough rendering" },
	{ SDR_TYPE_SHIELD_DECAL,                       "shield-impact",  "shield-impact",  nullptr,         "Shield impact" },
	{ SDR_TYPE_BATCHED_BITMAP,                     "batched",        "batched",        nullptr,         "Batched bitmaps" },
	{ SDR_TYPE_DEFAULT_MATERIAL,                   "default-material", "default-material", nullptr,     "Default material" },
	{ SDR_TYPE_NANOVG,                             "nanovg",         "nanovg",         nullptr,         "NanoVG UI" },
	{ SDR_TYPE_DECAL,                              "decal",          "decal",          nullptr,         "Decals" },
	{ SDR_TYPE_SCENE_FOG,                          "fog",            "fog",            nullptr,         "Scene fog" },
	{ SDR_TYPE_VOLUMETRIC_FOG,                     "volumetric-fog", "volumetric-fog", nullptr,         "Volumetric fog" },
	{ SDR_TYPE_ROCKET_UI,                          "rocketui",       "rocketui",       nullptr,         "Rocket UI" },
	{ SDR_TYPE_COPY,                               "copy",           "copy",           nullptr,         "Texture copy" },
	{ SDR_TYPE_COPY_WORLD,                         "copy-world",     "copy-world",     nullptr,         "World copy" },
	{ SDR_TYPE_MSAA_RESOLVE,                       "msaa-resolve",   "msaa-resolve",   nullptr,         "MSAA resolve" },
	{ SDR_TYPE_POST_PROCESS_SMAA_EDGE,             "smaa-edge",      "smaa-edge",      nullptr,         "SMAA edge detection" },
	{ SDR_TYPE_POST_PROCESS_SMAA_BLENDING_WEIGHT,  "smaa-blend",     "smaa-blend",     nullptr,         "SMAA blending weight" },
	{ SDR_TYPE_POST_PROCESS_SMAA_NEIGHBORHOOD_BLENDING, "smaa-neighbor", "smaa-neighbor", nullptr,      "SMAA neighborhood blending" },
	{ SDR_TYPE_ENVMAP_SPHERE_WARP,                 "envmap-warp",    "envmap-warp",    nullptr,         "Environment map warp" },
	{ SDR_TYPE_IRRADIANCE_MAP_GEN,                 "irradiance",     "irradiance",     nullptr,         "Irradiance map generation" },
};

const size_t VULKAN_SHADER_TYPES_COUNT = sizeof(VULKAN_SHADER_TYPES) / sizeof(VULKAN_SHADER_TYPES[0]);

// Shader variant definitions - maps flags to shader variations
// Based on GL_shader_variants in gropenglshader.cpp
const VulkanShaderVariantInfo VULKAN_SHADER_VARIANTS[] = {
	// Particle variants
	{ SDR_TYPE_EFFECT_PARTICLE, SDR_FLAG_PARTICLE_POINT_GEN, "USE_POINT_GEN", true, "Point sprite generation" },

	// Blur variants
	{ SDR_TYPE_POST_PROCESS_BLUR, SDR_FLAG_BLUR_HORIZONTAL, "BLUR_HORIZONTAL", false, "Horizontal blur" },
	{ SDR_TYPE_POST_PROCESS_BLUR, SDR_FLAG_BLUR_VERTICAL, "BLUR_VERTICAL", false, "Vertical blur" },

	// NanoVG variants
	{ SDR_TYPE_NANOVG, SDR_FLAG_NANOVG_EDGE_AA, "EDGE_AA", false, "Edge anti-aliasing" },

	// Decal variants
	{ SDR_TYPE_DECAL, SDR_FLAG_DECAL_USE_NORMAL_MAP, "USE_NORMAL_MAP", false, "Normal mapping" },

	// MSAA resolve variants
	{ SDR_TYPE_MSAA_RESOLVE, SDR_FLAG_MSAA_SAMPLES_4, "MSAA_SAMPLES_4", false, "4x MSAA" },
	{ SDR_TYPE_MSAA_RESOLVE, SDR_FLAG_MSAA_SAMPLES_8, "MSAA_SAMPLES_8", false, "8x MSAA" },
	{ SDR_TYPE_MSAA_RESOLVE, SDR_FLAG_MSAA_SAMPLES_16, "MSAA_SAMPLES_16", false, "16x MSAA" },

	// Volumetric fog variants
	{ SDR_TYPE_VOLUMETRIC_FOG, SDR_FLAG_VOLUMETRICS_DO_EDGE_SMOOTHING, "EDGE_SMOOTHING", false, "Edge smoothing" },
	{ SDR_TYPE_VOLUMETRIC_FOG, SDR_FLAG_VOLUMETRICS_NOISE, "NOISE", false, "Noise" },

	// Copy variants
	{ SDR_TYPE_COPY, SDR_FLAG_COPY_FROM_ARRAY, "FROM_ARRAY", false, "Copy from array" },

	// Tonemapping variants
	{ SDR_TYPE_POST_PROCESS_TONEMAPPING, SDR_FLAG_TONEMAPPING_LINEAR_OUT, "LINEAR_OUT", false, "Linear output" },

	// Deferred variants
	{ SDR_TYPE_DEFERRED_LIGHTING, SDR_FLAG_ENV_MAP, "ENV_MAP", false, "Environment mapping" },
};

const size_t VULKAN_SHADER_VARIANTS_COUNT = sizeof(VULKAN_SHADER_VARIANTS) / sizeof(VULKAN_SHADER_VARIANTS[0]);

bool VulkanShaderManager::init(vk::Device device)
{
	if (m_initialized) {
		return true;
	}

	m_device = device;
	m_initialized = true;

	mprintf(("VulkanShaderManager: Initialized\n"));
	return true;
}

void VulkanShaderManager::shutdown()
{
	if (!m_initialized) {
		return;
	}

	// Clear all shaders (unique_ptrs will clean up)
	m_shaders.clear();
	m_shaderMap.clear();
	m_freeSlots.clear();

	m_initialized = false;
	mprintf(("VulkanShaderManager: Shutdown complete\n"));
}

int VulkanShaderManager::maybeCreateShader(shader_type type, unsigned int flags)
{
	if (!m_initialized) {
		return -1;
	}

	// Check if shader already exists
	ShaderKey key{type, flags};
	auto it = m_shaderMap.find(key);
	if (it != m_shaderMap.end()) {
		// Return existing shader handle
		return static_cast<int>(it->second);
	}

	// Create new shader
	return compileShader(type, flags);
}

void VulkanShaderManager::recompileAllShaders(const std::function<void(size_t, size_t)>& progressCallback)
{
	if (!m_initialized) {
		return;
	}

	size_t total = m_shaders.size();
	size_t current = 0;

	for (auto& shader : m_shaders) {
		if (shader.valid) {
			// Re-compile this shader
			shader_type type = shader.type;
			unsigned int flags = shader.flags;

			// Release old modules
			shader.vertexModule.reset();
			shader.fragmentModule.reset();
			shader.geometryModule.reset();
			shader.valid = false;

			// Get shader type info
			const VulkanShaderTypeInfo* typeInfo = getShaderTypeInfo(type);
			if (typeInfo) {
				// Load vertex shader
				SCP_string vertFile = buildShaderFilename(typeInfo->vertexFile, flags, "vert");
				shader.vertexModule = loadSpirvModule(vertFile);

				// Load fragment shader
				SCP_string fragFile = buildShaderFilename(typeInfo->fragmentFile, flags, "frag");
				shader.fragmentModule = loadSpirvModule(fragFile);

				// Load geometry shader if needed
				if (typeInfo->geometryFile) {
					SCP_string geomFile = buildShaderFilename(typeInfo->geometryFile, flags, "geom");
					shader.geometryModule = loadSpirvModule(geomFile);
				}

				shader.valid = shader.vertexModule && shader.fragmentModule;
			}
		}

		++current;
		if (progressCallback) {
			progressCallback(current, total);
		}
	}

	mprintf(("VulkanShaderManager: Recompiled %zu shaders\n", total));
}

const VulkanShaderModule* VulkanShaderManager::getShader(int handle) const
{
	if (handle < 0 || static_cast<size_t>(handle) >= m_shaders.size()) {
		return nullptr;
	}

	const VulkanShaderModule& shader = m_shaders[handle];
	return shader.valid ? &shader : nullptr;
}

const VulkanShaderModule* VulkanShaderManager::getShaderByType(shader_type type, unsigned int flags) const
{
	ShaderKey key{type, flags};
	auto it = m_shaderMap.find(key);
	if (it == m_shaderMap.end()) {
		return nullptr;
	}

	return getShader(static_cast<int>(it->second));
}

bool VulkanShaderManager::isShaderTypeSupported(shader_type type) const
{
	return getShaderTypeInfo(type) != nullptr;
}

vk::UniqueShaderModule VulkanShaderManager::loadSpirvModule(const SCP_string& filename)
{
	// Try to load from def_files
	SCP_string fullName = filename + ".spv";

	const auto def_file = defaults_try_get_file(fullName.c_str());
	if (def_file.data == nullptr || def_file.size == 0) {
		mprintf(("VulkanShaderManager: Could not load SPIR-V file: %s\n", fullName.c_str()));
		return {};
	}

	// Validate SPIR-V magic number
	if (def_file.size < 4) {
		mprintf(("VulkanShaderManager: SPIR-V file too small: %s\n", fullName.c_str()));
		return {};
	}

	const uint32_t* spirvData = static_cast<const uint32_t*>(def_file.data);
	if (spirvData[0] != 0x07230203) {
		mprintf(("VulkanShaderManager: Invalid SPIR-V magic number in: %s\n", fullName.c_str()));
		return {};
	}

	vk::ShaderModuleCreateInfo createInfo;
	createInfo.codeSize = def_file.size;
	createInfo.pCode = spirvData;

	try {
		auto module = m_device.createShaderModuleUnique(createInfo);
		mprintf(("VulkanShaderManager: Loaded SPIR-V: %s (size=%zu)\n", fullName.c_str(), def_file.size));
		return module;
	} catch (const vk::SystemError& e) {
		mprintf(("VulkanShaderManager: Failed to create shader module from %s: %s\n",
			fullName.c_str(), e.what()));
		return {};
	}
}

int VulkanShaderManager::compileShader(shader_type type, unsigned int flags)
{
	const VulkanShaderTypeInfo* typeInfo = getShaderTypeInfo(type);
	if (!typeInfo) {
		mprintf(("VulkanShaderManager: Unknown shader type: %d\n", static_cast<int>(type)));
		return -1;
	}

	VulkanShaderModule shader;
	shader.type = type;
	shader.flags = flags;
	shader.description = typeInfo->description;

	// Build filenames with variant suffix
	SCP_string vertFile = buildShaderFilename(typeInfo->vertexFile, flags, "vert");
	SCP_string fragFile = buildShaderFilename(typeInfo->fragmentFile, flags, "frag");

	// Load vertex shader
	shader.vertexModule = loadSpirvModule(vertFile);
	if (!shader.vertexModule) {
		// Try base filename without variant suffix
		vertFile = SCP_string(typeInfo->vertexFile) + ".vert";
		shader.vertexModule = loadSpirvModule(vertFile);
	}

	// Load fragment shader
	shader.fragmentModule = loadSpirvModule(fragFile);
	if (!shader.fragmentModule) {
		fragFile = SCP_string(typeInfo->fragmentFile) + ".frag";
		shader.fragmentModule = loadSpirvModule(fragFile);
	}

	// Load geometry shader if specified
	if (typeInfo->geometryFile) {
		// Check if any variant flag enables geometry shader
		bool useGeom = false;
		for (size_t i = 0; i < VULKAN_SHADER_VARIANTS_COUNT; ++i) {
			if (VULKAN_SHADER_VARIANTS[i].type == type &&
			    (flags & VULKAN_SHADER_VARIANTS[i].flag) &&
			    VULKAN_SHADER_VARIANTS[i].usesGeometryShader) {
				useGeom = true;
				break;
			}
		}

		if (useGeom) {
			SCP_string geomFile = buildShaderFilename(typeInfo->geometryFile, flags, "geom");
			shader.geometryModule = loadSpirvModule(geomFile);
			if (!shader.geometryModule) {
				geomFile = SCP_string(typeInfo->geometryFile) + ".geom";
				shader.geometryModule = loadSpirvModule(geomFile);
			}
		}
	}

	// Check if essential modules loaded
	shader.valid = shader.vertexModule && shader.fragmentModule;

	if (!shader.valid) {
		mprintf(("VulkanShaderManager: Failed to load shader type %d flags 0x%x\n",
			static_cast<int>(type), flags));
		// Don't return -1 yet - we might want to track partial loads for debugging
	}

	// Find or allocate slot
	size_t index;
	if (!m_freeSlots.empty()) {
		index = m_freeSlots.back();
		m_freeSlots.pop_back();
		m_shaders[index] = std::move(shader);
	} else {
		index = m_shaders.size();
		m_shaders.push_back(std::move(shader));
	}

	// Add to lookup map
	ShaderKey key{type, flags};
	m_shaderMap[key] = index;

	if (m_shaders[index].valid) {
		nprintf(("Vulkan", "VulkanShaderManager: Created shader %zu: %s (flags 0x%x)\n",
			index, typeInfo->description, flags));
	}

	return static_cast<int>(index);
}

const VulkanShaderTypeInfo* VulkanShaderManager::getShaderTypeInfo(shader_type type) const
{
	for (size_t i = 0; i < VULKAN_SHADER_TYPES_COUNT; ++i) {
		if (VULKAN_SHADER_TYPES[i].type == type) {
			return &VULKAN_SHADER_TYPES[i];
		}
	}
	return nullptr;
}

SCP_string VulkanShaderManager::buildShaderFilename(const char* baseName, unsigned int flags, const char* stage)
{
	// For now, just use base name with stage extension
	// Future: could add flag suffixes for pre-compiled variants
	// e.g., "main-v_lit_deferred.vert.spv" for MODEL with LIGHT|DEFERRED flags
	SCP_string result = baseName;
	result += ".";
	result += stage;
	return result;
}

} // namespace vulkan
} // namespace graphics

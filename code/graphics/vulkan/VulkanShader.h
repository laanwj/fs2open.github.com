#pragma once

#include "globalincs/pstypes.h"
#include "graphics/2d.h"

#include <vulkan/vulkan.hpp>
#include <functional>

namespace graphics {
namespace vulkan {

/**
 * @brief Holds SPIR-V shader modules for a single shader program
 *
 * Corresponds to an OpenGL shader program (vertex + fragment + optional geometry).
 */
struct VulkanShaderModule {
	vk::UniqueShaderModule vertexModule;
	vk::UniqueShaderModule fragmentModule;
	vk::UniqueShaderModule geometryModule;  // May be null

	shader_type type = SDR_TYPE_NONE;
	unsigned int flags = 0;

	SCP_string description;
	bool valid = false;

	// Check if this shader uses geometry shader
	bool hasGeometryShader() const { return static_cast<bool>(geometryModule); }
};

/**
 * @brief Shader type definition - maps shader type to SPIR-V filenames
 *
 * Based on opengl_shader_type_t from gropenglshader.h
 */
struct VulkanShaderTypeInfo {
	shader_type type;
	const char* vertexFile;      // Vertex shader SPIR-V filename (without .spv)
	const char* fragmentFile;    // Fragment shader SPIR-V filename
	const char* geometryFile;    // Geometry shader filename (may be null)
	const char* description;
};

/**
 * @brief Shader variant definition - maps shader flags to defines
 *
 * Based on opengl_shader_variant_t from gropenglshader.h
 */
struct VulkanShaderVariantInfo {
	shader_type type;
	unsigned int flag;
	const char* flagDefine;    // SPIR-V specialization constant name
	bool usesGeometryShader;
	const char* description;
};

/**
 * @brief Key for shader lookup (type + flags combination)
 */
struct ShaderKey {
	shader_type type;
	unsigned int flags;

	bool operator==(const ShaderKey& other) const {
		return type == other.type && flags == other.flags;
	}
};

struct ShaderKeyHasher {
	size_t operator()(const ShaderKey& key) const {
		return std::hash<int>()(static_cast<int>(key.type)) ^
		       (std::hash<unsigned int>()(key.flags) << 8);
	}
};

/**
 * @brief Manages Vulkan shader modules (SPIR-V loading and caching)
 *
 * Provides the implementation for gr_screen.gf_maybe_create_shader and
 * gr_screen.gf_recompile_all_shaders function pointers.
 */
class VulkanShaderManager {
public:
	VulkanShaderManager() = default;
	~VulkanShaderManager() = default;

	// Non-copyable
	VulkanShaderManager(const VulkanShaderManager&) = delete;
	VulkanShaderManager& operator=(const VulkanShaderManager&) = delete;

	/**
	 * @brief Initialize the shader manager
	 * @param device Vulkan logical device
	 * @return true on success
	 */
	bool init(vk::Device device);

	/**
	 * @brief Shutdown and release all shader modules
	 */
	void shutdown();

	/**
	 * @brief Get or create a shader program
	 *
	 * Implements gr_screen.gf_maybe_create_shader
	 *
	 * @param type Shader type
	 * @param flags Shader variant flags
	 * @return Shader handle (index), or -1 on failure
	 */
	int maybeCreateShader(shader_type type, unsigned int flags);

	/**
	 * @brief Recompile all loaded shaders
	 *
	 * Implements gr_screen.gf_recompile_all_shaders
	 *
	 * @param progressCallback Called with (current, total) progress
	 */
	void recompileAllShaders(const std::function<void(size_t, size_t)>& progressCallback);

	/**
	 * @brief Get a shader by handle
	 * @param handle Shader handle from maybeCreateShader
	 * @return Pointer to shader module, or nullptr if invalid
	 */
	const VulkanShaderModule* getShader(int handle) const;

	/**
	 * @brief Get a shader by handle (alias for getShader)
	 */
	const VulkanShaderModule* getShaderByHandle(int handle) const { return getShader(handle); }

	/**
	 * @brief Get a shader by type and flags
	 * @param type Shader type
	 * @param flags Shader variant flags
	 * @return Pointer to shader module, or nullptr if not found
	 */
	const VulkanShaderModule* getShaderByType(shader_type type, unsigned int flags) const;

	/**
	 * @brief Get total number of loaded shaders
	 */
	size_t getShaderCount() const { return m_shaders.size(); }

	/**
	 * @brief Check if a shader type is supported
	 * @param type Shader type to check
	 * @return true if the shader type has SPIR-V files defined
	 */
	bool isShaderTypeSupported(shader_type type) const;

private:
	/**
	 * @brief Load a SPIR-V shader module from embedded files
	 * @param filename Base filename (e.g., "model.vert")
	 * @return Shader module, or empty unique_ptr on failure
	 */
	vk::UniqueShaderModule loadSpirvModule(const SCP_string& filename);

	/**
	 * @brief Compile a shader with given type and flags
	 * @param type Shader type
	 * @param flags Variant flags
	 * @return Index of new shader, or -1 on failure
	 */
	int compileShader(shader_type type, unsigned int flags);

	/**
	 * @brief Get shader type info for a shader type
	 * @param type Shader type
	 * @return Pointer to type info, or nullptr if not found
	 */
	const VulkanShaderTypeInfo* getShaderTypeInfo(shader_type type) const;

	/**
	 * @brief Build SPIR-V filename with variant suffix
	 * @param baseName Base shader name
	 * @param flags Variant flags
	 * @param stage Shader stage suffix (vert, frag, geom)
	 * @return Full filename with .spv extension
	 */
	SCP_string buildShaderFilename(const char* baseName, unsigned int flags, const char* stage);

	vk::Device m_device;

	// Shader lookup: (type, flags) -> index in m_shaders
	SCP_unordered_map<ShaderKey, size_t, ShaderKeyHasher> m_shaderMap;

	// All loaded shaders
	SCP_vector<VulkanShaderModule> m_shaders;

	// Free list for shader slot reuse
	SCP_vector<size_t> m_freeSlots;

	bool m_initialized = false;
};

// Global shader type definitions
extern const VulkanShaderTypeInfo VULKAN_SHADER_TYPES[];
extern const size_t VULKAN_SHADER_TYPES_COUNT;

// Global shader variant definitions
extern const VulkanShaderVariantInfo VULKAN_SHADER_VARIANTS[];
extern const size_t VULKAN_SHADER_VARIANTS_COUNT;

// Global shader manager access
VulkanShaderManager* getShaderManager();
void setShaderManager(VulkanShaderManager* manager);

// ========== gr_screen function pointer implementations ==========

int vulkan_maybe_create_shader(shader_type shader_t, unsigned int flags);
void vulkan_recompile_all_shaders(const std::function<void(size_t, size_t)>& progressCallback);

} // namespace vulkan
} // namespace graphics

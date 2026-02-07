#pragma once

#include "globalincs/pstypes.h"
#include "graphics/2d.h"
#include "VulkanConstants.h"

#include <vulkan/vulkan.hpp>

namespace graphics {
namespace vulkan {

/**
 * @brief Descriptor set indices for the 3-tier layout
 *
 * Set 0: Global - per-frame data (lights, deferred globals, shadow maps)
 * Set 1: Material - per-material data (model data, textures)
 * Set 2: Per-Draw - per-draw-call data (generic data, matrices, etc.)
 */
enum class DescriptorSetIndex : uint32_t {
	Global = 0,
	Material = 1,
	PerDraw = 2,

	Count = 3
};

/**
 * @brief Descriptor binding info for a single binding point
 */
struct DescriptorBindingInfo {
	uint32_t binding;
	vk::DescriptorType type;
	uint32_t count;
	vk::ShaderStageFlags stages;
};

/**
 * @brief Manages Vulkan descriptor sets, pools, and layouts
 *
 * Provides descriptor set allocation and update functionality.
 * Uses per-frame pools for transient descriptors and a persistent pool
 * for long-lived descriptors.
 */
class VulkanDescriptorManager {
public:
	static constexpr uint32_t MAX_TEXTURE_BINDINGS = 16;  // Texture array size

	VulkanDescriptorManager() = default;
	~VulkanDescriptorManager() = default;

	// Non-copyable
	VulkanDescriptorManager(const VulkanDescriptorManager&) = delete;
	VulkanDescriptorManager& operator=(const VulkanDescriptorManager&) = delete;

	/**
	 * @brief Initialize descriptor manager
	 * @param device Vulkan logical device
	 * @return true on success
	 */
	bool init(vk::Device device);

	/**
	 * @brief Shutdown and release resources
	 */
	void shutdown();

	/**
	 * @brief Get descriptor set layout for a given set index
	 */
	vk::DescriptorSetLayout getSetLayout(DescriptorSetIndex setIndex) const;

	/**
	 * @brief Get all descriptor set layouts (for pipeline layout creation)
	 * @return Vector of layouts in order (Global, Material, PerDraw)
	 */
	SCP_vector<vk::DescriptorSetLayout> getAllSetLayouts() const;

	/**
	 * @brief Allocate a descriptor set from the per-frame pool
	 * @param setIndex Which set type to allocate
	 * @return Allocated descriptor set, or null handle on failure
	 */
	vk::DescriptorSet allocateFrameSet(DescriptorSetIndex setIndex);

	/**
	 * @brief Allocate a persistent descriptor set
	 * @param setIndex Which set type to allocate
	 * @return Allocated descriptor set
	 */
	vk::DescriptorSet allocatePersistentSet(DescriptorSetIndex setIndex);

	/**
	 * @brief Update a uniform buffer binding in a descriptor set
	 */
	void updateUniformBuffer(vk::DescriptorSet set, uint32_t binding,
	                         vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range);

	/**
	 * @brief Update a combined image sampler binding
	 */
	void updateTexture(vk::DescriptorSet set, uint32_t binding,
	                   vk::ImageView imageView, vk::Sampler sampler,
	                   vk::ImageLayout layout = vk::ImageLayout::eShaderReadOnlyOptimal);

	/**
	 * @brief Update a texture array binding
	 */
	void updateTextureArray(vk::DescriptorSet set, uint32_t binding,
	                        const SCP_vector<vk::DescriptorImageInfo>& images);

	/**
	 * @brief Begin a new frame - reset current frame's pool
	 */
	void beginFrame();

	/**
	 * @brief End current frame - advance to next pool
	 */
	void endFrame();

	/**
	 * @brief Get current frame index
	 */
	uint32_t getCurrentFrame() const { return m_currentFrame; }

	/**
	 * @brief Map uniform_block_type to descriptor set and binding
	 * @param blockType The uniform block type
	 * @param setIndex Output: which descriptor set
	 * @param binding Output: which binding within the set
	 * @return true if mapping exists
	 */
	static bool getUniformBlockBinding(uniform_block_type blockType,
	                                   DescriptorSetIndex& setIndex, uint32_t& binding);

private:
	/**
	 * @brief Create all descriptor set layouts
	 */
	void createSetLayouts();

	/**
	 * @brief Create descriptor pools
	 */
	void createDescriptorPools();

	/**
	 * @brief Create a single descriptor set layout
	 */
	vk::UniqueDescriptorSetLayout createSetLayout(const SCP_vector<DescriptorBindingInfo>& bindings);

	/**
	 * @brief Create a new descriptor pool with standard sizes
	 */
	vk::UniqueDescriptorPool createFramePool();

	vk::Device m_device;

	// Descriptor set layouts (one per set type)
	std::array<vk::UniqueDescriptorSetLayout, static_cast<size_t>(DescriptorSetIndex::Count)> m_setLayouts;

	// Per-frame descriptor pools (growable - new pools added on demand)
	std::array<SCP_vector<vk::UniqueDescriptorPool>, MAX_FRAMES_IN_FLIGHT> m_framePools;

	// Persistent descriptor pool (for long-lived sets)
	vk::UniqueDescriptorPool m_persistentPool;

	uint32_t m_currentFrame = 0;
	bool m_initialized = false;
};

// Global descriptor manager access
VulkanDescriptorManager* getDescriptorManager();
void setDescriptorManager(VulkanDescriptorManager* manager);

} // namespace vulkan
} // namespace graphics

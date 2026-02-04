#pragma once

#include "osapi/osapi.h"

#include "RenderFrame.h"
#include "VulkanMemory.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanShader.h"
#include "VulkanDescriptorManager.h"
#include "VulkanPipeline.h"
#include "VulkanState.h"
#include "VulkanDraw.h"
#include "VulkanDeletionQueue.h"

#include <vulkan/vulkan.hpp>

#if SDL_VERSION_ATLEAST(2, 0, 6)
#define SDL_SUPPORTS_VULKAN 1
#else
#define SDL_SUPPORTS_VULKAN 0
#endif

namespace graphics {
namespace vulkan {

struct QueueIndex {
	// Poor mans std::optional
	bool initialized = false;
	uint32_t index = 0;
};

struct PhysicalDeviceValues {
	vk::PhysicalDevice device;
	vk::PhysicalDeviceProperties properties;
	vk::PhysicalDeviceFeatures features;

	std::vector<vk::ExtensionProperties> extensions;

	vk::SurfaceCapabilitiesKHR surfaceCapabilities;
	std::vector<vk::SurfaceFormatKHR> surfaceFormats;
	std::vector<vk::PresentModeKHR> presentModes;

	std::vector<vk::QueueFamilyProperties> queueProperties;
	QueueIndex graphicsQueueIndex;
	QueueIndex transferQueueIndex;
	QueueIndex presentQueueIndex;
};

class VulkanRenderer {
  public:
	explicit VulkanRenderer(std::unique_ptr<os::GraphicsOperations> graphicsOps);

	bool initialize();

	/**
	 * @brief Setup for a new frame - begins command buffer and render pass
	 * Called at the START of each frame before any draw calls
	 */
	void setupFrame();

	/**
	 * @brief End frame - ends render pass, submits, and presents
	 * Called at the END of each frame after all draw calls
	 */
	void flip();

	void shutdown();

	/**
	 * @brief Get the minimum uniform buffer offset alignment requirement
	 * @return The alignment in bytes (typically 64 or 256)
	 */
	uint32_t getMinUniformBufferOffsetAlignment() const;

	/**
	 * @brief Get the current frame number (total frames rendered)
	 */
	uint64_t getCurrentFrameNumber() const { return m_frameNumber; }

	/**
	 * @brief Wait for all GPU work to complete
	 */
	void waitIdle();

  private:
	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

	bool initDisplayDevice() const;

	bool initializeInstance();

	bool initializeSurface();

	bool pickPhysicalDevice(PhysicalDeviceValues& deviceValues);

	bool createLogicalDevice(const PhysicalDeviceValues& deviceValues);

	bool createSwapChain(const PhysicalDeviceValues& deviceValues);

	vk::UniqueShaderModule loadShader(const SCP_string& name);

	void createGraphicsPipeline();

	void createRenderPass();

	void createFrameBuffers();

	void createCommandPool(const PhysicalDeviceValues& values);

	void createPresentSyncObjects();

	void acquireNextSwapChainImage();

	std::unique_ptr<os::GraphicsOperations> m_graphicsOps;

	vk::UniqueInstance m_vkInstance;
	vk::UniqueDebugReportCallbackEXT m_debugReport;

	vk::UniqueSurfaceKHR m_vkSurface;

	vk::UniqueDevice m_device;

	vk::Queue m_graphicsQueue;
	vk::Queue m_transferQueue;
	vk::Queue m_presentQueue;

	vk::UniqueSwapchainKHR m_swapChain;
	vk::Format m_swapChainImageFormat;
	vk::Extent2D m_swapChainExtent;
	SCP_vector<vk::Image> m_swapChainImages;
	SCP_vector<vk::UniqueImageView> m_swapChainImageViews;
	SCP_vector<vk::UniqueFramebuffer> m_swapChainFramebuffers;
	SCP_vector<RenderFrame*> m_swapChainImageRenderImage;

	uint32_t m_currentSwapChainImage = 0;

	vk::UniqueRenderPass m_renderPass;
	vk::UniquePipelineLayout m_pipelineLayout;
	vk::UniquePipeline m_graphicsPipeline;

	uint32_t m_currentFrame = 0;
	uint64_t m_frameNumber = 0;  // Total frames rendered (for sync tracking)
	std::array<std::unique_ptr<RenderFrame>, MAX_FRAMES_IN_FLIGHT> m_frames;

	vk::UniqueCommandPool m_graphicsCommandPool;

	// Current frame command buffer (valid between setupFrame and flip)
	vk::CommandBuffer m_currentCommandBuffer;
	std::vector<vk::CommandBuffer> m_currentCommandBuffers;  // For cleanup
	bool m_frameInProgress = false;

	// Physical device info (needed for memory manager)
	vk::PhysicalDevice m_physicalDevice;
	uint32_t m_graphicsQueueFamilyIndex = 0;
	uint32_t m_transferQueueFamilyIndex = 0;

	// Memory, buffer, and texture management
	std::unique_ptr<VulkanMemoryManager> m_memoryManager;
	std::unique_ptr<VulkanBufferManager> m_bufferManager;
	std::unique_ptr<VulkanTextureManager> m_textureManager;
	std::unique_ptr<VulkanDeletionQueue> m_deletionQueue;

	// Shader, descriptor, and pipeline management (Phase 3)
	std::unique_ptr<VulkanShaderManager> m_shaderManager;
	std::unique_ptr<VulkanDescriptorManager> m_descriptorManager;
	std::unique_ptr<VulkanPipelineManager> m_pipelineManager;

	// State tracking and draw management (Phase 4)
	std::unique_ptr<VulkanStateTracker> m_stateTracker;
	std::unique_ptr<VulkanDrawManager> m_drawManager;

#if SDL_SUPPORTS_VULKAN
	bool m_debugReportEnabled = false;
#endif
};

} // namespace vulkan
} // namespace graphics

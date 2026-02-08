#pragma once

#include "globalincs/pstypes.h"
#include "VulkanMemory.h"

#include <vulkan/vulkan.hpp>

namespace graphics {
namespace vulkan {

/**
 * @brief Manages Vulkan post-processing pipeline
 *
 * Owns offscreen render targets (HDR scene color + depth), render passes,
 * and executes post-processing passes (tonemapping, bloom, FXAA, etc.)
 * between the 3D scene rendering and the final swap chain presentation.
 */
class VulkanPostProcessor {
public:
	VulkanPostProcessor() = default;
	~VulkanPostProcessor() = default;

	// Non-copyable
	VulkanPostProcessor(const VulkanPostProcessor&) = delete;
	VulkanPostProcessor& operator=(const VulkanPostProcessor&) = delete;

	/**
	 * @brief Initialize post-processing resources
	 * @param device Vulkan logical device
	 * @param physDevice Physical device (for format checks)
	 * @param memMgr Memory manager for allocations
	 * @param extent Scene rendering resolution
	 * @param depthFormat Depth format (matches main depth buffer)
	 * @return true on success
	 */
	bool init(vk::Device device, vk::PhysicalDevice physDevice,
	          VulkanMemoryManager* memMgr, vk::Extent2D extent,
	          vk::Format depthFormat);

	/**
	 * @brief Shutdown and free all post-processing resources
	 */
	void shutdown();

	/**
	 * @brief Get the HDR scene render pass (for 3D scene rendering)
	 *
	 * This render pass has RGBA16F color + depth attachments with loadOp=eClear.
	 * Used between scene_texture_begin() and scene_texture_end().
	 */
	vk::RenderPass getSceneRenderPass() const { return m_sceneRenderPass; }

	/**
	 * @brief Get the HDR scene framebuffer
	 */
	vk::Framebuffer getSceneFramebuffer() const { return m_sceneFramebuffer; }

	/**
	 * @brief Get the scene rendering extent
	 */
	vk::Extent2D getSceneExtent() const { return m_extent; }

	/**
	 * @brief Execute post-processing passes and draw result to swap chain
	 *
	 * Called after the HDR scene render pass ends and before the resumed
	 * swap chain render pass begins. Runs tonemapping (and later bloom,
	 * FXAA, etc.) then draws a fullscreen triangle to blit the result
	 * into the swap chain.
	 *
	 * The caller is responsible for:
	 * 1. Ending the HDR scene render pass before calling this
	 * 2. Beginning the resumed swap chain render pass before calling this
	 *    (the blit draws INTO the resumed pass)
	 *
	 * @param cmd Active command buffer
	 */
	void blitToSwapChain(vk::CommandBuffer cmd);

	/**
	 * @brief Execute bloom post-processing passes
	 *
	 * Called after the HDR scene render pass ends and before the resumed
	 * swap chain render pass begins. Manages its own render passes internally.
	 *
	 * @param cmd Active command buffer (must be outside a render pass)
	 */
	void executeBloom(vk::CommandBuffer cmd);

	/**
	 * @brief Execute tonemapping pass (HDR scene → LDR)
	 *
	 * Called after bloom and before FXAA. Renders to Scene_ldr (RGBA8).
	 * Must be called outside a render pass.
	 *
	 * @param cmd Active command buffer (must be outside a render pass)
	 */
	void executeTonemap(vk::CommandBuffer cmd);

	/**
	 * @brief Execute FXAA anti-aliasing passes
	 *
	 * Called after tonemapping. Runs prepass (LDR→luminance) then
	 * FXAA main pass (luminance→LDR). Must be called outside a render pass.
	 *
	 * @param cmd Active command buffer (must be outside a render pass)
	 */
	void executeFXAA(vk::CommandBuffer cmd);

	/**
	 * @brief Execute post-processing effects (saturation, brightness, etc.)
	 *
	 * Called after FXAA and before the final blit. Reads Scene_ldr, writes
	 * Scene_luminance (reused as temp target). Must be called outside a render pass.
	 *
	 * @param cmd Active command buffer (must be outside a render pass)
	 * @return true if effects were applied (blit should read Scene_luminance)
	 */
	bool executePostEffects(vk::CommandBuffer cmd);

	/**
	 * @brief Execute lightshafts (god rays) pass
	 *
	 * Called after FXAA and before post-effects. Additively blends god rays
	 * onto Scene_ldr based on sun position and depth buffer sampling.
	 * Must be called outside a render pass.
	 *
	 * @param cmd Active command buffer (must be outside a render pass)
	 */
	void executeLightshafts(vk::CommandBuffer cmd);

	/**
	 * @brief Check if LDR targets are available (tonemapping + FXAA ready)
	 */
	bool hasLDRTargets() const { return m_ldrInitialized; }

	/**
	 * @brief Get the scene color image view (for post-processing texture binding)
	 */
	vk::ImageView getSceneColorView() const { return m_sceneColor.view; }

	/**
	 * @brief Get the scene color sampler
	 */
	vk::Sampler getSceneColorSampler() const { return m_linearSampler; }

	/**
	 * @brief Check if post-processing is initialized
	 */
	bool isInitialized() const { return m_initialized; }

private:
	void updateTonemappingUBO();

	bool createImage(uint32_t width, uint32_t height, vk::Format format,
	                 vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect,
	                 vk::Image& outImage, vk::ImageView& outView,
	                 VulkanAllocation& outAllocation);

	// LDR target methods
	bool initLDRTargets();
	void shutdownLDRTargets();

	// Bloom pipeline methods
	bool initBloom();
	void shutdownBloom();
	void generateMipmaps(vk::CommandBuffer cmd, vk::Image image,
	                     uint32_t width, uint32_t height, uint32_t mipLevels);
	void drawFullscreenTriangle(vk::CommandBuffer cmd, vk::RenderPass renderPass,
	                            vk::Framebuffer framebuffer, vk::Extent2D extent,
	                            int shaderType, unsigned int shaderFlags,
	                            vk::ImageView textureView, vk::Sampler sampler,
	                            const void* uboData, size_t uboSize,
	                            int blendMode);

	struct RenderTarget {
		vk::Image image;
		vk::ImageView view;
		VulkanAllocation allocation;
		vk::Format format = vk::Format::eUndefined;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	RenderTarget m_sceneColor;    // RGBA16F HDR scene color
	RenderTarget m_sceneDepth;    // Depth buffer for scene

	// Scene render pass and framebuffer
	vk::RenderPass m_sceneRenderPass;
	vk::Framebuffer m_sceneFramebuffer;

	// Sampler for post-processing texture reads (maxLod=0)
	vk::Sampler m_linearSampler;
	// Sampler with mipmap support for bloom textures
	vk::Sampler m_mipmapSampler;

	// Persistent UBO for tonemapping shader parameters
	vk::Buffer m_tonemapUBO;
	VulkanAllocation m_tonemapUBOAlloc;

	// ---- Bloom resources ----
	static constexpr int MAX_MIP_BLUR_LEVELS = 4;
	static constexpr size_t BLOOM_UBO_SLOT_SIZE = 256; // >= minUniformBufferOffsetAlignment

	struct BloomTarget {
		vk::Image image;
		VulkanAllocation allocation;
		vk::ImageView fullView;                      // All mip levels (for textureLod sampling)
		vk::ImageView mipViews[MAX_MIP_BLUR_LEVELS]; // Per-mip views (for framebuffer attachment)
		vk::Framebuffer mipFramebuffers[MAX_MIP_BLUR_LEVELS];
	};

	BloomTarget m_bloomTex[2];                 // Half-res RGBA16F, 4 mip levels
	uint32_t m_bloomWidth = 0;                 // Half of scene width
	uint32_t m_bloomHeight = 0;                // Half of scene height
	vk::RenderPass m_bloomRenderPass;          // Color-only RGBA16F, loadOp=eDontCare
	vk::RenderPass m_bloomCompositeRenderPass; // Color-only RGBA16F, loadOp=eLoad (additive to scene)
	vk::Framebuffer m_sceneColorBloomFB;       // Scene_color as color attachment for bloom composite

	// Per-draw UBO for bloom passes (each draw uses different offset)
	vk::Buffer m_bloomUBO;
	VulkanAllocation m_bloomUBOAlloc;
	void* m_bloomUBOMapped = nullptr;
	uint32_t m_bloomUBOCursor = 0;             // Current slot index (reset per frame)
	static constexpr uint32_t BLOOM_UBO_MAX_SLOTS = 24;

	bool m_bloomInitialized = false;

	// ---- LDR / FXAA resources ----
	RenderTarget m_sceneLdr;           // RGBA8 LDR after tonemapping
	RenderTarget m_sceneLuminance;     // RGBA8 LDR with luma in alpha (for FXAA)
	vk::RenderPass m_ldrRenderPass;    // Color-only RGBA8, loadOp=eDontCare
	vk::RenderPass m_ldrLoadRenderPass; // Color-only RGBA8, loadOp=eLoad (for additive blending)
	vk::Framebuffer m_sceneLdrFB;
	vk::Framebuffer m_sceneLuminanceFB;
	bool m_ldrInitialized = false;
	bool m_postEffectsApplied = false; // Set per-frame by executePostEffects

	vk::Device m_device;
	VulkanMemoryManager* m_memoryManager = nullptr;
	vk::Extent2D m_extent;
	vk::Format m_depthFormat = vk::Format::eUndefined;

	bool m_initialized = false;
};

// Global post-processor access
VulkanPostProcessor* getPostProcessor();
void setPostProcessor(VulkanPostProcessor* pp);

} // namespace vulkan
} // namespace graphics

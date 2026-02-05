#include "VulkanState.h"

#include <cmath>

namespace graphics {
namespace vulkan {

// Global state tracker pointer
static VulkanStateTracker* g_stateTracker = nullptr;

VulkanStateTracker* getStateTracker()
{
	return g_stateTracker;
}

void setStateTracker(VulkanStateTracker* tracker)
{
	g_stateTracker = tracker;
}

bool VulkanStateTracker::init(vk::Device device)
{
	if (m_initialized) {
		return true;
	}

	m_device = device;

	// Initialize default viewport
	m_viewport.x = 0.0f;
	m_viewport.y = 0.0f;
	m_viewport.width = static_cast<float>(gr_screen.max_w);
	m_viewport.height = static_cast<float>(gr_screen.max_h);
	m_viewport.minDepth = 0.0f;
	m_viewport.maxDepth = 1.0f;

	// Initialize default scissor
	m_scissor.offset.x = 0;
	m_scissor.offset.y = 0;
	m_scissor.extent.width = gr_screen.max_w;
	m_scissor.extent.height = gr_screen.max_h;

	// Initialize clear color (dark blue for debugging - shows clears are working)
	m_clearColor.float32[0] = 0.0f;
	m_clearColor.float32[1] = 0.0f;
	m_clearColor.float32[2] = 0.3f;
	m_clearColor.float32[3] = 1.0f;

	m_initialized = true;
	mprintf(("VulkanStateTracker: Initialized\n"));
	return true;
}

void VulkanStateTracker::shutdown()
{
	if (!m_initialized) {
		return;
	}

	m_cmdBuffer = nullptr;
	m_currentPipeline = nullptr;
	m_currentRenderPass = nullptr;

	m_initialized = false;
	mprintf(("VulkanStateTracker: Shutdown complete\n"));
}

void VulkanStateTracker::beginFrame(vk::CommandBuffer cmdBuffer)
{
	mprintf(("VulkanStateTracker::beginFrame - cmdBuffer=%p\n",
		static_cast<void*>(static_cast<VkCommandBuffer>(cmdBuffer))));

	m_cmdBuffer = cmdBuffer;

	// Reset state for new frame
	m_currentPipeline = nullptr;
	m_currentRenderPass = nullptr;

	for (auto& set : m_boundDescriptorSets) {
		set = nullptr;
	}

	// Mark all dynamic state as dirty
	m_viewportDirty = true;
	m_scissorDirty = true;
	m_depthBiasDirty = true;
	m_stencilRefDirty = true;
	m_lineWidthDirty = true;
}

void VulkanStateTracker::endFrame()
{
	mprintf(("VulkanStateTracker::endFrame - clearing cmdBuffer (was %p)\n",
		static_cast<void*>(static_cast<VkCommandBuffer>(m_cmdBuffer))));
	m_cmdBuffer = nullptr;
}

void VulkanStateTracker::setRenderPass(vk::RenderPass renderPass, uint32_t subpass)
{
	m_currentRenderPass = renderPass;
	m_currentSubpass = subpass;

	// Pipeline needs to be rebound when render pass changes
	m_currentPipeline = nullptr;
}

void VulkanStateTracker::setViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
{
	if (m_viewport.x != x || m_viewport.y != y ||
	    m_viewport.width != width || m_viewport.height != height ||
	    m_viewport.minDepth != minDepth || m_viewport.maxDepth != maxDepth) {
		m_viewport.x = x;
		m_viewport.y = y;
		m_viewport.width = width;
		m_viewport.height = height;
		m_viewport.minDepth = minDepth;
		m_viewport.maxDepth = maxDepth;
		m_viewportDirty = true;
	}
}

void VulkanStateTracker::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
	if (m_scissor.offset.x != x || m_scissor.offset.y != y ||
	    m_scissor.extent.width != width || m_scissor.extent.height != height) {
		m_scissor.offset.x = x;
		m_scissor.offset.y = y;
		m_scissor.extent.width = width;
		m_scissor.extent.height = height;
		m_scissorDirty = true;
	}
}

void VulkanStateTracker::setScissorEnabled(bool enabled)
{
	if (m_scissorEnabled != enabled) {
		m_scissorEnabled = enabled;
		m_scissorDirty = true;
	}
}

void VulkanStateTracker::setDepthBias(float constantFactor, float slopeFactor)
{
	if (m_depthBiasConstant != constantFactor || m_depthBiasSlope != slopeFactor) {
		m_depthBiasConstant = constantFactor;
		m_depthBiasSlope = slopeFactor;
		m_depthBiasDirty = true;
	}
}

void VulkanStateTracker::setStencilReference(uint32_t reference)
{
	if (m_stencilReference != reference) {
		m_stencilReference = reference;
		m_stencilRefDirty = true;
	}
}

void VulkanStateTracker::setLineWidth(float width)
{
	if (m_lineWidth != width) {
		m_lineWidth = width;
		m_lineWidthDirty = true;
	}
}

void VulkanStateTracker::bindPipeline(vk::Pipeline pipeline, vk::PipelineLayout layout)
{
	if (m_currentPipeline != pipeline && pipeline && m_cmdBuffer) {
		m_cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
		m_currentPipeline = pipeline;
		m_currentPipelineLayout = layout;

		// After binding new pipeline, need to re-apply dynamic state
		applyDynamicState();

		// Clear bound descriptor sets since they need to be rebound with new layout
		for (auto& set : m_boundDescriptorSets) {
			set = nullptr;
		}
	}
}

void VulkanStateTracker::bindDescriptorSet(DescriptorSetIndex setIndex, vk::DescriptorSet set,
                                            const SCP_vector<uint32_t>& dynamicOffsets)
{
	uint32_t index = static_cast<uint32_t>(setIndex);

	if (m_boundDescriptorSets[index] != set && set && m_cmdBuffer && m_currentPipelineLayout) {
		mprintf(("VulkanStateTracker: Binding descriptor set %p to index %u (cmdBuffer=%p)\n",
			static_cast<void*>(static_cast<VkDescriptorSet>(set)),
			index,
			static_cast<void*>(static_cast<VkCommandBuffer>(m_cmdBuffer))));

		m_cmdBuffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			m_currentPipelineLayout,
			index,
			1, &set,
			static_cast<uint32_t>(dynamicOffsets.size()),
			dynamicOffsets.empty() ? nullptr : dynamicOffsets.data());

		m_boundDescriptorSets[index] = set;
	}
}

void VulkanStateTracker::bindVertexBuffer(uint32_t binding, vk::Buffer buffer, vk::DeviceSize offset)
{
	if (m_cmdBuffer && buffer) {
		m_cmdBuffer.bindVertexBuffers(binding, 1, &buffer, &offset);
	}
}

void VulkanStateTracker::bindIndexBuffer(vk::Buffer buffer, vk::DeviceSize offset, vk::IndexType indexType)
{
	if (m_cmdBuffer && buffer) {
		m_cmdBuffer.bindIndexBuffer(buffer, offset, indexType);
	}
}

void VulkanStateTracker::setClearColor(float r, float g, float b, float a)
{
	m_clearColor.float32[0] = r;
	m_clearColor.float32[1] = g;
	m_clearColor.float32[2] = b;
	m_clearColor.float32[3] = a;
}

void VulkanStateTracker::applyDynamicState()
{
	if (!m_cmdBuffer) {
		return;
	}

	if (m_viewportDirty) {
		m_cmdBuffer.setViewport(0, 1, &m_viewport);
		m_viewportDirty = false;
	}

	if (m_scissorDirty) {
		if (m_scissorEnabled) {
			m_cmdBuffer.setScissor(0, 1, &m_scissor);
		} else {
			// Set scissor to full viewport when disabled
			// Note: viewport may have negative height (Y-flip), use abs values
			vk::Rect2D fullScissor;
			fullScissor.offset.x = static_cast<int32_t>(m_viewport.x);
			fullScissor.offset.y = static_cast<int32_t>(std::min(m_viewport.y, m_viewport.y + m_viewport.height));
			fullScissor.extent.width = static_cast<uint32_t>(std::abs(m_viewport.width));
			fullScissor.extent.height = static_cast<uint32_t>(std::abs(m_viewport.height));
			m_cmdBuffer.setScissor(0, 1, &fullScissor);
		}
		m_scissorDirty = false;
	}

	if (m_depthBiasDirty) {
		m_cmdBuffer.setDepthBias(m_depthBiasConstant, 0.0f, m_depthBiasSlope);
		m_depthBiasDirty = false;
	}

	if (m_stencilRefDirty) {
		m_cmdBuffer.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack, m_stencilReference);
		m_stencilRefDirty = false;
	}

	if (m_lineWidthDirty) {
		m_cmdBuffer.setLineWidth(m_lineWidth);
		m_lineWidthDirty = false;
	}
}

} // namespace vulkan
} // namespace graphics

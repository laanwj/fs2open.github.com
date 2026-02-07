
#include "VulkanDeletionQueue.h"

namespace graphics {
namespace vulkan {

namespace {
VulkanDeletionQueue* g_deletionQueue = nullptr;
}

VulkanDeletionQueue* getDeletionQueue()
{
	Assertion(g_deletionQueue != nullptr, "Vulkan DeletionQueue not initialized!");
	return g_deletionQueue;
}

void setDeletionQueue(VulkanDeletionQueue* queue)
{
	g_deletionQueue = queue;
}

VulkanDeletionQueue::~VulkanDeletionQueue()
{
	shutdown();
}

void VulkanDeletionQueue::init(vk::Device device, VulkanMemoryManager* memoryManager)
{
	m_device = device;
	m_memoryManager = memoryManager;
	m_initialized = true;
}

void VulkanDeletionQueue::shutdown()
{
	if (!m_initialized) {
		return;
	}

	flushAll();
	m_initialized = false;
}

void VulkanDeletionQueue::queueBuffer(vk::Buffer buffer, VulkanAllocation allocation)
{
	if (!m_initialized || !buffer) {
		return;
	}

	PendingDestruction pending;
	pending.resource = PendingBuffer{buffer, allocation};
	pending.framesRemaining = FRAMES_TO_WAIT;
	m_pendingDestructions.push_back(pending);
}

void VulkanDeletionQueue::queueImage(vk::Image image, VulkanAllocation allocation)
{
	if (!m_initialized || !image) {
		return;
	}

	PendingDestruction pending;
	pending.resource = PendingImage{image, allocation};
	pending.framesRemaining = FRAMES_TO_WAIT;
	m_pendingDestructions.push_back(pending);
}

void VulkanDeletionQueue::queueImageView(vk::ImageView imageView)
{
	if (!m_initialized || !imageView) {
		return;
	}

	PendingDestruction pending;
	pending.resource = imageView;
	pending.framesRemaining = FRAMES_TO_WAIT;
	m_pendingDestructions.push_back(pending);
}

void VulkanDeletionQueue::queueFramebuffer(vk::Framebuffer framebuffer)
{
	if (!m_initialized || !framebuffer) {
		return;
	}

	PendingDestruction pending;
	pending.resource = framebuffer;
	pending.framesRemaining = FRAMES_TO_WAIT;
	m_pendingDestructions.push_back(pending);
}

void VulkanDeletionQueue::queueRenderPass(vk::RenderPass renderPass)
{
	if (!m_initialized || !renderPass) {
		return;
	}

	PendingDestruction pending;
	pending.resource = renderPass;
	pending.framesRemaining = FRAMES_TO_WAIT;
	m_pendingDestructions.push_back(pending);
}

void VulkanDeletionQueue::queueSampler(vk::Sampler sampler)
{
	if (!m_initialized || !sampler) {
		return;
	}

	PendingDestruction pending;
	pending.resource = sampler;
	pending.framesRemaining = FRAMES_TO_WAIT;
	m_pendingDestructions.push_back(pending);
}

void VulkanDeletionQueue::processDestructions()
{
	if (!m_initialized || m_pendingDestructions.empty()) {
		return;
	}

	auto it = m_pendingDestructions.begin();
	while (it != m_pendingDestructions.end()) {
		if (it->framesRemaining > 0) {
			it->framesRemaining--;
			++it;
		} else {
			destroyResource(it->resource);
			it = m_pendingDestructions.erase(it);
		}
	}
}

void VulkanDeletionQueue::flushAll()
{
	if (!m_initialized) {
		return;
	}

	for (const auto& pending : m_pendingDestructions) {
		destroyResource(pending.resource);
	}
	m_pendingDestructions.clear();
}

void VulkanDeletionQueue::destroyResource(const PendingResource& resource)
{
	std::visit([this](auto&& res) -> void {
		using T = std::decay_t<decltype(res)>;

		if constexpr (std::is_same_v<T, PendingBuffer>) {
			if (res.buffer) {
				m_device.destroyBuffer(res.buffer);
			}
			if (res.allocation.memory != VK_NULL_HANDLE && m_memoryManager) {
				VulkanAllocation alloc = res.allocation;  // Copy for non-const ref
				m_memoryManager->freeAllocation(alloc);
			}
		} else if constexpr (std::is_same_v<T, PendingImage>) {
			if (res.image) {
				m_device.destroyImage(res.image);
			}
			if (res.allocation.memory != VK_NULL_HANDLE && m_memoryManager) {
				VulkanAllocation alloc = res.allocation;  // Copy for non-const ref
				m_memoryManager->freeAllocation(alloc);
			}
		} else if constexpr (std::is_same_v<T, vk::ImageView>) {
			if (res) {
				m_device.destroyImageView(res);
			}
		} else if constexpr (std::is_same_v<T, vk::Framebuffer>) {
			if (res) {
				m_device.destroyFramebuffer(res);
			}
		} else if constexpr (std::is_same_v<T, vk::RenderPass>) {
			if (res) {
				m_device.destroyRenderPass(res);
			}
		} else if constexpr (std::is_same_v<T, vk::Sampler>) {
			if (res) {
				m_device.destroySampler(res);
			}
		}
	}, resource);
}

} // namespace vulkan
} // namespace graphics

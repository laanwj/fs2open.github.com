#include "VulkanBuffer.h"
#include "VulkanDeletionQueue.h"
#include "VulkanDraw.h"

#include "globalincs/pstypes.h"

namespace graphics {
namespace vulkan {

namespace {
VulkanBufferManager* g_bufferManager = nullptr;
}

VulkanBufferManager* getBufferManager()
{
	return g_bufferManager;
}

void setBufferManager(VulkanBufferManager* manager)
{
	g_bufferManager = manager;
}

VulkanBufferManager::VulkanBufferManager() = default;

VulkanBufferManager::~VulkanBufferManager()
{
	if (m_initialized) {
		shutdown();
	}
}

bool VulkanBufferManager::init(vk::Device device,
                               VulkanMemoryManager* memoryManager,
                               uint32_t graphicsQueueFamily,
                               uint32_t transferQueueFamily)
{
	if (m_initialized) {
		mprintf(("VulkanBufferManager::init called when already initialized!\n"));
		return false;
	}

	if (!device || !memoryManager) {
		mprintf(("VulkanBufferManager::init called with null device or memory manager!\n"));
		return false;
	}

	m_device = device;
	m_memoryManager = memoryManager;
	m_graphicsQueueFamily = graphicsQueueFamily;
	m_transferQueueFamily = transferQueueFamily;
	m_currentFrame = 0;

	// Create fallback color buffer with white (1,1,1,1) for shaders expecting vertColor
	{
		vk::BufferCreateInfo bufferInfo;
		bufferInfo.size = 16;  // vec4 = 16 bytes
		bufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		try {
			m_fallbackColorBuffer = m_device.createBuffer(bufferInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("Failed to create fallback color buffer: %s\n", e.what()));
			return false;
		}

		if (!m_memoryManager->allocateBufferMemory(m_fallbackColorBuffer, MemoryUsage::CpuToGpu, m_fallbackColorAllocation)) {
			m_device.destroyBuffer(m_fallbackColorBuffer);
			m_fallbackColorBuffer = nullptr;
			mprintf(("Failed to allocate fallback color buffer memory!\n"));
			return false;
		}

		// Write white color (1.0, 1.0, 1.0, 1.0) to the buffer
		float whiteColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		void* mapped = m_memoryManager->mapMemory(m_fallbackColorAllocation);
		if (mapped) {
			memcpy(mapped, whiteColor, sizeof(whiteColor));
			m_memoryManager->flushMemory(m_fallbackColorAllocation, 0, sizeof(whiteColor));
			m_memoryManager->unmapMemory(m_fallbackColorAllocation);
		}

		mprintf(("Created fallback white color buffer\n"));
	}

	// Create fallback texcoord buffer with zeros (0,0,0,0) for shaders expecting vertTexCoord
	{
		vk::BufferCreateInfo bufferInfo;
		bufferInfo.size = 16;  // vec4 = 16 bytes
		bufferInfo.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		try {
			m_fallbackTexCoordBuffer = m_device.createBuffer(bufferInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("Failed to create fallback texcoord buffer: %s\n", e.what()));
			return false;
		}

		if (!m_memoryManager->allocateBufferMemory(m_fallbackTexCoordBuffer, MemoryUsage::CpuToGpu, m_fallbackTexCoordAllocation)) {
			m_device.destroyBuffer(m_fallbackTexCoordBuffer);
			m_fallbackTexCoordBuffer = nullptr;
			mprintf(("Failed to allocate fallback texcoord buffer memory!\n"));
			return false;
		}

		// Write zero texcoord (0.0, 0.0, 0.0, 0.0) to the buffer
		float zeroTexCoord[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		void* mapped = m_memoryManager->mapMemory(m_fallbackTexCoordAllocation);
		if (mapped) {
			memcpy(mapped, zeroTexCoord, sizeof(zeroTexCoord));
			m_memoryManager->flushMemory(m_fallbackTexCoordAllocation, 0, sizeof(zeroTexCoord));
			m_memoryManager->unmapMemory(m_fallbackTexCoordAllocation);
		}

		mprintf(("Created fallback zero texcoord buffer\n"));
	}

	// Create fallback uniform buffer (zeros) for uninitialized descriptor set bindings
	// Without this, descriptor set UBO bindings left unwritten after pool reset
	// contain undefined data, causing intermittent rendering failures
	{
		vk::BufferCreateInfo bufferInfo;
		bufferInfo.size = FALLBACK_UNIFORM_BUFFER_SIZE;
		bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		try {
			m_fallbackUniformBuffer = m_device.createBuffer(bufferInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("Failed to create fallback uniform buffer: %s\n", e.what()));
			return false;
		}

		if (!m_memoryManager->allocateBufferMemory(m_fallbackUniformBuffer, MemoryUsage::CpuToGpu, m_fallbackUniformAllocation)) {
			m_device.destroyBuffer(m_fallbackUniformBuffer);
			m_fallbackUniformBuffer = nullptr;
			mprintf(("Failed to allocate fallback uniform buffer memory!\n"));
			return false;
		}

		// Zero-fill the buffer
		void* mapped = m_memoryManager->mapMemory(m_fallbackUniformAllocation);
		if (mapped) {
			memset(mapped, 0, FALLBACK_UNIFORM_BUFFER_SIZE);
			m_memoryManager->flushMemory(m_fallbackUniformAllocation, 0, FALLBACK_UNIFORM_BUFFER_SIZE);
			m_memoryManager->unmapMemory(m_fallbackUniformAllocation);
		}

		mprintf(("Created fallback uniform buffer (%zu bytes)\n", FALLBACK_UNIFORM_BUFFER_SIZE));
	}

	m_initialized = true;
	mprintf(("Vulkan Buffer Manager initialized (per-frame streaming buffers enabled, %u frames)\n",
		BUFFER_MAX_FRAMES_IN_FLIGHT));
	return true;
}

void VulkanBufferManager::shutdown()
{
	if (!m_initialized) {
		return;
	}

	// Destroy fallback color buffer
	if (m_fallbackColorBuffer) {
		m_device.destroyBuffer(m_fallbackColorBuffer);
		m_fallbackColorBuffer = nullptr;
	}
	if (m_fallbackColorAllocation.memory != VK_NULL_HANDLE) {
		m_memoryManager->freeAllocation(m_fallbackColorAllocation);
		m_fallbackColorAllocation = {};
	}

	// Destroy fallback texcoord buffer
	if (m_fallbackTexCoordBuffer) {
		m_device.destroyBuffer(m_fallbackTexCoordBuffer);
		m_fallbackTexCoordBuffer = nullptr;
	}
	if (m_fallbackTexCoordAllocation.memory != VK_NULL_HANDLE) {
		m_memoryManager->freeAllocation(m_fallbackTexCoordAllocation);
		m_fallbackTexCoordAllocation = {};
	}

	// Destroy fallback uniform buffer
	if (m_fallbackUniformBuffer) {
		m_device.destroyBuffer(m_fallbackUniformBuffer);
		m_fallbackUniformBuffer = nullptr;
	}
	if (m_fallbackUniformAllocation.memory != VK_NULL_HANDLE) {
		m_memoryManager->freeAllocation(m_fallbackUniformAllocation);
		m_fallbackUniformAllocation = {};
	}

	// Free all remaining buffers
	for (auto& bufferObj : m_buffers) {
		if (bufferObj.valid) {
			if (bufferObj.buffer) {
				m_device.destroyBuffer(bufferObj.buffer);
			}
			if (bufferObj.allocation.memory != VK_NULL_HANDLE) {
				m_memoryManager->freeAllocation(bufferObj.allocation);
			}
			bufferObj.valid = false;
		}
	}

	m_buffers.clear();
	m_freeIndices.clear();
	m_activeBufferCount = 0;
	m_totalBufferMemory = 0;
	m_initialized = false;

	mprintf(("Vulkan Buffer Manager shutdown\n"));
}

void VulkanBufferManager::setCurrentFrame(uint32_t frameIndex)
{
	m_currentFrame = frameIndex % BUFFER_MAX_FRAMES_IN_FLIGHT;
}

vk::BufferUsageFlags VulkanBufferManager::getVkUsageFlags(BufferType type) const
{
	vk::BufferUsageFlags flags = vk::BufferUsageFlagBits::eTransferDst;

	switch (type) {
	case BufferType::Vertex:
		flags |= vk::BufferUsageFlagBits::eVertexBuffer;
		break;
	case BufferType::Index:
		flags |= vk::BufferUsageFlagBits::eIndexBuffer;
		break;
	case BufferType::Uniform:
		flags |= vk::BufferUsageFlagBits::eUniformBuffer;
		break;
	}

	return flags;
}

MemoryUsage VulkanBufferManager::getMemoryUsage(BufferUsageHint hint) const
{
	switch (hint) {
	case BufferUsageHint::Static:
		// Static data goes to device-local memory for best GPU performance
		// For simplicity, we use CpuToGpu which allows host writes
		// A more optimized path would use staging buffers for truly static data
		return MemoryUsage::CpuToGpu;

	case BufferUsageHint::Dynamic:
	case BufferUsageHint::Streaming:
		// Frequently updated data needs to be host visible
		return MemoryUsage::CpuToGpu;

	case BufferUsageHint::PersistentMapping:
		// Persistent mapping requires host visible memory
		return MemoryUsage::CpuOnly;

	default:
		return MemoryUsage::CpuToGpu;
	}
}

gr_buffer_handle VulkanBufferManager::createBuffer(BufferType type, BufferUsageHint usage)
{
	if (!m_initialized) {
		mprintf(("VulkanBufferManager::createBuffer called before initialization!\n"));
		return gr_buffer_handle::invalid();
	}

	VulkanBufferObject bufferObj;
	bufferObj.type = type;
	bufferObj.usage = usage;
	bufferObj.valid = true;
	// Note: actual buffer creation is deferred until data is uploaded
	// All frame slots start as null/zero

	int index;
	if (!m_freeIndices.empty()) {
		// Reuse a freed slot
		index = m_freeIndices.back();
		m_freeIndices.pop_back();
		m_buffers[index] = bufferObj;
	} else {
		// Add new slot
		index = static_cast<int>(m_buffers.size());
		m_buffers.push_back(bufferObj);
	}

	++m_activeBufferCount;
	return gr_buffer_handle(index);
}

void VulkanBufferManager::deleteBuffer(gr_buffer_handle handle)
{
	if (!m_initialized || !isValidHandle(handle)) {
		return;
	}

	VulkanBufferObject& bufferObj = m_buffers[handle.value()];
	if (!bufferObj.valid) {
		return;
	}

	// Queue buffer for deferred destruction
	auto* deletionQueue = getDeletionQueue();
	if (bufferObj.buffer) {
		if (deletionQueue) {
			deletionQueue->queueBuffer(bufferObj.buffer, bufferObj.allocation);
		}
		m_totalBufferMemory -= bufferObj.totalSize;
	}
	bufferObj.buffer = nullptr;
	bufferObj.allocation = {};
	bufferObj.spanSize = 0;
	bufferObj.totalSize = 0;

	--m_activeBufferCount;
	bufferObj.valid = false;

	// Add to free list for reuse
	m_freeIndices.push_back(handle.value());
}

bool VulkanBufferManager::createOrResizeBuffer(VulkanBufferObject& bufferObj, size_t spanSize)
{
	// Calculate required total size
	// For streaming buffers: need space for all frames
	// For static buffers: just the span size
	size_t requiredTotal = bufferObj.isStreaming()
		? spanSize * BUFFER_MAX_FRAMES_IN_FLIGHT
		: spanSize;

	// If buffer exists and is large enough, just update span size
	if (bufferObj.buffer && bufferObj.totalSize >= requiredTotal) {
		bufferObj.spanSize = spanSize;
		return true;
	}

	// Need to create or resize - save old buffer info for data copy
	vk::Buffer oldBuffer = bufferObj.buffer;
	VulkanAllocation oldAllocation = bufferObj.allocation;
	size_t oldSpanSize = bufferObj.spanSize;
	size_t oldTotalSize = bufferObj.totalSize;

	// Create new buffer with total size for all frames
	vk::BufferCreateInfo bufferInfo;
	bufferInfo.size = requiredTotal;
	bufferInfo.usage = getVkUsageFlags(bufferObj.type);

	// Handle queue family sharing
	uint32_t queueFamilies[] = {m_graphicsQueueFamily, m_transferQueueFamily};
	if (m_graphicsQueueFamily != m_transferQueueFamily) {
		bufferInfo.sharingMode = vk::SharingMode::eConcurrent;
		bufferInfo.queueFamilyIndexCount = 2;
		bufferInfo.pQueueFamilyIndices = queueFamilies;
	} else {
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;
	}

	try {
		bufferObj.buffer = m_device.createBuffer(bufferInfo);
	} catch (const vk::SystemError& e) {
		mprintf(("Failed to create Vulkan buffer: %s\n", e.what()));
		// Restore old buffer on failure
		bufferObj.buffer = oldBuffer;
		return false;
	}

	// Allocate memory
	MemoryUsage memUsage = getMemoryUsage(bufferObj.usage);
	if (!m_memoryManager->allocateBufferMemory(bufferObj.buffer, memUsage, bufferObj.allocation)) {
		m_device.destroyBuffer(bufferObj.buffer);
		bufferObj.buffer = oldBuffer;
		bufferObj.allocation = oldAllocation;
		return false;
	}

	// Copy existing data from old buffer to new buffer (current frame's span)
	if (oldBuffer && oldSpanSize > 0) {
		void* oldMapped = m_memoryManager->mapMemory(oldAllocation);
		void* newMapped = m_memoryManager->mapMemory(bufferObj.allocation);

		if (oldMapped && newMapped) {
			// Copy all frame spans that had data
			size_t copySize = std::min(oldTotalSize, requiredTotal);
			memcpy(newMapped, oldMapped, copySize);
			m_memoryManager->flushMemory(bufferObj.allocation, 0, copySize);
		}

		if (oldMapped) m_memoryManager->unmapMemory(oldAllocation);
		if (newMapped) m_memoryManager->unmapMemory(bufferObj.allocation);
	}

	// Queue old buffer for deferred destruction (after copying data)
	if (oldBuffer) {
		queueDeferredDestruction(oldBuffer, oldAllocation, oldTotalSize);
		m_totalBufferMemory -= oldTotalSize;
	}

	bufferObj.spanSize = spanSize;
	bufferObj.totalSize = requiredTotal;
	m_totalBufferMemory += requiredTotal;

	return true;
}

void VulkanBufferManager::updateBufferData(gr_buffer_handle handle, size_t size, const void* data)
{
	if (!m_initialized || !isValidHandle(handle)) {
		return;
	}

	if (size == 0) {
		mprintf(("WARNING: updateBufferData called with size 0\n"));
		return;
	}

	VulkanBufferObject& bufferObj = m_buffers[handle.value()];
	if (!bufferObj.valid) {
		return;
	}

	// For streaming buffers with data, sub-allocate within the frame span.
	// This replicates OpenGL's glBufferData(GL_STREAM_DRAW) orphaning: each upload
	// gets its own region so recorded draw commands still reference valid data.
	if (bufferObj.isStreaming() && data) {
		// Reset cursor at the start of each frame
		if (m_currentFrame != bufferObj.lastResetFrame) {
			bufferObj.streamCursor = 0;
			bufferObj.lastWriteStreamOffset = 0;
			bufferObj.lastResetFrame = m_currentFrame;
		}

		// Ensure span is large enough for all sub-allocations this frame
		size_t neededSpan = bufferObj.streamCursor + size;
		if (!createOrResizeBuffer(bufferObj, neededSpan)) {
			mprintf(("Failed to create/resize buffer for streaming update!\n"));
			return;
		}

		// Write at the cursor position within this frame's span
		size_t frameOffset = bufferObj.getFrameOffset(m_currentFrame);
		size_t writeOffset = frameOffset + bufferObj.streamCursor;

		void* mapped = m_memoryManager->mapMemory(bufferObj.allocation);
		if (mapped) {
			memcpy(static_cast<uint8_t*>(mapped) + writeOffset, data, size);
			m_memoryManager->flushMemory(bufferObj.allocation, writeOffset, size);
			m_memoryManager->unmapMemory(bufferObj.allocation);
		} else {
			mprintf(("Failed to map buffer memory for streaming update!\n"));
		}

		bufferObj.lastWriteStreamOffset = bufferObj.streamCursor;
		bufferObj.streamCursor += size;
		return;
	}

	// Non-streaming path (static buffers, or null data for pre-allocation)
	if (!createOrResizeBuffer(bufferObj, size)) {
		mprintf(("Failed to create/resize buffer for update!\n"));
		return;
	}

	// A null data pointer just allocates/resizes the buffer without writing
	if (data) {
		size_t frameOffset = bufferObj.getFrameOffset(m_currentFrame);
		void* mapped = m_memoryManager->mapMemory(bufferObj.allocation);
		if (mapped) {
			memcpy(static_cast<uint8_t*>(mapped) + frameOffset, data, size);
			m_memoryManager->flushMemory(bufferObj.allocation, frameOffset, size);
			m_memoryManager->unmapMemory(bufferObj.allocation);
		} else {
			mprintf(("Failed to map buffer memory for update!\n"));
		}
	}
}

void VulkanBufferManager::updateBufferDataOffset(gr_buffer_handle handle, size_t offset, size_t size, const void* data)
{
	if (!m_initialized || !isValidHandle(handle)) {
		return;
	}

	VulkanBufferObject& bufferObj = m_buffers[handle.value()];
	if (!bufferObj.valid) {
		return;
	}

	if (!bufferObj.buffer) {
		mprintf(("updateBufferDataOffset called on uninitialized buffer!\n"));
		return;
	}

	if (offset + size > bufferObj.spanSize) {
		mprintf(("updateBufferDataOffset: offset+size exceeds buffer span size!\n"));
		return;
	}

	// Calculate total offset: frame base + local offset
	size_t frameOffset = bufferObj.getFrameOffset(m_currentFrame);
	size_t totalOffset = frameOffset + offset;

	// Map, update region, and unmap
	void* mapped = m_memoryManager->mapMemory(bufferObj.allocation);
	if (mapped) {
		memcpy(static_cast<uint8_t*>(mapped) + totalOffset, data, size);
		m_memoryManager->flushMemory(bufferObj.allocation, totalOffset, size);
		m_memoryManager->unmapMemory(bufferObj.allocation);
	} else {
		mprintf(("Failed to map buffer memory for offset update!\n"));
	}
}

void* VulkanBufferManager::mapBuffer(gr_buffer_handle handle)
{
	if (!m_initialized || !isValidHandle(handle)) {
		return nullptr;
	}

	VulkanBufferObject& bufferObj = m_buffers[handle.value()];
	if (!bufferObj.valid || !bufferObj.buffer) {
		return nullptr;
	}

	// Only persistent mapping buffers should stay mapped
	if (bufferObj.usage != BufferUsageHint::PersistentMapping) {
		mprintf(("WARNING: mapBuffer called on non-persistent buffer\n"));
	}

	// Map the entire buffer, caller uses frame-relative offsets
	void* mapped = m_memoryManager->mapMemory(bufferObj.allocation);
	if (!mapped) {
		return nullptr;
	}

	// Return pointer offset to current frame's span
	size_t frameOffset = bufferObj.getFrameOffset(m_currentFrame);
	return static_cast<uint8_t*>(mapped) + frameOffset;
}

void VulkanBufferManager::flushMappedBuffer(gr_buffer_handle handle, size_t offset, size_t size)
{
	if (!m_initialized || !isValidHandle(handle)) {
		return;
	}

	VulkanBufferObject& bufferObj = m_buffers[handle.value()];
	if (!bufferObj.valid) {
		return;
	}

	// Adjust offset for current frame's span
	size_t frameOffset = bufferObj.getFrameOffset(m_currentFrame);
	m_memoryManager->flushMemory(bufferObj.allocation, frameOffset + offset, size);
}

void VulkanBufferManager::bindUniformBuffer(uniform_block_type blockType, size_t offset, size_t size, gr_buffer_handle buffer)
{
	// Tell the draw manager about this pending binding
	// Pass the handle, not the vk::Buffer - the buffer may be recreated before use
	// The draw manager will add the frame base offset when actually binding
	auto* drawManager = getDrawManager();
	if (drawManager) {
		drawManager->setPendingUniformBinding(blockType, buffer,
		                                       static_cast<vk::DeviceSize>(offset),
		                                       static_cast<vk::DeviceSize>(size));
	}
}

vk::Buffer VulkanBufferManager::getVkBuffer(gr_buffer_handle handle) const
{
	if (!isValidHandle(handle)) {
		return nullptr;
	}

	const VulkanBufferObject& bufferObj = m_buffers[handle.value()];
	if (!bufferObj.valid) {
		return nullptr;
	}

	// Always return the single buffer - callers use getFrameBaseOffset for offsets
	return bufferObj.buffer;
}

size_t VulkanBufferManager::getBufferSize(gr_buffer_handle handle) const
{
	if (!isValidHandle(handle)) {
		return 0;
	}

	const VulkanBufferObject& bufferObj = m_buffers[handle.value()];
	if (!bufferObj.valid) {
		return 0;
	}

	// Return the span size (per-frame usable size)
	return bufferObj.spanSize;
}

size_t VulkanBufferManager::getFrameBaseOffset(gr_buffer_handle handle) const
{
	if (!isValidHandle(handle)) {
		return 0;
	}

	const VulkanBufferObject& bufferObj = m_buffers[handle.value()];
	if (!bufferObj.valid) {
		return 0;
	}

	// Return the offset for the current frame's span, plus the stream sub-allocation
	// offset for the most recent upload. This ensures vertex buffer bindings point to
	// the correct data region when a streaming buffer is updated multiple times per frame.
	return bufferObj.getFrameOffset(m_currentFrame) + bufferObj.lastWriteStreamOffset;
}

bool VulkanBufferManager::isValidHandle(gr_buffer_handle handle) const
{
	if (!handle.isValid()) {
		return false;
	}
	if (static_cast<size_t>(handle.value()) >= m_buffers.size()) {
		return false;
	}
	return m_buffers[handle.value()].valid;
}

VulkanBufferObject* VulkanBufferManager::getBufferObject(gr_buffer_handle handle)
{
	if (!isValidHandle(handle)) {
		return nullptr;
	}
	return &m_buffers[handle.value()];
}

const VulkanBufferObject* VulkanBufferManager::getBufferObject(gr_buffer_handle handle) const
{
	if (!isValidHandle(handle)) {
		return nullptr;
	}
	return &m_buffers[handle.value()];
}

void VulkanBufferManager::processDeferredDestructions()
{
	if (m_pendingDestructions.empty()) {
		return;
	}

	// Process pending destructions - only destroy buffers that have waited enough frames
	// This ensures all in-flight command buffers have completed using the buffer
	auto it = m_pendingDestructions.begin();
	while (it != m_pendingDestructions.end()) {
		if (it->framesRemaining > 0) {
			// Still waiting - decrement counter and keep in list
			it->framesRemaining--;
			++it;
		} else {
			// Ready to destroy
			if (it->buffer) {
				m_device.destroyBuffer(it->buffer);
			}
			if (it->allocation.memory != VK_NULL_HANDLE) {
				m_memoryManager->freeAllocation(it->allocation);
			}

			it = m_pendingDestructions.erase(it);
		}
	}
}

void VulkanBufferManager::queueDeferredDestruction(vk::Buffer buffer, VulkanAllocation allocation, size_t size)
{
	// Use the unified deletion queue for deferred destruction
	auto* deletionQueue = getDeletionQueue();
	if (deletionQueue) {
		deletionQueue->queueBuffer(buffer, allocation);
	}
}

} // namespace vulkan
} // namespace graphics

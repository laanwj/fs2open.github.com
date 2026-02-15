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
	Assertion(g_bufferManager != nullptr, "Vulkan BufferManager not initialized!");
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

bool VulkanBufferManager::createOneShotBuffer(vk::Flags<vk::BufferUsageFlagBits> usage, const void* data, size_t size, vk::Buffer& buf, VulkanAllocation& alloc) const
{
	vk::BufferCreateInfo bufferInfo;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = vk::SharingMode::eExclusive;

	try {
		buf = m_device.createBuffer(bufferInfo);
	} catch (const vk::SystemError& e) {
		mprintf(("Failed to create buffer: %s\n", e.what()));
		return false;
	}

	if (!m_memoryManager->allocateBufferMemory(buf, MemoryUsage::CpuToGpu, alloc)) {
		m_device.destroyBuffer(buf);
		buf = nullptr;
		mprintf(("Failed to allocate buffer memory!\n"));
		return false;
	}

	void* mapped = m_memoryManager->mapMemory(alloc);
	if (mapped) {
		memcpy(mapped, data, size);
		m_memoryManager->flushMemory(alloc, 0, size);
		m_memoryManager->unmapMemory(alloc);
	} else {
		m_memoryManager->freeAllocation(alloc);
		m_device.destroyBuffer(buf);
		buf = nullptr;

		mprintf(("Failed to map buffer memory!\n"));
		return false;
	}
	return true;
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
	float whiteColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	if (!createOneShotBuffer(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, whiteColor, sizeof(whiteColor), m_fallbackColorBuffer, m_fallbackColorAllocation)) {
		mprintf(("VulkanBufferManager::init could not create fallback color buffer\n"));
		return false;
	}

	float zeroTexCoord[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	if (!createOneShotBuffer(vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, zeroTexCoord, sizeof(zeroTexCoord), m_fallbackTexCoordBuffer, m_fallbackTexCoordAllocation)) {
		mprintf(("VulkanBufferManager::init could not create fallback texcoord buffer\n"));
		return false;
	}

	// Create fallback uniform buffer (zeros) for uninitialized descriptor set bindings
	// Without this, descriptor set UBO bindings left unwritten after pool reset
	// contain undefined data, causing intermittent rendering failures
	float dummy_ubo[FALLBACK_UNIFORM_BUFFER_SIZE] = {};
	if (!createOneShotBuffer(vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer, dummy_ubo, sizeof(dummy_ubo), m_fallbackUniformBuffer, m_fallbackUniformAllocation)) {
		mprintf(("VulkanBufferManager::init could not create fallback texcoord buffer\n"));
		return false;
	}

	m_initialized = true;
	mprintf(("Vulkan Buffer Manager initialized (per-frame streaming buffers enabled, %u frames)\n",
		MAX_FRAMES_IN_FLIGHT));
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
	m_currentFrame = frameIndex % MAX_FRAMES_IN_FLIGHT;
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
		deletionQueue->queueBuffer(bufferObj.buffer, bufferObj.allocation);
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
		? spanSize * MAX_FRAMES_IN_FLIGHT
		: spanSize;

	// If buffer exists and is large enough, just update span size
	if (bufferObj.buffer && bufferObj.totalSize >= requiredTotal) {
		// If this is a streaming buffer, we'd need to synchronize and
		// move the frames around. However, there's a check in
		// updateBufferData that makes sure streaming buffers with data only ever
		// grow.
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

	// Copy existing data from old buffer to new buffer
	if (oldBuffer && oldSpanSize > 0) {
		void* oldMapped = m_memoryManager->mapMemory(oldAllocation);
		void* newMapped = m_memoryManager->mapMemory(bufferObj.allocation);

		if (oldMapped && newMapped) {
			if (bufferObj.isStreaming()) {
				// Copy each frame's span individually to preserve frame layout.
				// Old and new span sizes differ, so a bulk memcpy would misalign
				// frame 1+ data across the new span boundaries.
				for (uint32_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
					size_t oldFrameOffset = frame * oldSpanSize;
					size_t newFrameOffset = frame * spanSize;
					size_t copySize = std::min(oldSpanSize, spanSize);
					memcpy(static_cast<uint8_t*>(newMapped) + newFrameOffset,
					       static_cast<uint8_t*>(oldMapped) + oldFrameOffset,
					       copySize);
				}
				m_memoryManager->flushMemory(bufferObj.allocation, 0, requiredTotal);
			} else {
				// Static buffer: single span, simple copy
				size_t copySize = std::min(oldTotalSize, requiredTotal);
				memcpy(newMapped, oldMapped, copySize);
				m_memoryManager->flushMemory(bufferObj.allocation, 0, copySize);
			}
		}

		if (oldMapped) m_memoryManager->unmapMemory(oldAllocation);
		if (newMapped) m_memoryManager->unmapMemory(bufferObj.allocation);
	}

	// Queue old buffer for deferred destruction (after copying data)
	if (oldBuffer) {
		auto* deletionQueue = getDeletionQueue();
		deletionQueue->queueBuffer(oldBuffer, oldAllocation);
		m_totalBufferMemory -= oldTotalSize;
	}

	bufferObj.spanSize = spanSize;
	bufferObj.totalSize = requiredTotal;
	// Add size of new buffer. Size of old buffer is subtracted above.
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

		// Ensure span is large enough for all sub-allocations this frame.
		// Only grow, never shrink — shrinking spanSize would cause frame spans
		// to overlap in the ring buffer (frame 1 data overlapping frame 0 data
		// that the GPU may still be reading).
		size_t neededSpan = bufferObj.streamCursor + size;
		if (neededSpan > bufferObj.spanSize || !bufferObj.buffer) {
			Verify(createOrResizeBuffer(bufferObj, neededSpan));
		}

		// Write at the cursor position within this frame's span
		size_t frameOffset = bufferObj.getFrameOffset(m_currentFrame);
		size_t writeOffset = frameOffset + bufferObj.streamCursor;

		void* mapped = m_memoryManager->mapMemory(bufferObj.allocation);
		Verify(mapped);
		memcpy(static_cast<uint8_t*>(mapped) + writeOffset, data, size);
		m_memoryManager->flushMemory(bufferObj.allocation, writeOffset, size);
		m_memoryManager->unmapMemory(bufferObj.allocation);

		bufferObj.lastWriteStreamOffset = bufferObj.streamCursor;
		bufferObj.streamCursor += size;
		return;
	}

	// Non-streaming path (static buffers, or null data for pre-allocation)
	Verify(createOrResizeBuffer(bufferObj, size));

	// A null data pointer just allocates/resizes the buffer without writing
	if (data) {
		size_t frameOffset = bufferObj.getFrameOffset(m_currentFrame);
		void* mapped = m_memoryManager->mapMemory(bufferObj.allocation);
		Verify(mapped);
		memcpy(static_cast<uint8_t*>(mapped) + frameOffset, data, size);
		m_memoryManager->flushMemory(bufferObj.allocation, frameOffset, size);
		m_memoryManager->unmapMemory(bufferObj.allocation);
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
	Verify(mapped);
	memcpy(static_cast<uint8_t*>(mapped) + totalOffset, data, size);
	m_memoryManager->flushMemory(bufferObj.allocation, totalOffset, size);
	m_memoryManager->unmapMemory(bufferObj.allocation);
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
	// Resolve the full offset NOW (frame base + caller offset) to prevent stale
	// lastWriteStreamOffset if the same streaming buffer is updated again before draw.
	// The vk::Buffer is still looked up at draw time (via handle) to survive buffer recreation.
	size_t resolvedOffset = getFrameBaseOffset(buffer) + offset;

	auto* drawManager = getDrawManager();
	drawManager->setPendingUniformBinding(blockType, buffer,
	                                       static_cast<vk::DeviceSize>(resolvedOffset),
	                                       static_cast<vk::DeviceSize>(size));
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

	// Catch stale streaming offsets: if a streaming buffer has a non-zero
	// lastWriteStreamOffset from a previous frame, the offset would be wrong
	// (pointing into the previous upload's region within the current frame's span).
	// This indicates a buffer is being bound for rendering without being uploaded first.
	Assertion(bufferObj.lastWriteStreamOffset == 0 || bufferObj.lastResetFrame == m_currentFrame,
		"Stale lastWriteStreamOffset %zu on streaming buffer (handle %d): "
		"lastResetFrame=%u but currentFrame=%u. Buffer bound without upload this frame!",
		bufferObj.lastWriteStreamOffset, handle.value(),
		bufferObj.lastResetFrame, m_currentFrame);

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

// ========== gr_screen function pointer implementations ==========

gr_buffer_handle vulkan_create_buffer(BufferType type, BufferUsageHint usage)
{
	auto* bufferManager = getBufferManager();
	return bufferManager->createBuffer(type, usage);
}

void vulkan_delete_buffer(gr_buffer_handle handle)
{
	auto* bufferManager = getBufferManager();
	bufferManager->deleteBuffer(handle);
}

void vulkan_update_buffer_data(gr_buffer_handle handle, size_t size, const void* data)
{
	auto* bufferManager = getBufferManager();
	bufferManager->updateBufferData(handle, size, data);
}

void vulkan_update_buffer_data_offset(gr_buffer_handle handle, size_t offset, size_t size, const void* data)
{
	auto* bufferManager = getBufferManager();
	bufferManager->updateBufferDataOffset(handle, offset, size, data);
}

void* vulkan_map_buffer(gr_buffer_handle handle)
{
	auto* bufferManager = getBufferManager();
	void* result = bufferManager->mapBuffer(handle);
	Verify(result);
	return result;
}

void vulkan_flush_mapped_buffer(gr_buffer_handle handle, size_t offset, size_t size)
{
	auto* bufferManager = getBufferManager();
	bufferManager->flushMappedBuffer(handle, offset, size);
}

void vulkan_bind_uniform_buffer(uniform_block_type blockType, size_t offset, size_t size, gr_buffer_handle buffer)
{
	auto* bufferManager = getBufferManager();
	bufferManager->bindUniformBuffer(blockType, offset, size, buffer);
}

} // namespace vulkan
} // namespace graphics

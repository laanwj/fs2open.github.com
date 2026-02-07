#pragma once

#include "graphics/2d.h"
#include "VulkanConstants.h"
#include "VulkanMemory.h"

#include <vulkan/vulkan.hpp>

namespace graphics {
namespace vulkan {

/**
 * @brief Internal representation of a Vulkan buffer
 *
 * For streaming buffers (BufferUsageHint::Streaming), we use a ring buffer
 * approach: a single VkBuffer with separate spans per frame in flight.
 * This avoids race conditions where the GPU is still reading while CPU writes.
 *
 * Ring buffer layout for streaming buffers:
 *   [Frame 0 span][Frame 1 span]
 *   |<- spanSize ->|<- spanSize ->|
 *   |<-------- totalSize -------->|
 */
struct VulkanBufferObject {
	vk::Buffer buffer = nullptr;           // Single VkBuffer
	VulkanAllocation allocation = {};      // Single allocation
	size_t spanSize = 0;                   // Size of each frame's span (0 for static)
	size_t totalSize = 0;                  // Total buffer size

	BufferType type = BufferType::Vertex;
	BufferUsageHint usage = BufferUsageHint::Static;
	bool valid = false;

	// Stream sub-allocation state (for orphaning semantics within a frame)
	// When a streaming buffer is updated multiple times per frame, each upload
	// writes at an advancing cursor instead of overwriting offset 0.
	// This replicates OpenGL's glBufferData(GL_STREAM_DRAW) orphaning behavior.
	size_t streamCursor = 0;           // Next write position within the frame span (bytes)
	size_t lastWriteStreamOffset = 0;  // Byte offset of the most recent upload
	uint32_t lastResetFrame = UINT32_MAX; // Frame index when cursor was last reset

	// Helper to check if this buffer uses ring buffer spans
	bool isStreaming() const {
		return usage == BufferUsageHint::Streaming || usage == BufferUsageHint::Dynamic;
	}

	// Get the byte offset for a specific frame's span
	size_t getFrameOffset(uint32_t frameIndex) const {
		if (!isStreaming()) return 0;
		return frameIndex * spanSize;
	}
};

/**
 * @brief Pending buffer destruction entry
 * Buffers destroyed mid-frame are queued for deferred destruction
 * Must wait for all in-flight frames to complete before actual destruction
 */
struct PendingBufferDestruction {
	vk::Buffer buffer;
	VulkanAllocation allocation;
	size_t size;
	uint32_t framesRemaining;  // Number of frames to wait before destruction
};

/**
 * @brief Manages GPU buffer creation, updates, and destruction
 *
 * This class handles all buffer operations for the Vulkan renderer including
 * vertex buffers, index buffers, and uniform buffers.
 *
 * Streaming buffers are automatically double-buffered to prevent GPU/CPU
 * race conditions when MAX_FRAMES_IN_FLIGHT > 1.
 */
class VulkanBufferManager {
public:
	VulkanBufferManager();
	~VulkanBufferManager();

	// Non-copyable
	VulkanBufferManager(const VulkanBufferManager&) = delete;
	VulkanBufferManager& operator=(const VulkanBufferManager&) = delete;

	/**
	 * @brief Initialize the buffer manager
	 * @param device The Vulkan logical device
	 * @param memoryManager The memory manager for allocations
	 * @param graphicsQueueFamily Graphics queue family index
	 * @param transferQueueFamily Transfer queue family index
	 * @return true on success
	 */
	bool init(vk::Device device,
	          VulkanMemoryManager* memoryManager,
	          uint32_t graphicsQueueFamily,
	          uint32_t transferQueueFamily);

	/**
	 * @brief Shutdown and free all buffers
	 */
	void shutdown();

	/**
	 * @brief Set the current frame index for per-frame buffer selection
	 * Must be called at the start of each frame before any buffer updates
	 * @param frameIndex The current frame index (0 to MAX_FRAMES_IN_FLIGHT-1)
	 */
	void setCurrentFrame(uint32_t frameIndex);

	/**
	 * @brief Get the current frame index
	 */
	uint32_t getCurrentFrame() const { return m_currentFrame; }

	/**
	 * @brief Create a new buffer
	 * @param type The buffer type (Vertex, Index, Uniform)
	 * @param usage Usage hint for optimization
	 * @return Handle to the created buffer, or invalid handle on failure
	 */
	gr_buffer_handle createBuffer(BufferType type, BufferUsageHint usage);

	/**
	 * @brief Delete a buffer
	 * @param handle The buffer to delete
	 */
	void deleteBuffer(gr_buffer_handle handle);

	/**
	 * @brief Update buffer data (full replacement)
	 * @param handle The buffer to update
	 * @param size Size of data in bytes
	 * @param data Pointer to data
	 */
	void updateBufferData(gr_buffer_handle handle, size_t size, const void* data);

	/**
	 * @brief Update buffer data at an offset
	 * @param handle The buffer to update
	 * @param offset Offset in bytes
	 * @param size Size of data in bytes
	 * @param data Pointer to data
	 */
	void updateBufferDataOffset(gr_buffer_handle handle, size_t offset, size_t size, const void* data);

	/**
	 * @brief Map buffer for CPU access
	 * @param handle The buffer to map
	 * @return Pointer to mapped memory, or nullptr on failure
	 */
	void* mapBuffer(gr_buffer_handle handle);

	/**
	 * @brief Flush a range of a mapped buffer
	 * @param handle The buffer to flush
	 * @param offset Offset in bytes
	 * @param size Size of range in bytes
	 */
	void flushMappedBuffer(gr_buffer_handle handle, size_t offset, size_t size);

	/**
	 * @brief Bind uniform buffer to a binding slot
	 * @param blockType The uniform block type
	 * @param offset Offset within the buffer
	 * @param size Size of the bound range
	 * @param buffer The buffer to bind
	 */
	void bindUniformBuffer(uniform_block_type blockType, size_t offset, size_t size, gr_buffer_handle buffer);

	/**
	 * @brief Get the Vulkan buffer handle for the current frame
	 * @param handle The buffer handle
	 * @return The VkBuffer, or VK_NULL_HANDLE if invalid
	 */
	vk::Buffer getVkBuffer(gr_buffer_handle handle) const;

	/**
	 * @brief Get buffer size for the current frame's span
	 * @param handle The buffer handle
	 * @return Size in bytes, or 0 if invalid
	 */
	size_t getBufferSize(gr_buffer_handle handle) const;

	/**
	 * @brief Get the base offset for the current frame's span
	 * For streaming buffers, returns frameIndex * spanSize
	 * For static buffers, returns 0
	 * @param handle The buffer handle
	 * @return Byte offset for current frame's span
	 */
	size_t getFrameBaseOffset(gr_buffer_handle handle) const;

	/**
	 * @brief Check if a handle is valid
	 */
	bool isValidHandle(gr_buffer_handle handle) const;

	/**
	 * @brief Get statistics
	 */
	size_t getBufferCount() const { return m_activeBufferCount; }
	size_t getTotalBufferMemory() const { return m_totalBufferMemory; }

	/**
	 * @brief Get the constant white color buffer for fallback vertex colors
	 * This buffer contains vec4(1,1,1,1) for shaders expecting vertColor
	 */
	vk::Buffer getFallbackColorBuffer() const { return m_fallbackColorBuffer; }

	/**
	 * @brief Get the constant zero texcoord buffer for fallback vertex texcoords
	 * This buffer contains vec4(0,0,0,0) for shaders expecting vertTexCoord
	 */
	vk::Buffer getFallbackTexCoordBuffer() const { return m_fallbackTexCoordBuffer; }

	/**
	 * @brief Get the fallback uniform buffer for uninitialized descriptor bindings
	 * This buffer contains zeros and is used to pre-fill all UBO descriptor bindings
	 * to avoid undefined behavior from uninitialized descriptors after pool reset
	 */
	vk::Buffer getFallbackUniformBuffer() const { return m_fallbackUniformBuffer; }

	/**
	 * @brief Get the size of the fallback uniform buffer
	 */
	size_t getFallbackUniformBufferSize() const { return FALLBACK_UNIFORM_BUFFER_SIZE; }

	/**
	 * @brief Process deferred buffer destructions from previous frame
	 * Called at frame start to destroy buffers that were queued last frame
	 */
	void processDeferredDestructions();

private:
	/**
	 * @brief Queue a buffer for deferred destruction
	 * Buffer will be destroyed at the start of the next frame
	 */
	void queueDeferredDestruction(vk::Buffer buffer, VulkanAllocation allocation, size_t size);

	/**
	 * @brief Convert BufferType to Vulkan usage flags
	 */
	vk::BufferUsageFlags getVkUsageFlags(BufferType type) const;

	/**
	 * @brief Convert BufferUsageHint to memory usage
	 */
	MemoryUsage getMemoryUsage(BufferUsageHint hint) const;

	/**
	 * @brief Create or resize a buffer to fit the required span size
	 * For streaming buffers, ensures total size >= spanSize * MAX_FRAMES_IN_FLIGHT
	 * @param bufferObj The buffer object
	 * @param spanSize Required size per frame span
	 */
	bool createOrResizeBuffer(VulkanBufferObject& bufferObj, size_t spanSize);

	/**
	 * @brief Get buffer object from handle
	 */
	VulkanBufferObject* getBufferObject(gr_buffer_handle handle);
	const VulkanBufferObject* getBufferObject(gr_buffer_handle handle) const;

	vk::Device m_device;
	VulkanMemoryManager* m_memoryManager = nullptr;

	uint32_t m_graphicsQueueFamily = 0;
	uint32_t m_transferQueueFamily = 0;
	uint32_t m_currentFrame = 0;

	SCP_vector<VulkanBufferObject> m_buffers;
	SCP_vector<int> m_freeIndices;  // Recycled buffer indices

	// Deferred destruction queue - buffers destroyed mid-frame are queued here
	SCP_vector<PendingBufferDestruction> m_pendingDestructions;

	// Fallback color buffer containing white (1,1,1,1) for vertex data without colors
	vk::Buffer m_fallbackColorBuffer;
	VulkanAllocation m_fallbackColorAllocation;

	// Fallback texcoord buffer containing (0,0,0,0) for vertex data without texcoords
	vk::Buffer m_fallbackTexCoordBuffer;
	VulkanAllocation m_fallbackTexCoordAllocation;

	// Fallback uniform buffer (zeros) for uninitialized descriptor set UBO bindings
	static constexpr size_t FALLBACK_UNIFORM_BUFFER_SIZE = 4096;
	vk::Buffer m_fallbackUniformBuffer;
	VulkanAllocation m_fallbackUniformAllocation;

	size_t m_activeBufferCount = 0;
	size_t m_totalBufferMemory = 0;

	bool m_initialized = false;
};

// Global buffer manager instance (set during renderer init)
VulkanBufferManager* getBufferManager();
void setBufferManager(VulkanBufferManager* manager);

} // namespace vulkan
} // namespace graphics

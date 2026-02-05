#include "VulkanTexture.h"
#include "VulkanBuffer.h"
#include "VulkanDeletionQueue.h"

#include "bmpman/bmpman.h"
#include "ddsutils/ddsutils.h"
#include "globalincs/systemvars.h"

namespace graphics {
namespace vulkan {

namespace {
VulkanTextureManager* g_textureManager = nullptr;
}

VulkanTextureManager* getTextureManager()
{
	return g_textureManager;
}

void setTextureManager(VulkanTextureManager* manager)
{
	g_textureManager = manager;
}

// tcache_slot_vulkan implementation

void tcache_slot_vulkan::reset()
{
	image = nullptr;
	imageView = nullptr;
	allocation = VulkanAllocation();
	format = vk::Format::eUndefined;
	currentLayout = vk::ImageLayout::eUndefined;
	width = 0;
	height = 0;
	mipLevels = 1;
	arrayLayers = 1;
	bpp = 0;
	bitmapHandle = -1;
	arrayIndex = 0;
	used = false;
	framebuffer = nullptr;
	renderPass = nullptr;
	isRenderTarget = false;
	uScale = 1.0f;
	vScale = 1.0f;
}

// VulkanTextureManager implementation

VulkanTextureManager::VulkanTextureManager() = default;

VulkanTextureManager::~VulkanTextureManager()
{
	if (m_initialized) {
		shutdown();
	}
}

bool VulkanTextureManager::init(vk::Device device, vk::PhysicalDevice physicalDevice,
                                VulkanMemoryManager* memoryManager,
                                vk::CommandPool commandPool, vk::Queue graphicsQueue)
{
	if (m_initialized) {
		mprintf(("VulkanTextureManager::init called when already initialized!\n"));
		return false;
	}

	m_device = device;
	m_physicalDevice = physicalDevice;
	m_memoryManager = memoryManager;
	m_commandPool = commandPool;
	m_graphicsQueue = graphicsQueue;

	// Query device limits
	auto properties = physicalDevice.getProperties();
	m_maxTextureSize = properties.limits.maxImageDimension2D;
	m_maxAnisotropy = properties.limits.maxSamplerAnisotropy;

	mprintf(("Vulkan Texture Manager initialized\n"));
	mprintf(("  Max texture size: %u\n", m_maxTextureSize));
	mprintf(("  Max anisotropy: %.1f\n", m_maxAnisotropy));

	// Create default sampler
	vk::SamplerCreateInfo samplerInfo;
	samplerInfo.magFilter = vk::Filter::eLinear;
	samplerInfo.minFilter = vk::Filter::eLinear;
	samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
	samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
	samplerInfo.anisotropyEnable = (m_maxAnisotropy > 1.0f);
	samplerInfo.maxAnisotropy = m_maxAnisotropy;
	samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
	samplerInfo.unnormalizedCoordinates = false;
	samplerInfo.compareEnable = false;
	samplerInfo.compareOp = vk::CompareOp::eAlways;
	samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

	try {
		m_defaultSampler = m_device.createSampler(samplerInfo);
	} catch (const vk::SystemError& e) {
		mprintf(("Failed to create default sampler: %s\n", e.what()));
		return false;
	}

	// Create fallback 1x1 white texture for unbound descriptor slots
	if (!createImage(1, 1, 1, vk::Format::eR8G8B8A8Unorm, vk::ImageTiling::eOptimal,
	                 vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
	                 MemoryUsage::GpuOnly, m_fallbackTexture, m_fallbackTextureAllocation)) {
		mprintf(("Failed to create fallback texture!\n"));
		return false;
	}

	m_fallbackTextureView = createImageView(m_fallbackTexture, vk::Format::eR8G8B8A8Unorm,
	                                         vk::ImageAspectFlagBits::eColor, 1, true);
	if (!m_fallbackTextureView) {
		mprintf(("Failed to create fallback texture view!\n"));
		m_device.destroyImage(m_fallbackTexture);
		m_memoryManager->freeAllocation(m_fallbackTextureAllocation);
		return false;
	}

	// Upload white pixel data to fallback texture
	{
		// Create staging buffer with white pixel (RGBA: 255, 255, 255, 255)
		uint32_t whitePixel = 0xFFFFFFFF;
		vk::DeviceSize bufferSize = 4;

		// Allocate staging buffer
		vk::BufferCreateInfo bufferInfo;
		bufferInfo.size = bufferSize;
		bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		vk::Buffer stagingBuffer = m_device.createBuffer(bufferInfo);
		VulkanAllocation stagingAlloc;
		m_memoryManager->allocateBufferMemory(stagingBuffer, MemoryUsage::CpuToGpu, stagingAlloc);

		// Copy pixel data to staging buffer
		void* mappedData = m_device.mapMemory(stagingAlloc.memory, stagingAlloc.offset, bufferSize);
		memcpy(mappedData, &whitePixel, sizeof(whitePixel));
		m_device.unmapMemory(stagingAlloc.memory);

		// Transition image for transfer
		transitionImageLayout(m_fallbackTexture, vk::Format::eR8G8B8A8Unorm,
		                      vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, 1);

		// Copy buffer to image
		copyBufferToImage(stagingBuffer, m_fallbackTexture, 1, 1);

		// Transition image for shader access
		transitionImageLayout(m_fallbackTexture, vk::Format::eR8G8B8A8Unorm,
		                      vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, 1);

		// Clean up staging buffer
		m_device.destroyBuffer(stagingBuffer);
		m_memoryManager->freeAllocation(stagingAlloc);
	}

	mprintf(("Created fallback texture\n"));

	m_initialized = true;
	return true;
}

void VulkanTextureManager::shutdown()
{
	if (!m_initialized) {
		return;
	}

	// Destroy fallback texture
	if (m_fallbackTextureView) {
		m_device.destroyImageView(m_fallbackTextureView);
		m_fallbackTextureView = nullptr;
	}
	if (m_fallbackTexture) {
		m_device.destroyImage(m_fallbackTexture);
		m_fallbackTexture = nullptr;
	}
	if (m_fallbackTextureAllocation.memory != VK_NULL_HANDLE) {
		m_memoryManager->freeAllocation(m_fallbackTextureAllocation);
	}

	// Destroy samplers
	if (m_defaultSampler) {
		m_device.destroySampler(m_defaultSampler);
		m_defaultSampler = nullptr;
	}

	for (auto& pair : m_samplerCache) {
		m_device.destroySampler(pair.second);
	}
	m_samplerCache.clear();

	m_initialized = false;
	mprintf(("Vulkan Texture Manager shutdown\n"));
}

void VulkanTextureManager::bm_init(bitmap_slot* slot)
{
	if (!m_initialized || !slot) {
		return;
	}

	// Allocate Vulkan-specific data
	if (slot->gr_info == nullptr) {
		slot->gr_info = new tcache_slot_vulkan();
	} else {
		static_cast<tcache_slot_vulkan*>(slot->gr_info)->reset();
	}
}

void VulkanTextureManager::bm_create(bitmap_slot* slot)
{
	if (!m_initialized || !slot) {
		return;
	}

	// Ensure gr_info is allocated
	if (slot->gr_info == nullptr) {
		slot->gr_info = new tcache_slot_vulkan();
	}
}

void VulkanTextureManager::bm_free_data(bitmap_slot* slot, bool release)
{
	if (!m_initialized || !slot || !slot->gr_info) {
		return;
	}

	auto* ts = static_cast<tcache_slot_vulkan*>(slot->gr_info);
	auto* deletionQueue = getDeletionQueue();

	// Queue resources for deferred destruction to avoid destroying
	// resources that may still be referenced by in-flight command buffers
	if (deletionQueue) {
		if (ts->framebuffer) {
			deletionQueue->queueFramebuffer(ts->framebuffer);
			ts->framebuffer = nullptr;
		}

		if (ts->renderPass) {
			deletionQueue->queueRenderPass(ts->renderPass);
			ts->renderPass = nullptr;
		}

		if (ts->imageView) {
			deletionQueue->queueImageView(ts->imageView);
			ts->imageView = nullptr;
		}

		if (ts->image) {
			deletionQueue->queueImage(ts->image, ts->allocation);
			ts->image = nullptr;
			ts->allocation = VulkanAllocation{};  // Clear to prevent double-free
		}
	} else {
		// Fallback to immediate destruction if no deletion queue
		if (ts->framebuffer) {
			m_device.destroyFramebuffer(ts->framebuffer);
			ts->framebuffer = nullptr;
		}

		if (ts->renderPass) {
			m_device.destroyRenderPass(ts->renderPass);
			ts->renderPass = nullptr;
		}

		if (ts->imageView) {
			m_device.destroyImageView(ts->imageView);
			ts->imageView = nullptr;
		}

		if (ts->image) {
			m_device.destroyImage(ts->image);
			ts->image = nullptr;
		}

		if (ts->allocation.memory != VK_NULL_HANDLE) {
			m_memoryManager->freeAllocation(ts->allocation);
		}
	}

	ts->reset();

	if (release) {
		delete ts;
		slot->gr_info = nullptr;
	}
}

bool VulkanTextureManager::bm_data(int handle, bitmap* bm)
{
	static int callCount = 0;
	if (callCount < 20) {
		mprintf(("VulkanTextureManager::bm_data #%d: handle=%d bm=%p bm->data=%p\n",
			callCount++, handle, bm, bm ? reinterpret_cast<void*>(bm->data) : nullptr));
	}

	if (!m_initialized || !bm || !bm->data) {
		return false;
	}

	auto* slot = bm_get_slot(handle, true);
	if (!slot) {
		return false;
	}

	// Ensure slot is initialized
	if (!slot->gr_info) {
		bm_init(slot);
	}

	auto* ts = static_cast<tcache_slot_vulkan*>(slot->gr_info);

	// Determine format
	vk::Format format = bppToVkFormat(bm->bpp);
	if (format == vk::Format::eUndefined) {
		mprintf(("VulkanTextureManager::bm_data: Unsupported bpp %d\n", bm->bpp));
		return false;
	}

	uint32_t width = static_cast<uint32_t>(bm->w);
	uint32_t height = static_cast<uint32_t>(bm->h);
	uint32_t mipLevels = 1;  // For now, no mipmaps

	// If texture already exists with same dimensions, just update data
	if (ts->image && ts->width == width && ts->height == height && ts->format == format) {
		// Update existing texture - would use staging buffer
		// For now, recreate
	}

	// Free existing resources
	if (ts->image) {
		if (ts->imageView) {
			m_device.destroyImageView(ts->imageView);
			ts->imageView = nullptr;
		}
		m_device.destroyImage(ts->image);
		ts->image = nullptr;
		m_memoryManager->freeAllocation(ts->allocation);
	}

	// Create image
	vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;

	if (!createImage(width, height, mipLevels, format, vk::ImageTiling::eOptimal,
	                 usage, MemoryUsage::GpuOnly, ts->image, ts->allocation)) {
		mprintf(("Failed to create texture image!\n"));
		return false;
	}

	// Create image view
	ts->imageView = createImageView(ts->image, format, vk::ImageAspectFlagBits::eColor, mipLevels, true);
	if (!ts->imageView) {
		mprintf(("Failed to create texture image view!\n"));
		m_device.destroyImage(ts->image);
		ts->image = nullptr;
		m_memoryManager->freeAllocation(ts->allocation);
		return false;
	}

	// Calculate data size
	size_t bytesPerPixel = bm->bpp / 8;
	size_t dataSize = width * height * bytesPerPixel;

	// Create staging buffer
	vk::BufferCreateInfo bufferInfo;
	bufferInfo.size = dataSize;
	bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
	bufferInfo.sharingMode = vk::SharingMode::eExclusive;

	vk::Buffer stagingBuffer;
	VulkanAllocation stagingAllocation;

	try {
		stagingBuffer = m_device.createBuffer(bufferInfo);
	} catch (const vk::SystemError& e) {
		mprintf(("Failed to create staging buffer: %s\n", e.what()));
		return false;
	}

	if (!m_memoryManager->allocateBufferMemory(stagingBuffer, MemoryUsage::CpuOnly, stagingAllocation)) {
		m_device.destroyBuffer(stagingBuffer);
		return false;
	}

	// Copy data to staging buffer
	void* mapped = m_memoryManager->mapMemory(stagingAllocation);
	if (mapped) {
		memcpy(mapped, reinterpret_cast<const void*>(bm->data), dataSize);
		m_memoryManager->flushMemory(stagingAllocation, 0, dataSize);
		m_memoryManager->unmapMemory(stagingAllocation);
	}

	// Transition image layout and copy
	transitionImageLayout(ts->image, format, vk::ImageLayout::eUndefined,
	                      vk::ImageLayout::eTransferDstOptimal, mipLevels);

	copyBufferToImage(stagingBuffer, ts->image, width, height);

	transitionImageLayout(ts->image, format, vk::ImageLayout::eTransferDstOptimal,
	                      vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);

	// Cleanup staging buffer
	m_device.destroyBuffer(stagingBuffer);
	m_memoryManager->freeAllocation(stagingAllocation);

	// Update slot info
	ts->width = width;
	ts->height = height;
	ts->format = format;
	ts->mipLevels = mipLevels;
	ts->bpp = bm->bpp;
	ts->bitmapHandle = handle;
	ts->currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	ts->used = true;
	ts->uScale = 1.0f;
	ts->vScale = 1.0f;

	return true;
}

int VulkanTextureManager::bm_make_render_target(int handle, int* width, int* height,
                                                 int* bpp, int* mm_lvl, int flags)
{
	if (!m_initialized || !width || !height) {
		return 0;
	}

	// Clamp to max size
	if (static_cast<uint32_t>(*width) > m_maxTextureSize) {
		*width = static_cast<int>(m_maxTextureSize);
	}
	if (static_cast<uint32_t>(*height) > m_maxTextureSize) {
		*height = static_cast<int>(m_maxTextureSize);
	}

	auto* slot = bm_get_slot(handle, true);
	if (!slot) {
		return 0;
	}

	if (!slot->gr_info) {
		bm_init(slot);
	}

	auto* ts = static_cast<tcache_slot_vulkan*>(slot->gr_info);

	// Free any existing resources
	bm_free_data(slot, false);

	uint32_t w = static_cast<uint32_t>(*width);
	uint32_t h = static_cast<uint32_t>(*height);
	uint32_t mipLevels = 1;

	if (flags & BMP_FLAG_RENDER_TARGET_MIPMAP) {
		mipLevels = calculateMipLevels(w, h);
	}

	vk::Format format = vk::Format::eR8G8B8A8Unorm;

	// Create image for render target
	vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eColorAttachment |
	                            vk::ImageUsageFlagBits::eSampled |
	                            vk::ImageUsageFlagBits::eTransferSrc;

	if (flags & BMP_FLAG_RENDER_TARGET_MIPMAP) {
		usage |= vk::ImageUsageFlagBits::eTransferDst;  // For mipmap generation
	}

	if (!createImage(w, h, mipLevels, format, vk::ImageTiling::eOptimal,
	                 usage, MemoryUsage::GpuOnly, ts->image, ts->allocation)) {
		mprintf(("Failed to create render target image!\n"));
		return 0;
	}

	// Create image view (use array view for shader compatibility)
	ts->imageView = createImageView(ts->image, format, vk::ImageAspectFlagBits::eColor, mipLevels, true);
	if (!ts->imageView) {
		m_device.destroyImage(ts->image);
		ts->image = nullptr;
		m_memoryManager->freeAllocation(ts->allocation);
		return 0;
	}

	// Create render pass for this target
	vk::AttachmentDescription colorAttachment;
	colorAttachment.format = format;
	colorAttachment.samples = vk::SampleCountFlagBits::e1;
	colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
	colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;
	colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
	colorAttachment.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

	vk::AttachmentReference colorAttachmentRef;
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

	vk::SubpassDescription subpass;
	subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;

	vk::RenderPassCreateInfo renderPassInfo;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	try {
		ts->renderPass = m_device.createRenderPass(renderPassInfo);
	} catch (const vk::SystemError& e) {
		mprintf(("Failed to create render pass: %s\n", e.what()));
		m_device.destroyImageView(ts->imageView);
		m_device.destroyImage(ts->image);
		ts->image = nullptr;
		ts->imageView = nullptr;
		m_memoryManager->freeAllocation(ts->allocation);
		return 0;
	}

	// Create framebuffer
	vk::FramebufferCreateInfo framebufferInfo;
	framebufferInfo.renderPass = ts->renderPass;
	framebufferInfo.attachmentCount = 1;
	framebufferInfo.pAttachments = &ts->imageView;
	framebufferInfo.width = w;
	framebufferInfo.height = h;
	framebufferInfo.layers = 1;

	try {
		ts->framebuffer = m_device.createFramebuffer(framebufferInfo);
	} catch (const vk::SystemError& e) {
		mprintf(("Failed to create framebuffer: %s\n", e.what()));
		m_device.destroyRenderPass(ts->renderPass);
		m_device.destroyImageView(ts->imageView);
		m_device.destroyImage(ts->image);
		ts->image = nullptr;
		ts->imageView = nullptr;
		ts->renderPass = nullptr;
		m_memoryManager->freeAllocation(ts->allocation);
		return 0;
	}

	// Update slot info
	ts->width = w;
	ts->height = h;
	ts->format = format;
	ts->mipLevels = mipLevels;
	ts->bpp = 32;
	ts->bitmapHandle = handle;
	ts->isRenderTarget = true;
	ts->used = true;
	ts->uScale = 1.0f;
	ts->vScale = 1.0f;
	ts->currentLayout = vk::ImageLayout::eUndefined;

	if (bpp) {
		*bpp = 32;
	}
	if (mm_lvl) {
		*mm_lvl = static_cast<int>(mipLevels);
	}

	mprintf(("Created Vulkan render target: %ux%u\n", w, h));
	return 1;
}

int VulkanTextureManager::bm_set_render_target(int handle, int face)
{
	if (!m_initialized) {
		return 0;
	}

	// handle < 0 means reset to default framebuffer
	if (handle < 0) {
		m_currentRenderTarget = -1;
		return 1;
	}

	auto* slot = bm_get_slot(handle, true);
	if (!slot || !slot->gr_info) {
		return 0;
	}

	auto* ts = static_cast<tcache_slot_vulkan*>(slot->gr_info);
	if (!ts->isRenderTarget || !ts->framebuffer) {
		return 0;
	}

	m_currentRenderTarget = handle;
	(void)face;  // TODO: Handle cubemap faces

	return 1;
}

void VulkanTextureManager::update_texture(int bitmap_handle, int bpp, const ubyte* data,
                                          int width, int height)
{
	if (!m_initialized || !data) {
		return;
	}

	auto* slot = bm_get_slot(bitmap_handle, true);
	if (!slot || !slot->gr_info) {
		return;
	}

	auto* ts = static_cast<tcache_slot_vulkan*>(slot->gr_info);
	if (!ts->image) {
		return;
	}

	// TODO: Implement partial texture update using staging buffer
	(void)bpp;
	(void)width;
	(void)height;
}

void VulkanTextureManager::get_bitmap_from_texture(void* data_out, int bitmap_num)
{
	if (!m_initialized || !data_out) {
		return;
	}

	// TODO: Implement texture readback
	(void)bitmap_num;
}

vk::Sampler VulkanTextureManager::getSampler(vk::Filter magFilter, vk::Filter minFilter,
                                              vk::SamplerAddressMode addressMode,
                                              bool enableAnisotropy, float maxAnisotropy,
                                              bool enableMipmaps)
{
	// Create a key from sampler state
	uint64_t key = 0;
	key |= static_cast<uint64_t>(magFilter) << 0;
	key |= static_cast<uint64_t>(minFilter) << 4;
	key |= static_cast<uint64_t>(addressMode) << 8;
	key |= static_cast<uint64_t>(enableAnisotropy) << 16;
	key |= static_cast<uint64_t>(enableMipmaps) << 17;
	key |= static_cast<uint64_t>(maxAnisotropy * 10) << 24;

	auto it = m_samplerCache.find(key);
	if (it != m_samplerCache.end()) {
		return it->second;
	}

	// Create new sampler
	vk::SamplerCreateInfo samplerInfo;
	samplerInfo.magFilter = magFilter;
	samplerInfo.minFilter = minFilter;
	samplerInfo.addressModeU = addressMode;
	samplerInfo.addressModeV = addressMode;
	samplerInfo.addressModeW = addressMode;
	samplerInfo.anisotropyEnable = enableAnisotropy && (m_maxAnisotropy > 1.0f);
	samplerInfo.maxAnisotropy = std::min(maxAnisotropy, m_maxAnisotropy);
	samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
	samplerInfo.unnormalizedCoordinates = false;
	samplerInfo.compareEnable = false;
	samplerInfo.compareOp = vk::CompareOp::eAlways;
	samplerInfo.mipmapMode = enableMipmaps ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = enableMipmaps ? VK_LOD_CLAMP_NONE : 0.0f;

	try {
		vk::Sampler sampler = m_device.createSampler(samplerInfo);
		m_samplerCache[key] = sampler;
		return sampler;
	} catch (const vk::SystemError& e) {
		mprintf(("Failed to create sampler: %s\n", e.what()));
		return m_defaultSampler;
	}
}

vk::Sampler VulkanTextureManager::getDefaultSampler()
{
	return m_defaultSampler;
}

vk::ImageView VulkanTextureManager::getFallbackTextureView()
{
	return m_fallbackTextureView;
}

tcache_slot_vulkan* VulkanTextureManager::getTextureSlot(int handle)
{
	auto* slot = bm_get_slot(handle, true);
	if (!slot || !slot->gr_info) {
		return nullptr;
	}
	return static_cast<tcache_slot_vulkan*>(slot->gr_info);
}

bool VulkanTextureManager::isTextureValid(int handle)
{
	auto* ts = getTextureSlot(handle);
	return ts && ts->image && ts->imageView && ts->used;
}

vk::Format VulkanTextureManager::bppToVkFormat(int bpp, bool compressed, int compressionType)
{
	if (compressed) {
		// DDS compression types
		switch (compressionType) {
		case DDS_DXT1:
			return vk::Format::eBc1RgbaUnormBlock;
		case DDS_DXT3:
			return vk::Format::eBc2UnormBlock;
		case DDS_DXT5:
			return vk::Format::eBc3UnormBlock;
		default:
			return vk::Format::eUndefined;
		}
	}

	switch (bpp) {
	case 8:
		return vk::Format::eR8Unorm;
	case 16:
		return vk::Format::eR8G8Unorm;
	case 24:
		// FSO uses BGR format for 24bpp
		return vk::Format::eB8G8R8Unorm;
	case 32:
		// FSO uses BGRA format (BMP_AARRGGBB = BGRA in memory)
		return vk::Format::eB8G8R8A8Unorm;
	default:
		return vk::Format::eUndefined;
	}
}

void VulkanTextureManager::transitionImageLayout(vk::Image image, vk::Format format,
                                                  vk::ImageLayout oldLayout,
                                                  vk::ImageLayout newLayout,
                                                  uint32_t mipLevels)
{
	vk::CommandBuffer commandBuffer = beginSingleTimeCommands();

	vk::ImageMemoryBarrier barrier;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = mipLevels;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	vk::PipelineStageFlags sourceStage;
	vk::PipelineStageFlags destinationStage;

	if (oldLayout == vk::ImageLayout::eUndefined &&
	    newLayout == vk::ImageLayout::eTransferDstOptimal) {
		barrier.srcAccessMask = {};
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
		destinationStage = vk::PipelineStageFlagBits::eTransfer;
	} else if (oldLayout == vk::ImageLayout::eTransferDstOptimal &&
	           newLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		sourceStage = vk::PipelineStageFlagBits::eTransfer;
		destinationStage = vk::PipelineStageFlagBits::eFragmentShader;
	} else if (oldLayout == vk::ImageLayout::eUndefined &&
	           newLayout == vk::ImageLayout::eColorAttachmentOptimal) {
		barrier.srcAccessMask = {};
		barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		sourceStage = vk::PipelineStageFlagBits::eTopOfPipe;
		destinationStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	} else {
		// Generic transition
		barrier.srcAccessMask = vk::AccessFlagBits::eMemoryWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
		sourceStage = vk::PipelineStageFlagBits::eAllCommands;
		destinationStage = vk::PipelineStageFlagBits::eAllCommands;
	}

	(void)format;  // Format might be needed for depth/stencil transitions

	commandBuffer.pipelineBarrier(sourceStage, destinationStage, {},
	                               nullptr, nullptr, barrier);

	endSingleTimeCommands(commandBuffer);
}

void VulkanTextureManager::generateMipmaps(int handle)
{
	auto* ts = getTextureSlot(handle);
	if (!ts || !ts->image || ts->mipLevels <= 1) {
		return;
	}

	// TODO: Implement mipmap generation using vkCmdBlitImage
}

void VulkanTextureManager::flushCache()
{
	// TODO: Implement cache flushing
}

void VulkanTextureManager::frameStart()
{
	// Called at the start of each frame
}

bool VulkanTextureManager::createImage(uint32_t width, uint32_t height, uint32_t mipLevels,
                                        vk::Format format, vk::ImageTiling tiling,
                                        vk::ImageUsageFlags usage, MemoryUsage memUsage,
                                        vk::Image& image, VulkanAllocation& allocation)
{
	vk::ImageCreateInfo imageInfo;
	imageInfo.imageType = vk::ImageType::e2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = tiling;
	imageInfo.initialLayout = vk::ImageLayout::eUndefined;
	imageInfo.usage = usage;
	imageInfo.sharingMode = vk::SharingMode::eExclusive;
	imageInfo.samples = vk::SampleCountFlagBits::e1;

	try {
		image = m_device.createImage(imageInfo);
	} catch (const vk::SystemError& e) {
		mprintf(("Failed to create image: %s\n", e.what()));
		return false;
	}

	if (!m_memoryManager->allocateImageMemory(image, memUsage, allocation)) {
		m_device.destroyImage(image);
		image = nullptr;
		return false;
	}

	return true;
}

vk::ImageView VulkanTextureManager::createImageView(vk::Image image, vk::Format format,
                                                     vk::ImageAspectFlags aspectFlags,
                                                     uint32_t mipLevels,
                                                     bool asArray)
{
	vk::ImageViewCreateInfo viewInfo;
	viewInfo.image = image;
	// Use 2DArray view type for shader compatibility (sampler2DArray in shaders)
	// Even single-layer textures are viewed as arrays with layerCount=1
	viewInfo.viewType = asArray ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = aspectFlags;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	try {
		return m_device.createImageView(viewInfo);
	} catch (const vk::SystemError& e) {
		mprintf(("Failed to create image view: %s\n", e.what()));
		return nullptr;
	}
}

void VulkanTextureManager::copyBufferToImage(vk::Buffer buffer, vk::Image image,
                                              uint32_t width, uint32_t height)
{
	vk::CommandBuffer commandBuffer = beginSingleTimeCommands();

	vk::BufferImageCopy region;
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = vk::Offset3D{0, 0, 0};
	region.imageExtent = vk::Extent3D{width, height, 1};

	commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, region);

	endSingleTimeCommands(commandBuffer);
}

vk::CommandBuffer VulkanTextureManager::beginSingleTimeCommands()
{
	vk::CommandBufferAllocateInfo allocInfo;
	allocInfo.level = vk::CommandBufferLevel::ePrimary;
	allocInfo.commandPool = m_commandPool;
	allocInfo.commandBufferCount = 1;

	vk::CommandBuffer commandBuffer = m_device.allocateCommandBuffers(allocInfo)[0];

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	commandBuffer.begin(beginInfo);

	return commandBuffer;
}

void VulkanTextureManager::endSingleTimeCommands(vk::CommandBuffer commandBuffer)
{
	commandBuffer.end();

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	m_graphicsQueue.submit(submitInfo, nullptr);
	m_graphicsQueue.waitIdle();

	m_device.freeCommandBuffers(m_commandPool, commandBuffer);
}

uint32_t VulkanTextureManager::calculateMipLevels(uint32_t width, uint32_t height)
{
	return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
}

} // namespace vulkan
} // namespace graphics

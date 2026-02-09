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
	Assertion(g_textureManager != nullptr, "Vulkan TextureManager not initialized!");
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
	framebufferView = nullptr;
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
	// Use ClampToEdge by default to match OpenGL's behavior for UI/interface textures.
	// OpenGL creates all textures with GL_CLAMP_TO_EDGE and only switches to GL_REPEAT
	// for 3D model textures at bind time (excluding AABITMAP, INTERFACE, CUBEMAP types).
	// Using eRepeat here causes visible 1-pixel seams on UI bitmaps where edge texels
	// blend with the opposite edge via linear filtering.
	samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
	samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
	samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
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

	if (ts->framebufferView) {
		deletionQueue->queueImageView(ts->framebufferView);
		ts->framebufferView = nullptr;
	}

	if (ts->image) {
		deletionQueue->queueImage(ts->image, ts->allocation);
		ts->image = nullptr;
		ts->allocation = VulkanAllocation{};  // Clear to prevent double-free
	}

	ts->reset();

	if (release) {
		delete ts;
		slot->gr_info = nullptr;
	}
}

bool VulkanTextureManager::bm_data(int handle, bitmap* bm, int compType)
{
	static int callCount = 0;
	if (callCount < 20) {
		mprintf(("VulkanTextureManager::bm_data #%d: handle=%d bm=%p bm->data=%p compType=%d\n",
			callCount++, handle, bm, bm ? reinterpret_cast<void*>(bm->data) : nullptr, compType));
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

	uint32_t width = static_cast<uint32_t>(bm->w);
	uint32_t height = static_cast<uint32_t>(bm->h);
	uint32_t mipLevels = 1;
	bool autoGenerateMips = false;
	bool isCompressed = (compType == DDS_DXT1 || compType == DDS_DXT3 ||
	                     compType == DDS_DXT5 || compType == DDS_BC7);

	static int fmtLogCount = 0;
	if (fmtLogCount < 30) {
		mprintf(("VulkanTextureManager::bm_data: handle=%d w=%d h=%d bpp=%d true_bpp=%d flags=0x%x compType=%d\n",
			handle, bm->w, bm->h, bm->bpp, bm->true_bpp, bm->flags, compType));
		fmtLogCount++;
	}

	// Determine format and data size
	vk::Format format;
	size_t dataSize;
	size_t blockSize = 0;
	SCP_vector<vk::BufferImageCopy> copyRegions;

	if (isCompressed) {
		format = bppToVkFormat(bm->bpp, true, compType);
		if (format == vk::Format::eUndefined) {
			mprintf(("VulkanTextureManager::bm_data: Unsupported compression type %d\n", compType));
			return false;
		}

		blockSize = (compType == DDS_DXT1) ? 8 : 16;

		// Get pre-baked mipmap count from DDS file
		mipLevels = static_cast<uint32_t>(bm_get_num_mipmaps(handle));
		if (mipLevels < 1) {
			mipLevels = 1;
		}

		// Calculate total data size for all mip levels and build copy regions
		dataSize = 0;
		uint32_t mipW = width;
		uint32_t mipH = height;
		for (uint32_t i = 0; i < mipLevels; i++) {
			uint32_t blocksW = (mipW + 3) / 4;
			uint32_t blocksH = (mipH + 3) / 4;
			size_t mipSize = blocksW * blocksH * blockSize;

			vk::BufferImageCopy region;
			region.bufferOffset = static_cast<vk::DeviceSize>(dataSize);
			region.bufferRowLength = 0;
			region.bufferImageHeight = 0;
			region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			region.imageSubresource.mipLevel = i;
			region.imageSubresource.baseArrayLayer = 0;
			region.imageSubresource.layerCount = 1;
			region.imageOffset = vk::Offset3D(0, 0, 0);
			region.imageExtent = vk::Extent3D(mipW, mipH, 1);
			copyRegions.push_back(region);

			dataSize += mipSize;
			mipW = std::max(1u, mipW / 2);
			mipH = std::max(1u, mipH / 2);
		}
	} else {
		format = bppToVkFormat(bm->bpp);
		if (format == vk::Format::eUndefined) {
			mprintf(("VulkanTextureManager::bm_data: Unsupported bpp %d\n", bm->bpp));
			return false;
		}

		// 24bpp textures uploaded as 32bpp (Vulkan doesn't support 24bpp optimal tiling)
		size_t dstBytesPerPixel = (bm->bpp == 24) ? 4 : (bm->bpp / 8);
		dataSize = width * height * dstBytesPerPixel;

		// Auto-generate mipmaps for textures whose files originally had them.
		// This only triggers for uncompressed textures that were originally DDS
		// with mipmaps but got decompressed by a non-DDS lock path.
		if (width > 4 && height > 4) {
			int numMipmaps = bm_get_num_mipmaps(handle);
			if (numMipmaps > 1) {
				vk::FormatProperties fmtProps = m_physicalDevice.getFormatProperties(format);
				if ((fmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear) &&
				    (fmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitSrc) &&
				    (fmtProps.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst)) {
					mipLevels = calculateMipLevels(width, height);
					autoGenerateMips = true;
				}
			}
		}
	}

	// If texture already exists with same dimensions, just update data
	if (ts->image && ts->width == width && ts->height == height && ts->format == format) {
		// Update existing texture - would use staging buffer
		// For now, recreate
	}

	// Defer destruction of existing resources — they may still be referenced
	// by in-flight render or upload command buffers
	if (ts->image) {
		auto* deletionQueue = getDeletionQueue();
		if (ts->imageView) {
			deletionQueue->queueImageView(ts->imageView);
			ts->imageView = nullptr;
		}
		deletionQueue->queueImage(ts->image, ts->allocation);
		ts->image = nullptr;
		ts->allocation = VulkanAllocation{};
	}

	// Create image
	vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
	if (autoGenerateMips) {
		usage |= vk::ImageUsageFlagBits::eTransferSrc;  // Needed for vkCmdBlitImage mipmap generation
	}

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
		if (isCompressed) {
			// Compressed data: copy raw block data directly (includes all mip levels)
			memcpy(mapped, reinterpret_cast<const void*>(bm->data), dataSize);
		} else if (bm->bpp == 24) {
			// Convert BGR (3 bytes) to BGRA (4 bytes), adding alpha=255
			const uint8_t* src = reinterpret_cast<const uint8_t*>(bm->data);
			uint8_t* dst = static_cast<uint8_t*>(mapped);
			size_t pixelCount = width * height;
			for (size_t i = 0; i < pixelCount; ++i) {
				dst[0] = src[0];  // B
				dst[1] = src[1];  // G
				dst[2] = src[2];  // R
				dst[3] = 255;     // A
				src += 3;
				dst += 4;
			}
		} else {
			memcpy(mapped, reinterpret_cast<const void*>(bm->data), dataSize);
		}
		m_memoryManager->flushMemory(stagingAllocation, 0, dataSize);
		m_memoryManager->unmapMemory(stagingAllocation);
	}

	// Record transitions + copy (+ optional mipmap generation) and submit async
	vk::CommandBuffer cmd = beginSingleTimeCommands();
	recordUploadCommands(cmd, ts->image, stagingBuffer, format, width, height,
	                     mipLevels, vk::ImageLayout::eUndefined, autoGenerateMips, copyRegions);
	submitUploadAsync(cmd, stagingBuffer, stagingAllocation);

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

	// For mipmapped render targets, create a single-mip view for framebuffer use
	// (framebuffer attachments must have levelCount == 1)
	if (mipLevels > 1) {
		ts->framebufferView = createImageView(ts->image, format, vk::ImageAspectFlagBits::eColor, 1, true);
		if (!ts->framebufferView) {
			m_device.destroyImageView(ts->imageView);
			m_device.destroyImage(ts->image);
			ts->image = nullptr;
			ts->imageView = nullptr;
			m_memoryManager->freeAllocation(ts->allocation);
			return 0;
		}
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
	// Use framebufferView (single-mip) if available, otherwise imageView
	vk::ImageView fbAttachment = ts->framebufferView ? ts->framebufferView : ts->imageView;
	vk::FramebufferCreateInfo framebufferInfo;
	framebufferInfo.renderPass = ts->renderPass;
	framebufferInfo.attachmentCount = 1;
	framebufferInfo.pAttachments = &fbAttachment;
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

	uint32_t w = static_cast<uint32_t>(width);
	uint32_t h = static_cast<uint32_t>(height);

	// Verify dimensions match existing texture
	if (ts->width != w || ts->height != h) {
		mprintf(("VulkanTextureManager::update_texture: Size mismatch (%ux%u vs %ux%u)\n",
			w, h, ts->width, ts->height));
		return;
	}

	// Use bppToVkFormat to determine format, matching how bm_data creates textures
	vk::Format format = bppToVkFormat(bpp);
	if (format == vk::Format::eUndefined) {
		mprintf(("VulkanTextureManager::update_texture: Unsupported bpp %d\n", bpp));
		return;
	}

	// Calculate staging buffer size (24bpp is uploaded as 32bpp BGRA)
	size_t srcBytesPerPixel = bpp / 8;
	size_t dstBytesPerPixel = (bpp == 24) ? 4 : srcBytesPerPixel;
	size_t dataSize = w * h * dstBytesPerPixel;

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
		mprintf(("VulkanTextureManager::update_texture: Failed to create staging buffer: %s\n", e.what()));
		return;
	}

	if (!m_memoryManager->allocateBufferMemory(stagingBuffer, MemoryUsage::CpuOnly, stagingAllocation)) {
		m_device.destroyBuffer(stagingBuffer);
		return;
	}

	// Copy data to staging buffer
	void* mapped = m_memoryManager->mapMemory(stagingAllocation);
	if (mapped) {
		if (bpp == 24) {
			// Convert BGR (3 bytes) to BGRA (4 bytes), adding alpha=255
			const uint8_t* src = data;
			uint8_t* dst = static_cast<uint8_t*>(mapped);
			size_t pixelCount = w * h;
			for (size_t i = 0; i < pixelCount; ++i) {
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				dst[3] = 255;
				src += 3;
				dst += 4;
			}
		} else {
			memcpy(mapped, data, dataSize);
		}
		m_memoryManager->flushMemory(stagingAllocation, 0, dataSize);
		m_memoryManager->unmapMemory(stagingAllocation);
	}

	// Record transitions + copy into a single command buffer and submit async
	vk::CommandBuffer cmd = beginSingleTimeCommands();
	recordUploadCommands(cmd, ts->image, stagingBuffer, format, w, h,
	                     ts->mipLevels, ts->currentLayout);
	submitUploadAsync(cmd, stagingBuffer, stagingAllocation);

	// Update layout tracking
	ts->currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
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
	samplerInfo.maxAnisotropy = std::max(1.0f, std::min(maxAnisotropy > 0.0f ? maxAnisotropy : m_maxAnisotropy, m_maxAnisotropy));
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
		case DDS_BC7:
			return vk::Format::eBc7UnormBlock;
		default:
			return vk::Format::eUndefined;
		}
	}

	switch (bpp) {
	case 8:
		return vk::Format::eR8Unorm;
	case 16:
		// OpenGL uses GL_UNSIGNED_SHORT_1_5_5_5_REV with GL_BGRA (A1R5G5B5)
		return vk::Format::eA1R5G5B5UnormPack16;
	case 24:
		// 24bpp (BGR) is almost never supported for optimal tiling in Vulkan.
		// We convert to 32bpp BGRA at upload time, so return the 32bpp format.
		return vk::Format::eB8G8R8A8Unorm;
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

	// Check format supports linear blit filter
	vk::FormatProperties formatProperties = m_physicalDevice.getFormatProperties(ts->format);
	if (!(formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear)) {
		mprintf(("VulkanTextureManager::generateMipmaps: Format %d does not support linear blitting\n",
		         static_cast<int>(ts->format)));
		return;
	}

	vk::CommandBuffer cmd = beginSingleTimeCommands();

	// Transition mip 0 from current layout to eTransferSrcOptimal
	{
		vk::ImageMemoryBarrier barrier;
		barrier.oldLayout = ts->currentLayout;
		barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = ts->image;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		vk::PipelineStageFlags srcStage;
		if (ts->currentLayout == vk::ImageLayout::eColorAttachmentOptimal) {
			barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
			srcStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		} else if (ts->currentLayout == vk::ImageLayout::eShaderReadOnlyOptimal) {
			barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
			srcStage = vk::PipelineStageFlagBits::eFragmentShader;
		} else {
			barrier.srcAccessMask = vk::AccessFlagBits::eMemoryWrite;
			srcStage = vk::PipelineStageFlagBits::eAllCommands;
		}
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

		cmd.pipelineBarrier(srcStage, vk::PipelineStageFlagBits::eTransfer,
		                    {}, {}, {}, barrier);
	}

	// Generate each mip level via blit from the previous level
	for (uint32_t i = 1; i < ts->mipLevels; i++) {
		uint32_t srcW = std::max(1u, ts->width >> (i - 1));
		uint32_t srcH = std::max(1u, ts->height >> (i - 1));
		uint32_t dstW = std::max(1u, ts->width >> i);
		uint32_t dstH = std::max(1u, ts->height >> i);

		// Transition mip i from eUndefined to eTransferDstOptimal
		{
			vk::ImageMemoryBarrier barrier;
			barrier.srcAccessMask = {};
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.oldLayout = vk::ImageLayout::eUndefined;
			barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = ts->image;
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = i;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;

			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
			                    vk::PipelineStageFlagBits::eTransfer,
			                    {}, {}, {}, barrier);
		}

		// Blit from mip i-1 to mip i
		vk::ImageBlit blit;
		blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.srcOffsets[0] = vk::Offset3D(0, 0, 0);
		blit.srcOffsets[1] = vk::Offset3D(static_cast<int32_t>(srcW), static_cast<int32_t>(srcH), 1);

		blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;
		blit.dstOffsets[0] = vk::Offset3D(0, 0, 0);
		blit.dstOffsets[1] = vk::Offset3D(static_cast<int32_t>(dstW), static_cast<int32_t>(dstH), 1);

		cmd.blitImage(ts->image, vk::ImageLayout::eTransferSrcOptimal,
		              ts->image, vk::ImageLayout::eTransferDstOptimal,
		              blit, vk::Filter::eLinear);

		// Transition mip i to eTransferSrcOptimal (source for next blit)
		{
			vk::ImageMemoryBarrier barrier;
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
			barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
			barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = ts->image;
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = i;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;

			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
			                    vk::PipelineStageFlagBits::eTransfer,
			                    {}, {}, {}, barrier);
		}
	}

	// Final transition: all mips to eShaderReadOnlyOptimal
	{
		vk::ImageMemoryBarrier barrier;
		barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = ts->image;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = ts->mipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
		                    vk::PipelineStageFlagBits::eFragmentShader,
		                    {}, {}, {}, barrier);
	}

	endSingleTimeCommands(cmd);
	ts->currentLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
}

void VulkanTextureManager::flushCache()
{
	// TODO: Implement cache flushing
}

void VulkanTextureManager::frameStart()
{
	processPendingCommandBuffers();
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

void VulkanTextureManager::recordUploadCommands(vk::CommandBuffer cmd, vk::Image image,
                                                 vk::Buffer stagingBuffer, vk::Format format,
                                                 uint32_t width, uint32_t height,
                                                 uint32_t mipLevels, vk::ImageLayout oldLayout,
                                                 bool generateMips,
                                                 const SCP_vector<vk::BufferImageCopy>& regions)
{
	(void)format;  // May be needed for depth/stencil transitions in the future

	// Barrier 1: oldLayout -> eTransferDstOptimal (all mip levels)
	{
		vk::ImageMemoryBarrier barrier;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		if (oldLayout == vk::ImageLayout::eUndefined) {
			barrier.srcAccessMask = {};
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
			                    vk::PipelineStageFlagBits::eTransfer,
			                    {}, nullptr, nullptr, barrier);
		} else {
			barrier.srcAccessMask = vk::AccessFlagBits::eMemoryWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
			                    vk::PipelineStageFlagBits::eTransfer,
			                    {}, nullptr, nullptr, barrier);
		}
	}

	if (!regions.empty()) {
		// Pre-baked mip levels: copy all regions (one per mip level) from the staging buffer
		cmd.copyBufferToImage(stagingBuffer, image, vk::ImageLayout::eTransferDstOptimal,
		                      static_cast<uint32_t>(regions.size()), regions.data());
	} else {
		// Single mip-0 copy
		vk::BufferImageCopy region;
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = vk::Offset3D(0, 0, 0);
		region.imageExtent = vk::Extent3D(width, height, 1);

		cmd.copyBufferToImage(stagingBuffer, image, vk::ImageLayout::eTransferDstOptimal, region);
	}

	if (generateMips && mipLevels > 1 && regions.empty()) {
		// Generate mipmaps via blit chain: upload mip 0, then downsample each level

		// Transition mip 0 from eTransferDstOptimal to eTransferSrcOptimal
		{
			vk::ImageMemoryBarrier barrier;
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
			barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
			barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;

			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
			                    vk::PipelineStageFlagBits::eTransfer,
			                    {}, {}, {}, barrier);
		}

		// Generate each mip level via blit from the previous level
		for (uint32_t i = 1; i < mipLevels; i++) {
			uint32_t srcW = std::max(1u, width >> (i - 1));
			uint32_t srcH = std::max(1u, height >> (i - 1));
			uint32_t dstW = std::max(1u, width >> i);
			uint32_t dstH = std::max(1u, height >> i);

			// Mip i is already in eTransferDstOptimal from barrier 1

			// Blit from mip i-1 to mip i
			vk::ImageBlit blit;
			blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;
			blit.srcOffsets[0] = vk::Offset3D(0, 0, 0);
			blit.srcOffsets[1] = vk::Offset3D(static_cast<int32_t>(srcW), static_cast<int32_t>(srcH), 1);

			blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;
			blit.dstOffsets[0] = vk::Offset3D(0, 0, 0);
			blit.dstOffsets[1] = vk::Offset3D(static_cast<int32_t>(dstW), static_cast<int32_t>(dstH), 1);

			cmd.blitImage(image, vk::ImageLayout::eTransferSrcOptimal,
			              image, vk::ImageLayout::eTransferDstOptimal,
			              blit, vk::Filter::eLinear);

			// Transition mip i to eTransferSrcOptimal (source for next blit)
			{
				vk::ImageMemoryBarrier barrier;
				barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
				barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
				barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
				barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
				barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				barrier.image = image;
				barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
				barrier.subresourceRange.baseMipLevel = i;
				barrier.subresourceRange.levelCount = 1;
				barrier.subresourceRange.baseArrayLayer = 0;
				barrier.subresourceRange.layerCount = 1;

				cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
				                    vk::PipelineStageFlagBits::eTransfer,
				                    {}, {}, {}, barrier);
			}
		}

		// Final transition: all mips from eTransferSrcOptimal to eShaderReadOnlyOptimal
		{
			vk::ImageMemoryBarrier barrier;
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
			barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
			barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
			barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = mipLevels;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;

			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
			                    vk::PipelineStageFlagBits::eFragmentShader,
			                    {}, {}, {}, barrier);
		}
	} else {
		// Simple transition: all mips from eTransferDstOptimal to eShaderReadOnlyOptimal
		{
			vk::ImageMemoryBarrier barrier;
			barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
			barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = mipLevels;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

			cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
			                    vk::PipelineStageFlagBits::eFragmentShader,
			                    {}, nullptr, nullptr, barrier);
		}
	}
}

void VulkanTextureManager::submitUploadAsync(vk::CommandBuffer cmd, vk::Buffer stagingBuffer,
                                             VulkanAllocation stagingAllocation)
{
	cmd.end();

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;

	m_graphicsQueue.submit(submitInfo, nullptr);

	// Defer staging buffer destruction (2 frames matches MAX_FRAMES_IN_FLIGHT)
	auto* deletionQueue = getDeletionQueue();
	deletionQueue->queueBuffer(stagingBuffer, stagingAllocation);

	// Defer command buffer free
	m_pendingCommandBuffers.push_back({cmd, VulkanDeletionQueue::FRAMES_TO_WAIT});
}

void VulkanTextureManager::processPendingCommandBuffers()
{
	auto it = m_pendingCommandBuffers.begin();
	while (it != m_pendingCommandBuffers.end()) {
		if (it->framesRemaining == 0) {
			m_device.freeCommandBuffers(m_commandPool, it->cb);
			it = m_pendingCommandBuffers.erase(it);
		} else {
			it->framesRemaining--;
			++it;
		}
	}
}

uint32_t VulkanTextureManager::calculateMipLevels(uint32_t width, uint32_t height)
{
	return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
}

// ========== gr_screen function pointer implementations ==========

int vulkan_preload(int bitmap_num, int /*is_aabitmap*/)
{
	auto* texManager = getTextureManager();

	// Check if texture is already loaded
	auto* slot = texManager->getTextureSlot(bitmap_num);
	if (slot && slot->imageView) {
		return 1;  // Already loaded
	}

	// Determine lock parameters based on compression type.
	// For compressed DDS textures, lock with the matching DXT/BC7 flags to get
	// raw compressed data with all pre-baked mipmap levels.
	int compType = bm_is_compressed(bitmap_num);
	int lockBpp = 32;
	ubyte lockFlags = BMP_TEX_XPARENT;

	switch (compType) {
	case DDS_DXT1:
		lockBpp = 24;
		lockFlags = BMP_TEX_DXT1;
		break;
	case DDS_DXT3:
		lockBpp = 32;
		lockFlags = BMP_TEX_DXT3;
		break;
	case DDS_DXT5:
		lockBpp = 32;
		lockFlags = BMP_TEX_DXT5;
		break;
	case DDS_BC7:
		lockBpp = 32;
		lockFlags = BMP_TEX_BC7;
		break;
	default:
		// Uncompressed or cubemap — use 32bpp decompressed
		compType = 0;
		break;
	}

	bitmap* bmp = bm_lock(bitmap_num, static_cast<ubyte>(lockBpp), lockFlags);
	if (!bmp) {
		static int warnCount = 0;
		if (warnCount < 10) {
			mprintf(("vulkan_preload: Failed to lock bitmap %d (compType=%d)\n", bitmap_num, compType));
			warnCount++;
		}
		return 0;
	}

	// Upload the texture
	bool success = texManager->bm_data(bitmap_num, bmp, compType);

	// Unlock bitmap
	bm_unlock(bitmap_num);

	if (success) {
		static int successCount = 0;
		if (successCount < 10) {
			mprintf(("vulkan_preload: Successfully uploaded texture %d (compressed=%d)\n",
				bitmap_num, compType));
			successCount++;
		}
	}

	return success ? 1 : 0;
}

void vulkan_bm_create(bitmap_slot* slot)
{
	auto* texManager = getTextureManager();
	texManager->bm_create(slot);
}

void vulkan_bm_free_data(bitmap_slot* slot, bool release)
{
	auto* texManager = getTextureManager();
	texManager->bm_free_data(slot, release);
}

void vulkan_bm_init(bitmap_slot* slot)
{
	auto* texManager = getTextureManager();
	texManager->bm_init(slot);
}

bool vulkan_bm_data(int handle, bitmap* bm)
{
	auto* texManager = getTextureManager();
	return texManager->bm_data(handle, bm);
}

int vulkan_bm_make_render_target(int handle, int* width, int* height, int* bpp, int* mm_lvl, int flags)
{
	auto* texManager = getTextureManager();
	return texManager->bm_make_render_target(handle, width, height, bpp, mm_lvl, flags);
}

int vulkan_bm_set_render_target(int handle, int face)
{
	auto* texManager = getTextureManager();
	return texManager->bm_set_render_target(handle, face);
}

void vulkan_update_texture(int bitmap_handle, int bpp, const ubyte* data, int width, int height)
{
	auto* texManager = getTextureManager();
	texManager->update_texture(bitmap_handle, bpp, data, width, height);
}

void vulkan_get_bitmap_from_texture(void* data_out, int bitmap_num)
{
	auto* texManager = getTextureManager();
	texManager->get_bitmap_from_texture(data_out, bitmap_num);
}

} // namespace vulkan
} // namespace graphics

#include "VulkanPostProcessing.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanPipeline.h"
#include "VulkanState.h"
#include "VulkanDraw.h"
#include "VulkanDescriptorManager.h"
#include "graphics/util/uniform_structs.h"
#include "graphics/post_processing.h"
#include "graphics/grinternal.h"
#include "graphics/2d.h"
#include "io/timer.h"
#include "lighting/lighting_profiles.h"
#include "lighting/lighting.h"
#include "math/floating.h"
#include "math/vecmat.h"
#include "render/3d.h"

extern float Sun_spot;
extern int Game_subspace_effect;

namespace graphics {
namespace vulkan {

// Global post-processor pointer
static VulkanPostProcessor* g_postProcessor = nullptr;

VulkanPostProcessor* getPostProcessor()
{
	return g_postProcessor;
}

void setPostProcessor(VulkanPostProcessor* pp)
{
	g_postProcessor = pp;
}

bool VulkanPostProcessor::init(vk::Device device, vk::PhysicalDevice physDevice,
                               VulkanMemoryManager* memMgr, vk::Extent2D extent,
                               vk::Format depthFormat)
{
	if (m_initialized) {
		return true;
	}

	m_device = device;
	m_memoryManager = memMgr;
	m_extent = extent;
	m_depthFormat = depthFormat;

	// Verify RGBA16F support for color attachment + sampling
	{
		vk::FormatProperties props = physDevice.getFormatProperties(vk::Format::eR16G16B16A16Sfloat);
		if (!(props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eColorAttachment) ||
		    !(props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage)) {
			mprintf(("VulkanPostProcessor: RGBA16F not supported for color attachment + sampling!\n"));
			return false;
		}
	}

	// Create HDR scene color target (RGBA16F)
	if (!createImage(extent.width, extent.height, vk::Format::eR16G16B16A16Sfloat,
	                 vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
	                 vk::ImageAspectFlagBits::eColor,
	                 m_sceneColor.image, m_sceneColor.view, m_sceneColor.allocation)) {
		mprintf(("VulkanPostProcessor: Failed to create scene color image!\n"));
		return false;
	}
	m_sceneColor.format = vk::Format::eR16G16B16A16Sfloat;
	m_sceneColor.width = extent.width;
	m_sceneColor.height = extent.height;

	// Create scene depth target
	vk::ImageAspectFlags depthAspect = vk::ImageAspectFlagBits::eDepth;
	if (depthFormat == vk::Format::eD24UnormS8Uint || depthFormat == vk::Format::eD32SfloatS8Uint) {
		depthAspect |= vk::ImageAspectFlagBits::eStencil;
	}

	if (!createImage(extent.width, extent.height, depthFormat,
	                 vk::ImageUsageFlagBits::eDepthStencilAttachment
	                 | vk::ImageUsageFlagBits::eSampled,
	                 vk::ImageAspectFlagBits::eDepth,  // View uses depth-only aspect
	                 m_sceneDepth.image, m_sceneDepth.view, m_sceneDepth.allocation)) {
		mprintf(("VulkanPostProcessor: Failed to create scene depth image!\n"));
		shutdown();
		return false;
	}
	m_sceneDepth.format = depthFormat;
	m_sceneDepth.width = extent.width;
	m_sceneDepth.height = extent.height;

	// Create HDR scene render pass
	// Attachment 0: Color (RGBA16F)
	//   loadOp=eClear: clear to black each frame
	//   finalLayout=eShaderReadOnlyOptimal: ready for post-processing sampling
	// Attachment 1: Depth
	//   loadOp=eClear: clear to far plane
	//   finalLayout=eDepthStencilAttachmentOptimal
	{
		std::array<vk::AttachmentDescription, 2> attachments;

		// Color
		attachments[0].format = vk::Format::eR16G16B16A16Sfloat;
		attachments[0].samples = vk::SampleCountFlagBits::e1;
		attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
		attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
		attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		attachments[0].initialLayout = vk::ImageLayout::eUndefined;
		attachments[0].finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		// Depth
		attachments[1].format = depthFormat;
		attachments[1].samples = vk::SampleCountFlagBits::e1;
		attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
		attachments[1].storeOp = vk::AttachmentStoreOp::eDontCare;
		attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eClear;
		attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		attachments[1].initialLayout = vk::ImageLayout::eUndefined;
		attachments[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::AttachmentReference colorRef;
		colorRef.attachment = 0;
		colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::AttachmentReference depthRef;
		depthRef.attachment = 1;
		depthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

		vk::SubpassDescription subpass;
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;

		// Dependency: external → subpass 0 (ensure previous frame's reads are done)
		vk::SubpassDependency dependency;
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput
		                        | vk::PipelineStageFlagBits::eEarlyFragmentTests;
		dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput
		                        | vk::PipelineStageFlagBits::eEarlyFragmentTests;
		dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite
		                         | vk::AccessFlagBits::eDepthStencilAttachmentWrite;

		vk::RenderPassCreateInfo rpInfo;
		rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		rpInfo.pAttachments = attachments.data();
		rpInfo.subpassCount = 1;
		rpInfo.pSubpasses = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies = &dependency;

		try {
			m_sceneRenderPass = m_device.createRenderPass(rpInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create scene render pass: %s\n", e.what()));
			shutdown();
			return false;
		}
	}

	// Create scene framebuffer
	{
		std::array<vk::ImageView, 2> fbAttachments = {m_sceneColor.view, m_sceneDepth.view};

		vk::FramebufferCreateInfo fbInfo;
		fbInfo.renderPass = m_sceneRenderPass;
		fbInfo.attachmentCount = static_cast<uint32_t>(fbAttachments.size());
		fbInfo.pAttachments = fbAttachments.data();
		fbInfo.width = extent.width;
		fbInfo.height = extent.height;
		fbInfo.layers = 1;

		try {
			m_sceneFramebuffer = m_device.createFramebuffer(fbInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create scene framebuffer: %s\n", e.what()));
			shutdown();
			return false;
		}
	}

	// Create linear sampler for post-processing texture reads
	{
		vk::SamplerCreateInfo samplerInfo;
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;
		samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueBlack;

		try {
			m_linearSampler = m_device.createSampler(samplerInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create sampler: %s\n", e.what()));
			shutdown();
			return false;
		}
	}

	// Create mipmap sampler for bloom textures (supports textureLod)
	{
		vk::SamplerCreateInfo samplerInfo;
		samplerInfo.magFilter = vk::Filter::eLinear;
		samplerInfo.minFilter = vk::Filter::eLinear;
		samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
		samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = static_cast<float>(MAX_MIP_BLUR_LEVELS);
		samplerInfo.borderColor = vk::BorderColor::eFloatOpaqueBlack;

		try {
			m_mipmapSampler = m_device.createSampler(samplerInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create mipmap sampler: %s\n", e.what()));
			shutdown();
			return false;
		}
	}

	// Create persistent UBO for tonemapping parameters
	{
		vk::BufferCreateInfo bufInfo;
		bufInfo.size = sizeof(graphics::generic_data::tonemapping_data);
		bufInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
		bufInfo.sharingMode = vk::SharingMode::eExclusive;

		try {
			m_tonemapUBO = m_device.createBuffer(bufInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create tonemap UBO: %s\n", e.what()));
			shutdown();
			return false;
		}

		if (!m_memoryManager->allocateBufferMemory(m_tonemapUBO, MemoryUsage::CpuToGpu, m_tonemapUBOAlloc)) {
			mprintf(("VulkanPostProcessor: Failed to allocate tonemap UBO memory!\n"));
			m_device.destroyBuffer(m_tonemapUBO);
			m_tonemapUBO = nullptr;
			shutdown();
			return false;
		}

		m_device.bindBufferMemory(m_tonemapUBO, m_tonemapUBOAlloc.memory, m_tonemapUBOAlloc.offset);

		// Write default passthrough tonemapping data (linear, exposure=1.0)
		auto* mapped = static_cast<graphics::generic_data::tonemapping_data*>(m_memoryManager->mapMemory(m_tonemapUBOAlloc));
		if (mapped) {
			memset(mapped, 0, sizeof(graphics::generic_data::tonemapping_data));
			mapped->exposure = 1.0f;
			mapped->tonemapper = 0;  // Linear
			m_memoryManager->unmapMemory(m_tonemapUBOAlloc);
		}
	}

	// Initialize bloom resources (non-fatal if it fails)
	if (!initBloom()) {
		mprintf(("VulkanPostProcessor: Bloom initialization failed (non-fatal)\n"));
	}

	// Initialize LDR targets for tonemapping + FXAA (non-fatal if it fails)
	if (!initLDRTargets()) {
		mprintf(("VulkanPostProcessor: LDR target initialization failed (non-fatal)\n"));
	}

	m_initialized = true;
	mprintf(("VulkanPostProcessor: Initialized (%ux%u, RGBA16F scene color)\n",
		extent.width, extent.height));
	return true;
}

void VulkanPostProcessor::shutdown()
{
	if (m_device) {
		m_device.waitIdle();

		shutdownLDRTargets();
		shutdownBloom();

		if (m_mipmapSampler) {
			m_device.destroySampler(m_mipmapSampler);
			m_mipmapSampler = nullptr;
		}

		if (m_tonemapUBO) {
			m_device.destroyBuffer(m_tonemapUBO);
			m_tonemapUBO = nullptr;
		}
		if (m_tonemapUBOAlloc.memory != VK_NULL_HANDLE) {
			m_memoryManager->freeAllocation(m_tonemapUBOAlloc);
		}

		if (m_linearSampler) {
			m_device.destroySampler(m_linearSampler);
			m_linearSampler = nullptr;
		}
		if (m_sceneFramebuffer) {
			m_device.destroyFramebuffer(m_sceneFramebuffer);
			m_sceneFramebuffer = nullptr;
		}
		if (m_sceneRenderPass) {
			m_device.destroyRenderPass(m_sceneRenderPass);
			m_sceneRenderPass = nullptr;
		}

		// Destroy scene color target
		if (m_sceneColor.view) {
			m_device.destroyImageView(m_sceneColor.view);
			m_sceneColor.view = nullptr;
		}
		if (m_sceneColor.image) {
			m_device.destroyImage(m_sceneColor.image);
			m_sceneColor.image = nullptr;
		}
		if (m_sceneColor.allocation.memory != VK_NULL_HANDLE) {
			m_memoryManager->freeAllocation(m_sceneColor.allocation);
		}

		// Destroy scene depth target
		if (m_sceneDepth.view) {
			m_device.destroyImageView(m_sceneDepth.view);
			m_sceneDepth.view = nullptr;
		}
		if (m_sceneDepth.image) {
			m_device.destroyImage(m_sceneDepth.image);
			m_sceneDepth.image = nullptr;
		}
		if (m_sceneDepth.allocation.memory != VK_NULL_HANDLE) {
			m_memoryManager->freeAllocation(m_sceneDepth.allocation);
		}
	}

	m_initialized = false;
}

void VulkanPostProcessor::updateTonemappingUBO()
{
	if (!m_tonemapUBO || !m_memoryManager) {
		return;
	}

	namespace ltp = lighting_profiles;

	auto* mapped = static_cast<graphics::generic_data::tonemapping_data*>(
		m_memoryManager->mapMemory(m_tonemapUBOAlloc));
	if (mapped) {
		auto ppc = ltp::current_piecewise_intermediates();
		mapped->exposure = ltp::current_exposure();
		mapped->tonemapper = static_cast<int>(ltp::current_tonemapper());
		mapped->x0 = ppc.x0;
		mapped->y0 = ppc.y0;
		mapped->x1 = ppc.x1;
		mapped->toe_B = ppc.toe_B;
		mapped->toe_lnA = ppc.toe_lnA;
		mapped->sh_B = ppc.sh_B;
		mapped->sh_lnA = ppc.sh_lnA;
		mapped->sh_offsetX = ppc.sh_offsetX;
		mapped->sh_offsetY = ppc.sh_offsetY;
		mapped->linearOut = 0;  // Apply sRGB conversion (HDR → swap chain)
		m_memoryManager->unmapMemory(m_tonemapUBOAlloc);
	}
}

// ===== Bloom Pipeline Implementation =====

// Local UBO struct for blur shader (extends blur_data with runtime direction parameter)
struct BlurUBOData {
	float texSize;
	int level;
	int direction; // 0 = horizontal, 1 = vertical
	int pad;
};

bool VulkanPostProcessor::initBloom()
{
	m_bloomWidth = m_extent.width / 2;
	m_bloomHeight = m_extent.height / 2;

	const uint32_t mipLevels = MAX_MIP_BLUR_LEVELS;

	// Create 2 bloom textures (RGBA16F, half-res, 4 mip levels each)
	for (int i = 0; i < 2; i++) {
		vk::ImageCreateInfo imageInfo;
		imageInfo.imageType = vk::ImageType::e2D;
		imageInfo.format = vk::Format::eR16G16B16A16Sfloat;
		imageInfo.extent.width = m_bloomWidth;
		imageInfo.extent.height = m_bloomHeight;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = mipLevels;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = vk::SampleCountFlagBits::e1;
		imageInfo.tiling = vk::ImageTiling::eOptimal;
		imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment
		                | vk::ImageUsageFlagBits::eSampled
		                | vk::ImageUsageFlagBits::eTransferSrc
		                | vk::ImageUsageFlagBits::eTransferDst;
		imageInfo.sharingMode = vk::SharingMode::eExclusive;
		imageInfo.initialLayout = vk::ImageLayout::eUndefined;

		try {
			m_bloomTex[i].image = m_device.createImage(imageInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create bloom image %d: %s\n", i, e.what()));
			return false;
		}

		if (!m_memoryManager->allocateImageMemory(m_bloomTex[i].image, MemoryUsage::GpuOnly, m_bloomTex[i].allocation)) {
			mprintf(("VulkanPostProcessor: Failed to allocate bloom image %d memory!\n", i));
			return false;
		}

		// Full image view (all mip levels, for textureLod sampling)
		vk::ImageViewCreateInfo fullViewInfo;
		fullViewInfo.image = m_bloomTex[i].image;
		fullViewInfo.viewType = vk::ImageViewType::e2D;
		fullViewInfo.format = vk::Format::eR16G16B16A16Sfloat;
		fullViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		fullViewInfo.subresourceRange.baseMipLevel = 0;
		fullViewInfo.subresourceRange.levelCount = mipLevels;
		fullViewInfo.subresourceRange.baseArrayLayer = 0;
		fullViewInfo.subresourceRange.layerCount = 1;

		try {
			m_bloomTex[i].fullView = m_device.createImageView(fullViewInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create bloom %d full view: %s\n", i, e.what()));
			return false;
		}

		// Per-mip image views (for framebuffer attachment)
		for (uint32_t mip = 0; mip < mipLevels; mip++) {
			vk::ImageViewCreateInfo mipViewInfo = fullViewInfo;
			mipViewInfo.subresourceRange.baseMipLevel = mip;
			mipViewInfo.subresourceRange.levelCount = 1;

			try {
				m_bloomTex[i].mipViews[mip] = m_device.createImageView(mipViewInfo);
			} catch (const vk::SystemError& e) {
				mprintf(("VulkanPostProcessor: Failed to create bloom %d mip %u view: %s\n", i, mip, e.what()));
				return false;
			}
		}
	}

	// Create bloom render pass (color-only RGBA16F, loadOp=eDontCare for overwriting)
	{
		vk::AttachmentDescription att;
		att.format = vk::Format::eR16G16B16A16Sfloat;
		att.samples = vk::SampleCountFlagBits::e1;
		att.loadOp = vk::AttachmentLoadOp::eDontCare;
		att.storeOp = vk::AttachmentStoreOp::eStore;
		att.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		att.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		att.initialLayout = vk::ImageLayout::eUndefined;
		att.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::AttachmentReference colorRef;
		colorRef.attachment = 0;
		colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::SubpassDescription subpass;
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;

		vk::SubpassDependency dep;
		dep.srcSubpass = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass = 0;
		dep.srcStageMask = vk::PipelineStageFlagBits::eFragmentShader
		                  | vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dep.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader
		                  | vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dep.srcAccessMask = vk::AccessFlagBits::eShaderRead
		                  | vk::AccessFlagBits::eColorAttachmentWrite;
		dep.dstAccessMask = vk::AccessFlagBits::eShaderRead
		                  | vk::AccessFlagBits::eColorAttachmentWrite;

		vk::RenderPassCreateInfo rpInfo;
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments = &att;
		rpInfo.subpassCount = 1;
		rpInfo.pSubpasses = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies = &dep;

		try {
			m_bloomRenderPass = m_device.createRenderPass(rpInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create bloom render pass: %s\n", e.what()));
			return false;
		}
	}

	// Create bloom composite render pass (loadOp=eLoad for additive compositing onto scene color)
	{
		vk::AttachmentDescription att;
		att.format = vk::Format::eR16G16B16A16Sfloat;
		att.samples = vk::SampleCountFlagBits::e1;
		att.loadOp = vk::AttachmentLoadOp::eLoad;
		att.storeOp = vk::AttachmentStoreOp::eStore;
		att.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		att.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		att.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
		att.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::AttachmentReference colorRef;
		colorRef.attachment = 0;
		colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::SubpassDescription subpass;
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;

		vk::SubpassDependency dep;
		dep.srcSubpass = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass = 0;
		dep.srcStageMask = vk::PipelineStageFlagBits::eFragmentShader
		                  | vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dep.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader
		                  | vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dep.srcAccessMask = vk::AccessFlagBits::eShaderRead
		                  | vk::AccessFlagBits::eColorAttachmentWrite;
		dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead
		                  | vk::AccessFlagBits::eColorAttachmentWrite;

		vk::RenderPassCreateInfo rpInfo;
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments = &att;
		rpInfo.subpassCount = 1;
		rpInfo.pSubpasses = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies = &dep;

		try {
			m_bloomCompositeRenderPass = m_device.createRenderPass(rpInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create bloom composite render pass: %s\n", e.what()));
			return false;
		}
	}

	// Create per-mip framebuffers for bloom textures
	for (int i = 0; i < 2; i++) {
		for (uint32_t mip = 0; mip < mipLevels; mip++) {
			uint32_t mipW = std::max(1u, m_bloomWidth >> mip);
			uint32_t mipH = std::max(1u, m_bloomHeight >> mip);

			vk::FramebufferCreateInfo fbInfo;
			fbInfo.renderPass = m_bloomRenderPass;
			fbInfo.attachmentCount = 1;
			fbInfo.pAttachments = &m_bloomTex[i].mipViews[mip];
			fbInfo.width = mipW;
			fbInfo.height = mipH;
			fbInfo.layers = 1;

			try {
				m_bloomTex[i].mipFramebuffers[mip] = m_device.createFramebuffer(fbInfo);
			} catch (const vk::SystemError& e) {
				mprintf(("VulkanPostProcessor: Failed to create bloom %d mip %u framebuffer: %s\n", i, mip, e.what()));
				return false;
			}
		}
	}

	// Create scene color framebuffer for bloom composite (wraps m_sceneColor as attachment)
	{
		vk::FramebufferCreateInfo fbInfo;
		fbInfo.renderPass = m_bloomCompositeRenderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = &m_sceneColor.view;
		fbInfo.width = m_extent.width;
		fbInfo.height = m_extent.height;
		fbInfo.layers = 1;

		try {
			m_sceneColorBloomFB = m_device.createFramebuffer(fbInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create scene color bloom framebuffer: %s\n", e.what()));
			return false;
		}
	}

	// Create bloom UBO buffer (slot-based allocation for per-draw data)
	{
		vk::BufferCreateInfo bufInfo;
		bufInfo.size = BLOOM_UBO_MAX_SLOTS * BLOOM_UBO_SLOT_SIZE;
		bufInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
		bufInfo.sharingMode = vk::SharingMode::eExclusive;

		try {
			m_bloomUBO = m_device.createBuffer(bufInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create bloom UBO: %s\n", e.what()));
			return false;
		}

		if (!m_memoryManager->allocateBufferMemory(m_bloomUBO, MemoryUsage::CpuToGpu, m_bloomUBOAlloc)) {
			mprintf(("VulkanPostProcessor: Failed to allocate bloom UBO memory!\n"));
			m_device.destroyBuffer(m_bloomUBO);
			m_bloomUBO = nullptr;
			return false;
		}

		m_device.bindBufferMemory(m_bloomUBO, m_bloomUBOAlloc.memory, m_bloomUBOAlloc.offset);
	}

	m_bloomInitialized = true;
	mprintf(("VulkanPostProcessor: Bloom initialized (%ux%u, %d mip levels)\n",
		m_bloomWidth, m_bloomHeight, MAX_MIP_BLUR_LEVELS));
	return true;
}

void VulkanPostProcessor::shutdownBloom()
{
	if (!m_bloomInitialized) {
		return;
	}

	if (m_bloomUBO) {
		m_device.destroyBuffer(m_bloomUBO);
		m_bloomUBO = nullptr;
	}
	if (m_bloomUBOAlloc.memory != VK_NULL_HANDLE) {
		m_memoryManager->freeAllocation(m_bloomUBOAlloc);
	}

	if (m_sceneColorBloomFB) {
		m_device.destroyFramebuffer(m_sceneColorBloomFB);
		m_sceneColorBloomFB = nullptr;
	}

	for (int i = 0; i < 2; i++) {
		for (uint32_t mip = 0; mip < MAX_MIP_BLUR_LEVELS; mip++) {
			if (m_bloomTex[i].mipFramebuffers[mip]) {
				m_device.destroyFramebuffer(m_bloomTex[i].mipFramebuffers[mip]);
				m_bloomTex[i].mipFramebuffers[mip] = nullptr;
			}
			if (m_bloomTex[i].mipViews[mip]) {
				m_device.destroyImageView(m_bloomTex[i].mipViews[mip]);
				m_bloomTex[i].mipViews[mip] = nullptr;
			}
		}
		if (m_bloomTex[i].fullView) {
			m_device.destroyImageView(m_bloomTex[i].fullView);
			m_bloomTex[i].fullView = nullptr;
		}
		if (m_bloomTex[i].image) {
			m_device.destroyImage(m_bloomTex[i].image);
			m_bloomTex[i].image = nullptr;
		}
		if (m_bloomTex[i].allocation.memory != VK_NULL_HANDLE) {
			m_memoryManager->freeAllocation(m_bloomTex[i].allocation);
		}
	}

	if (m_bloomCompositeRenderPass) {
		m_device.destroyRenderPass(m_bloomCompositeRenderPass);
		m_bloomCompositeRenderPass = nullptr;
	}
	if (m_bloomRenderPass) {
		m_device.destroyRenderPass(m_bloomRenderPass);
		m_bloomRenderPass = nullptr;
	}

	m_bloomInitialized = false;
}

void VulkanPostProcessor::generateMipmaps(vk::CommandBuffer cmd, vk::Image image,
                                           uint32_t width, uint32_t height, uint32_t mipLevels)
{
	// Transition mip 0 from eShaderReadOnlyOptimal (after brightpass) to eTransferSrcOptimal
	{
		vk::ImageMemoryBarrier barrier;
		barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
		barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::PipelineStageFlagBits::eTransfer,
			{}, {}, {}, barrier);
	}

	// Generate each mip level via blit from the previous level
	for (uint32_t i = 1; i < mipLevels; i++) {
		uint32_t srcW = std::max(1u, width >> (i - 1));
		uint32_t srcH = std::max(1u, height >> (i - 1));
		uint32_t dstW = std::max(1u, width >> i);
		uint32_t dstH = std::max(1u, height >> i);

		// Transition mip i from eUndefined to eTransferDstOptimal
		{
			vk::ImageMemoryBarrier barrier;
			barrier.srcAccessMask = {};
			barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
			barrier.oldLayout = vk::ImageLayout::eUndefined;
			barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
			barrier.subresourceRange.baseMipLevel = i;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;

			cmd.pipelineBarrier(
				vk::PipelineStageFlagBits::eTransfer,
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

		cmd.blitImage(
			image, vk::ImageLayout::eTransferSrcOptimal,
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

			cmd.pipelineBarrier(
				vk::PipelineStageFlagBits::eTransfer,
				vk::PipelineStageFlagBits::eTransfer,
				{}, {}, {}, barrier);
		}
	}

	// Transition ALL mips from eTransferSrcOptimal to eShaderReadOnlyOptimal
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

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eFragmentShader,
			{}, {}, {}, barrier);
	}
}

void VulkanPostProcessor::drawFullscreenTriangle(vk::CommandBuffer cmd, vk::RenderPass renderPass,
                                                  vk::Framebuffer framebuffer, vk::Extent2D extent,
                                                  int shaderType,
                                                  vk::ImageView textureView, vk::Sampler sampler,
                                                  const void* uboData, size_t uboSize,
                                                  int blendMode)
{
	auto* pipelineMgr = getPipelineManager();
	auto* descriptorMgr = getDescriptorManager();
	auto* bufferMgr = getBufferManager();
	auto* texMgr = getTextureManager();

	if (!pipelineMgr || !descriptorMgr || !bufferMgr || !texMgr) {
		return;
	}

	// Get/create pipeline for this shader + render pass combination
	PipelineConfig config;
	config.shaderType = static_cast<shader_type>(shaderType);
	config.vertexLayoutHash = 0;
	config.primitiveType = PRIM_TYPE_TRIS;
	config.depthMode = ZBUFFER_TYPE_NONE;
	config.blendMode = static_cast<gr_alpha_blend>(blendMode);
	config.cullEnabled = false;
	config.depthWriteEnabled = false;
	config.renderPass = renderPass;

	vertex_layout emptyLayout;
	vk::Pipeline pipeline = pipelineMgr->getPipeline(config, emptyLayout);
	if (!pipeline) {
		return;
	}

	vk::PipelineLayout pipelineLayout = pipelineMgr->getPipelineLayout();

	// Begin render pass
	vk::RenderPassBeginInfo rpBegin;
	rpBegin.renderPass = renderPass;
	rpBegin.framebuffer = framebuffer;
	rpBegin.renderArea.offset = vk::Offset2D(0, 0);
	rpBegin.renderArea.extent = extent;

	cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

	// Set viewport and scissor
	vk::Viewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(extent.width);
	viewport.height = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	cmd.setViewport(0, viewport);

	vk::Rect2D scissor;
	scissor.offset = vk::Offset2D(0, 0);
	scissor.extent = extent;
	cmd.setScissor(0, scissor);

	// Allocate Material descriptor set (Set 1)
	vk::DescriptorSet materialSet = descriptorMgr->allocateFrameSet(DescriptorSetIndex::Material);
	if (!materialSet) {
		cmd.endRenderPass();
		return;
	}

	{
		// Source texture at binding 1 element 0
		vk::DescriptorImageInfo imageInfo;
		imageInfo.sampler = sampler;
		imageInfo.imageView = textureView;
		imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::WriteDescriptorSet texWrite;
		texWrite.dstSet = materialSet;
		texWrite.dstBinding = 1;
		texWrite.dstArrayElement = 0;
		texWrite.descriptorCount = 1;
		texWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		texWrite.pImageInfo = &imageInfo;

		// Fallback UBO for binding 0 (ModelData) and binding 2 (DecalGlobals)
		auto fallbackBuf = bufferMgr->getFallbackUniformBuffer();
		vk::DescriptorBufferInfo fallbackBufInfo;
		fallbackBufInfo.buffer = fallbackBuf;
		fallbackBufInfo.offset = 0;
		fallbackBufInfo.range = 4096;

		vk::WriteDescriptorSet modelWrite;
		modelWrite.dstSet = materialSet;
		modelWrite.dstBinding = 0;
		modelWrite.dstArrayElement = 0;
		modelWrite.descriptorCount = 1;
		modelWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
		modelWrite.pBufferInfo = &fallbackBufInfo;

		vk::WriteDescriptorSet decalWrite;
		decalWrite.dstSet = materialSet;
		decalWrite.dstBinding = 2;
		decalWrite.dstArrayElement = 0;
		decalWrite.descriptorCount = 1;
		decalWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
		decalWrite.pBufferInfo = &fallbackBufInfo;

		// Fill remaining texture array elements with fallback
		vk::ImageView fallbackView = texMgr->getFallbackTextureView();
		vk::Sampler defaultSampler = texMgr->getDefaultSampler();

		SCP_vector<vk::DescriptorImageInfo> fallbackImages(VulkanDescriptorManager::MAX_TEXTURE_BINDINGS - 1);
		for (auto& fi : fallbackImages) {
			fi.sampler = defaultSampler;
			fi.imageView = fallbackView;
			fi.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		}

		vk::WriteDescriptorSet fallbackTexWrite;
		fallbackTexWrite.dstSet = materialSet;
		fallbackTexWrite.dstBinding = 1;
		fallbackTexWrite.dstArrayElement = 1;
		fallbackTexWrite.descriptorCount = static_cast<uint32_t>(fallbackImages.size());
		fallbackTexWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		fallbackTexWrite.pImageInfo = fallbackImages.data();

		std::array<vk::WriteDescriptorSet, 4> writes = {texWrite, modelWrite, decalWrite, fallbackTexWrite};
		m_device.updateDescriptorSets(writes, {});
	}

	// Allocate PerDraw descriptor set (Set 2)
	vk::DescriptorSet perDrawSet = descriptorMgr->allocateFrameSet(DescriptorSetIndex::PerDraw);
	if (!perDrawSet) {
		cmd.endRenderPass();
		return;
	}

	{
		vk::DescriptorBufferInfo uboInfo;

		if (uboData && uboSize > 0 && m_bloomUBOMapped) {
			// Write UBO data to current bloom UBO slot
			Assertion(m_bloomUBOCursor < BLOOM_UBO_MAX_SLOTS, "Bloom UBO slot overflow!");
			uint32_t slotOffset = m_bloomUBOCursor * static_cast<uint32_t>(BLOOM_UBO_SLOT_SIZE);
			memcpy(static_cast<uint8_t*>(m_bloomUBOMapped) + slotOffset, uboData, uboSize);
			m_bloomUBOCursor++;

			uboInfo.buffer = m_bloomUBO;
			uboInfo.offset = slotOffset;
			uboInfo.range = BLOOM_UBO_SLOT_SIZE;
		} else {
			// No UBO data — use fallback zero buffer
			uboInfo.buffer = bufferMgr->getFallbackUniformBuffer();
			uboInfo.offset = 0;
			uboInfo.range = 4096;
		}

		vk::WriteDescriptorSet write;
		write.dstSet = perDrawSet;
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eUniformBuffer;
		write.pBufferInfo = &uboInfo;

		// Fallback for remaining per-draw bindings (1-4: Matrices, NanoVGData, DecalInfo, MovieData)
		auto fallbackBuf = bufferMgr->getFallbackUniformBuffer();
		vk::DescriptorBufferInfo fallbackInfo;
		fallbackInfo.buffer = fallbackBuf;
		fallbackInfo.offset = 0;
		fallbackInfo.range = 4096;

		SCP_vector<vk::WriteDescriptorSet> writes;
		writes.push_back(write);
		for (uint32_t b = 1; b <= 4; ++b) {
			vk::WriteDescriptorSet fw;
			fw.dstSet = perDrawSet;
			fw.dstBinding = b;
			fw.dstArrayElement = 0;
			fw.descriptorCount = 1;
			fw.descriptorType = vk::DescriptorType::eUniformBuffer;
			fw.pBufferInfo = &fallbackInfo;
			writes.push_back(fw);
		}

		m_device.updateDescriptorSets(writes, {});
	}

	// Bind descriptor sets (Set 0 already bound from frame setup)
	cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
		static_cast<uint32_t>(DescriptorSetIndex::Material),
		{materialSet, perDrawSet}, {});

	cmd.draw(3, 1, 0, 0);
	cmd.endRenderPass();
}

void VulkanPostProcessor::executeBloom(vk::CommandBuffer cmd)
{
	if (!m_bloomInitialized || gr_bloom_intensity() <= 0) {
		return;
	}

	// Map bloom UBO for writing per-draw data
	m_bloomUBOMapped = m_memoryManager->mapMemory(m_bloomUBOAlloc);
	if (!m_bloomUBOMapped) {
		return;
	}
	m_bloomUBOCursor = 0;

	// 1. Bright pass: extract pixels brighter than 1.0 from scene color → bloom_tex[0] mip 0
	drawFullscreenTriangle(cmd, m_bloomRenderPass,
		m_bloomTex[0].mipFramebuffers[0],
		vk::Extent2D(m_bloomWidth, m_bloomHeight),
		SDR_TYPE_POST_PROCESS_BRIGHTPASS,
		m_sceneColor.view, m_linearSampler,
		nullptr, 0,  // Brightpass has no UBO
		ALPHA_BLEND_NONE);

	// 2. Generate mipmaps for bloom_tex[0] (fill mips 1-3 from mip 0)
	generateMipmaps(cmd, m_bloomTex[0].image, m_bloomWidth, m_bloomHeight, MAX_MIP_BLUR_LEVELS);

	// 3. Blur iterations (2 iterations of vertical + horizontal ping-pong)
	for (int iteration = 0; iteration < 2; iteration++) {
		for (int pass = 0; pass < 2; pass++) {
			// pass 0 = vertical (tex[0] → tex[1]), pass 1 = horizontal (tex[1] → tex[0])
			int srcIdx = pass;
			int dstIdx = 1 - pass;
			int direction = (pass == 0) ? 1 : 0;  // 1=vertical, 0=horizontal

			for (int mip = 0; mip < MAX_MIP_BLUR_LEVELS; mip++) {
				uint32_t mipW = std::max(1u, m_bloomWidth >> mip);
				uint32_t mipH = std::max(1u, m_bloomHeight >> mip);

				BlurUBOData blurData;
				blurData.texSize = (direction == 0) ? 1.0f / static_cast<float>(mipW)
				                                    : 1.0f / static_cast<float>(mipH);
				blurData.level = mip;
				blurData.direction = direction;
				blurData.pad = 0;

				drawFullscreenTriangle(cmd, m_bloomRenderPass,
					m_bloomTex[dstIdx].mipFramebuffers[mip],
					vk::Extent2D(mipW, mipH),
					SDR_TYPE_POST_PROCESS_BLUR,
					m_bloomTex[srcIdx].fullView, m_mipmapSampler,
					&blurData, sizeof(blurData),
					ALPHA_BLEND_NONE);
			}
		}
	}

	// 4. Transition scene color for bloom composite (eShaderReadOnlyOptimal → eColorAttachmentOptimal)
	{
		vk::ImageMemoryBarrier barrier;
		barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
		barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead
		                      | vk::AccessFlagBits::eColorAttachmentWrite;
		barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = m_sceneColor.image;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eFragmentShader,
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			{}, {}, {}, barrier);
	}

	// 5. Bloom composite: additively blend blurred bloom onto scene color
	graphics::generic_data::bloom_composition_data compData;
	compData.bloom_intensity = gr_bloom_intensity() / 100.0f;
	compData.levels = MAX_MIP_BLUR_LEVELS;
	compData.pad[0] = 0.0f;
	compData.pad[1] = 0.0f;

	drawFullscreenTriangle(cmd, m_bloomCompositeRenderPass,
		m_sceneColorBloomFB,
		m_extent,
		SDR_TYPE_POST_PROCESS_BLOOM_COMP,
		m_bloomTex[0].fullView, m_mipmapSampler,
		&compData, sizeof(compData),
		ALPHA_BLEND_ADDITIVE);

	// Scene_color is now in eShaderReadOnlyOptimal (from bloom composite render pass finalLayout)

	// Unmap bloom UBO
	m_memoryManager->unmapMemory(m_bloomUBOAlloc);
	m_bloomUBOMapped = nullptr;
}

// ===== LDR Targets + FXAA Pipeline Implementation =====

bool VulkanPostProcessor::initLDRTargets()
{
	// Create Scene_ldr (RGBA8, full resolution) — tonemapped LDR output
	if (!createImage(m_extent.width, m_extent.height, vk::Format::eR8G8B8A8Unorm,
	                 vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
	                 vk::ImageAspectFlagBits::eColor,
	                 m_sceneLdr.image, m_sceneLdr.view, m_sceneLdr.allocation)) {
		mprintf(("VulkanPostProcessor: Failed to create Scene_ldr image!\n"));
		return false;
	}
	m_sceneLdr.format = vk::Format::eR8G8B8A8Unorm;
	m_sceneLdr.width = m_extent.width;
	m_sceneLdr.height = m_extent.height;

	// Create Scene_luminance (RGBA8, full resolution) — LDR with luma in alpha for FXAA
	if (!createImage(m_extent.width, m_extent.height, vk::Format::eR8G8B8A8Unorm,
	                 vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
	                 vk::ImageAspectFlagBits::eColor,
	                 m_sceneLuminance.image, m_sceneLuminance.view, m_sceneLuminance.allocation)) {
		mprintf(("VulkanPostProcessor: Failed to create Scene_luminance image!\n"));
		return false;
	}
	m_sceneLuminance.format = vk::Format::eR8G8B8A8Unorm;
	m_sceneLuminance.width = m_extent.width;
	m_sceneLuminance.height = m_extent.height;

	// Create LDR render pass (color-only RGBA8, loadOp=eDontCare, finalLayout=eShaderReadOnlyOptimal)
	{
		vk::AttachmentDescription att;
		att.format = vk::Format::eR8G8B8A8Unorm;
		att.samples = vk::SampleCountFlagBits::e1;
		att.loadOp = vk::AttachmentLoadOp::eDontCare;
		att.storeOp = vk::AttachmentStoreOp::eStore;
		att.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		att.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		att.initialLayout = vk::ImageLayout::eUndefined;
		att.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::AttachmentReference colorRef;
		colorRef.attachment = 0;
		colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::SubpassDescription subpass;
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;

		vk::SubpassDependency dep;
		dep.srcSubpass = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass = 0;
		dep.srcStageMask = vk::PipelineStageFlagBits::eFragmentShader
		                  | vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dep.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader
		                  | vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dep.srcAccessMask = vk::AccessFlagBits::eShaderRead
		                  | vk::AccessFlagBits::eColorAttachmentWrite;
		dep.dstAccessMask = vk::AccessFlagBits::eShaderRead
		                  | vk::AccessFlagBits::eColorAttachmentWrite;

		vk::RenderPassCreateInfo rpInfo;
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments = &att;
		rpInfo.subpassCount = 1;
		rpInfo.pSubpasses = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies = &dep;

		try {
			m_ldrRenderPass = m_device.createRenderPass(rpInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create LDR render pass: %s\n", e.what()));
			return false;
		}
	}

	// Create framebuffers
	{
		vk::FramebufferCreateInfo fbInfo;
		fbInfo.renderPass = m_ldrRenderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = &m_sceneLdr.view;
		fbInfo.width = m_extent.width;
		fbInfo.height = m_extent.height;
		fbInfo.layers = 1;

		try {
			m_sceneLdrFB = m_device.createFramebuffer(fbInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create Scene_ldr framebuffer: %s\n", e.what()));
			return false;
		}

		fbInfo.pAttachments = &m_sceneLuminance.view;
		try {
			m_sceneLuminanceFB = m_device.createFramebuffer(fbInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create Scene_luminance framebuffer: %s\n", e.what()));
			return false;
		}
	}

	// Create LDR load render pass (loadOp=eLoad for additive blending onto existing content)
	{
		vk::AttachmentDescription att;
		att.format = vk::Format::eR8G8B8A8Unorm;
		att.samples = vk::SampleCountFlagBits::e1;
		att.loadOp = vk::AttachmentLoadOp::eLoad;
		att.storeOp = vk::AttachmentStoreOp::eStore;
		att.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
		att.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
		att.initialLayout = vk::ImageLayout::eColorAttachmentOptimal;
		att.finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::AttachmentReference colorRef;
		colorRef.attachment = 0;
		colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

		vk::SubpassDescription subpass;
		subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;

		vk::SubpassDependency dep;
		dep.srcSubpass = VK_SUBPASS_EXTERNAL;
		dep.dstSubpass = 0;
		dep.srcStageMask = vk::PipelineStageFlagBits::eFragmentShader
		                  | vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dep.dstStageMask = vk::PipelineStageFlagBits::eFragmentShader
		                  | vk::PipelineStageFlagBits::eColorAttachmentOutput;
		dep.srcAccessMask = vk::AccessFlagBits::eShaderRead
		                  | vk::AccessFlagBits::eColorAttachmentWrite;
		dep.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead
		                  | vk::AccessFlagBits::eColorAttachmentWrite;

		vk::RenderPassCreateInfo rpInfo;
		rpInfo.attachmentCount = 1;
		rpInfo.pAttachments = &att;
		rpInfo.subpassCount = 1;
		rpInfo.pSubpasses = &subpass;
		rpInfo.dependencyCount = 1;
		rpInfo.pDependencies = &dep;

		try {
			m_ldrLoadRenderPass = m_device.createRenderPass(rpInfo);
		} catch (const vk::SystemError& e) {
			mprintf(("VulkanPostProcessor: Failed to create LDR load render pass: %s\n", e.what()));
			return false;
		}
	}

	m_ldrInitialized = true;
	mprintf(("VulkanPostProcessor: LDR targets initialized (%ux%u, RGBA8)\n",
		m_extent.width, m_extent.height));
	return true;
}

void VulkanPostProcessor::shutdownLDRTargets()
{
	if (!m_ldrInitialized) {
		return;
	}

	if (m_sceneLuminanceFB) {
		m_device.destroyFramebuffer(m_sceneLuminanceFB);
		m_sceneLuminanceFB = nullptr;
	}
	if (m_sceneLdrFB) {
		m_device.destroyFramebuffer(m_sceneLdrFB);
		m_sceneLdrFB = nullptr;
	}
	if (m_ldrLoadRenderPass) {
		m_device.destroyRenderPass(m_ldrLoadRenderPass);
		m_ldrLoadRenderPass = nullptr;
	}
	if (m_ldrRenderPass) {
		m_device.destroyRenderPass(m_ldrRenderPass);
		m_ldrRenderPass = nullptr;
	}

	// Scene_luminance
	if (m_sceneLuminance.view) {
		m_device.destroyImageView(m_sceneLuminance.view);
		m_sceneLuminance.view = nullptr;
	}
	if (m_sceneLuminance.image) {
		m_device.destroyImage(m_sceneLuminance.image);
		m_sceneLuminance.image = nullptr;
	}
	if (m_sceneLuminance.allocation.memory != VK_NULL_HANDLE) {
		m_memoryManager->freeAllocation(m_sceneLuminance.allocation);
	}

	// Scene_ldr
	if (m_sceneLdr.view) {
		m_device.destroyImageView(m_sceneLdr.view);
		m_sceneLdr.view = nullptr;
	}
	if (m_sceneLdr.image) {
		m_device.destroyImage(m_sceneLdr.image);
		m_sceneLdr.image = nullptr;
	}
	if (m_sceneLdr.allocation.memory != VK_NULL_HANDLE) {
		m_memoryManager->freeAllocation(m_sceneLdr.allocation);
	}

	m_ldrInitialized = false;
}

void VulkanPostProcessor::executeTonemap(vk::CommandBuffer cmd)
{
	if (!m_ldrInitialized) {
		return;
	}

	namespace ltp = lighting_profiles;

	// Map bloom UBO for the tonemapping draw's UBO slot
	m_bloomUBOMapped = m_memoryManager->mapMemory(m_bloomUBOAlloc);
	if (!m_bloomUBOMapped) {
		return;
	}

	// Reset cursor if bloom didn't run this frame (bloom resets to 0 when it runs)
	if (gr_bloom_intensity() <= 0 || !m_bloomInitialized) {
		m_bloomUBOCursor = 0;
	}

	// Build tonemapping data directly from lighting profiles
	graphics::generic_data::tonemapping_data tmData;
	memset(&tmData, 0, sizeof(tmData));
	auto ppc = ltp::current_piecewise_intermediates();
	tmData.exposure = ltp::current_exposure();
	tmData.tonemapper = static_cast<int>(ltp::current_tonemapper());
	tmData.x0 = ppc.x0;
	tmData.y0 = ppc.y0;
	tmData.x1 = ppc.x1;
	tmData.toe_B = ppc.toe_B;
	tmData.toe_lnA = ppc.toe_lnA;
	tmData.sh_B = ppc.sh_B;
	tmData.sh_lnA = ppc.sh_lnA;
	tmData.sh_offsetX = ppc.sh_offsetX;
	tmData.sh_offsetY = ppc.sh_offsetY;

	// HDR scene → Scene_ldr via tonemapping shader
	drawFullscreenTriangle(cmd, m_ldrRenderPass,
		m_sceneLdrFB, m_extent,
		SDR_TYPE_POST_PROCESS_TONEMAPPING,
		m_sceneColor.view, m_linearSampler,
		&tmData, sizeof(tmData),
		ALPHA_BLEND_NONE);

	m_memoryManager->unmapMemory(m_bloomUBOAlloc);
	m_bloomUBOMapped = nullptr;
}

void VulkanPostProcessor::executeFXAA(vk::CommandBuffer cmd)
{
	if (!m_ldrInitialized || !gr_is_fxaa_mode(Gr_aa_mode)) {
		return;
	}

	m_bloomUBOMapped = m_memoryManager->mapMemory(m_bloomUBOAlloc);
	if (!m_bloomUBOMapped) {
		return;
	}

	// FXAA prepass: Scene_ldr → Scene_luminance (compute luma in alpha)
	drawFullscreenTriangle(cmd, m_ldrRenderPass,
		m_sceneLuminanceFB, m_extent,
		SDR_TYPE_POST_PROCESS_FXAA_PREPASS,
		m_sceneLdr.view, m_linearSampler,
		nullptr, 0,
		ALPHA_BLEND_NONE);

	// FXAA main pass: Scene_luminance → Scene_ldr
	graphics::generic_data::fxaa_data fxaaData;
	fxaaData.rt_w = static_cast<float>(m_extent.width);
	fxaaData.rt_h = static_cast<float>(m_extent.height);
	fxaaData.pad[0] = 0.0f;
	fxaaData.pad[1] = 0.0f;

	drawFullscreenTriangle(cmd, m_ldrRenderPass,
		m_sceneLdrFB, m_extent,
		SDR_TYPE_POST_PROCESS_FXAA,
		m_sceneLuminance.view, m_linearSampler,
		&fxaaData, sizeof(fxaaData),
		ALPHA_BLEND_NONE);

	m_memoryManager->unmapMemory(m_bloomUBOAlloc);
	m_bloomUBOMapped = nullptr;
}

bool VulkanPostProcessor::executePostEffects(vk::CommandBuffer cmd)
{
	m_postEffectsApplied = false;

	if (!m_ldrInitialized || !graphics::Post_processing_manager) {
		return false;
	}

	const auto& postEffects = graphics::Post_processing_manager->getPostEffects();
	if (postEffects.empty()) {
		return false;
	}

	// Compute effect flags from current state
	int effectFlags = 0;
	for (size_t idx = 0; idx < postEffects.size(); idx++) {
		if (postEffects[idx].always_on || (postEffects[idx].intensity != postEffects[idx].default_intensity)) {
			effectFlags |= (1 << idx);
		}
	}

	if (effectFlags == 0) {
		return false;
	}

	m_bloomUBOMapped = m_memoryManager->mapMemory(m_bloomUBOAlloc);
	if (!m_bloomUBOMapped) {
		return false;
	}

	// Build the extended post_data UBO with effectFlags appended
	struct PostEffectsUBOData {
		graphics::generic_data::post_data base;
		int effectFlags;
		int pad[3];
	};

	PostEffectsUBOData uboData;
	memset(&uboData, 0, sizeof(uboData));
	uboData.base.timer = static_cast<float>(timer_get_milliseconds() % 100 + 1);
	uboData.effectFlags = effectFlags;

	// Fill effect parameters
	for (size_t idx = 0; idx < postEffects.size(); idx++) {
		if (!(effectFlags & (1 << idx))) {
			continue;
		}
		float value = postEffects[idx].intensity;
		switch (postEffects[idx].uniform_type) {
		case graphics::PostEffectUniformType::NoiseAmount:
			uboData.base.noise_amount = value;
			break;
		case graphics::PostEffectUniformType::Saturation:
			uboData.base.saturation = value;
			break;
		case graphics::PostEffectUniformType::Brightness:
			uboData.base.brightness = value;
			break;
		case graphics::PostEffectUniformType::Contrast:
			uboData.base.contrast = value;
			break;
		case graphics::PostEffectUniformType::FilmGrain:
			uboData.base.film_grain = value;
			break;
		case graphics::PostEffectUniformType::TvStripes:
			uboData.base.tv_stripes = value;
			break;
		case graphics::PostEffectUniformType::Cutoff:
			uboData.base.cutoff = value;
			break;
		case graphics::PostEffectUniformType::Dither:
			uboData.base.dither = value;
			break;
		case graphics::PostEffectUniformType::Tint:
			uboData.base.tint = postEffects[idx].rgb;
			break;
		case graphics::PostEffectUniformType::CustomEffectVEC3A:
			uboData.base.custom_effect_vec3_a = postEffects[idx].rgb;
			break;
		case graphics::PostEffectUniformType::CustomEffectFloatA:
			uboData.base.custom_effect_float_a = value;
			break;
		case graphics::PostEffectUniformType::CustomEffectVEC3B:
			uboData.base.custom_effect_vec3_b = postEffects[idx].rgb;
			break;
		case graphics::PostEffectUniformType::CustomEffectFloatB:
			uboData.base.custom_effect_float_b = value;
			break;
		default:
			break;
		}
	}

	// Post-effects: Scene_ldr → Scene_luminance (reusing luminance target as temp)
	drawFullscreenTriangle(cmd, m_ldrRenderPass,
		m_sceneLuminanceFB, m_extent,
		SDR_TYPE_POST_PROCESS_MAIN,
		m_sceneLdr.view, m_linearSampler,
		&uboData, sizeof(uboData),
		ALPHA_BLEND_NONE);

	m_memoryManager->unmapMemory(m_bloomUBOAlloc);
	m_bloomUBOMapped = nullptr;

	m_postEffectsApplied = true;
	return true;
}

void VulkanPostProcessor::executeLightshafts(vk::CommandBuffer cmd)
{
	if (!m_ldrInitialized || !graphics::Post_processing_manager) {
		return;
	}

	if (Game_subspace_effect || !gr_sunglare_enabled() || !gr_lightshafts_enabled()) {
		return;
	}

	// Find a global light with glare facing the camera
	int n_lights = light_get_global_count();
	float sun_x = 0.0f, sun_y = 0.0f;
	bool found = false;

	for (int idx = 0; idx < n_lights; idx++) {
		vec3d light_dir;
		light_get_global_dir(&light_dir, idx);

		if (!light_has_glare(idx)) {
			continue;
		}

		float dot = vm_vec_dot(&light_dir, &Eye_matrix.vec.fvec);
		if (dot > 0.7f) {
			sun_x = asinf_safe(vm_vec_dot(&light_dir, &Eye_matrix.vec.rvec)) / PI * 1.5f + 0.5f;
			sun_y = asinf_safe(vm_vec_dot(&light_dir, &Eye_matrix.vec.uvec)) / PI * 1.5f * gr_screen.clip_aspect + 0.5f;
			found = true;
			break;
		}
	}

	if (!found) {
		return;
	}

	// Transition scene depth from eDepthStencilAttachmentOptimal to eShaderReadOnlyOptimal for sampling
	{
		vk::ImageMemoryBarrier barrier;
		barrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
		barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
		barrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = m_sceneDepth.image;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
		if (m_depthFormat == vk::Format::eD24UnormS8Uint || m_depthFormat == vk::Format::eD32SfloatS8Uint) {
			barrier.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
		}
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eLateFragmentTests,
			vk::PipelineStageFlagBits::eFragmentShader,
			{}, {}, {}, barrier);
	}

	// Transition Scene_ldr to eColorAttachmentOptimal for loadOp=eLoad render pass
	{
		vk::ImageMemoryBarrier barrier;
		barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
		barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = m_sceneLdr.image;
		barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eFragmentShader,
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			{}, {}, {}, barrier);
	}

	// Build lightshaft UBO data
	auto& ls_params = graphics::Post_processing_manager->getLightshaftParams();

	graphics::generic_data::lightshaft_data lsData;
	lsData.sun_pos.x = sun_x;
	lsData.sun_pos.y = sun_y;
	lsData.density = ls_params.density;
	lsData.weight = ls_params.weight;
	lsData.falloff = ls_params.falloff;
	lsData.intensity = Sun_spot * ls_params.intensity;
	lsData.cp_intensity = Sun_spot * ls_params.cpintensity;
	lsData.pad[0] = 0.0f;

	m_bloomUBOMapped = m_memoryManager->mapMemory(m_bloomUBOAlloc);
	if (!m_bloomUBOMapped) {
		return;
	}

	// Additive blend lightshafts onto Scene_ldr
	drawFullscreenTriangle(cmd, m_ldrLoadRenderPass,
		m_sceneLdrFB, m_extent,
		SDR_TYPE_POST_PROCESS_LIGHTSHAFTS,
		m_sceneDepth.view, m_linearSampler,
		&lsData, sizeof(lsData),
		ALPHA_BLEND_ADDITIVE);

	m_memoryManager->unmapMemory(m_bloomUBOAlloc);
	m_bloomUBOMapped = nullptr;
}

void VulkanPostProcessor::blitToSwapChain(vk::CommandBuffer cmd)
{
	// If LDR targets exist, executeTonemap()+executeFXAA() already ran.
	// Blit from the latest post-processing result with passthrough settings.
	// Otherwise, fall back to direct HDR→swap chain tonemapping.
	bool useLdr = m_ldrInitialized;

	if (!useLdr) {
		// Update tonemapping parameters from engine lighting profile
		updateTonemappingUBO();
	}

	auto* pipelineMgr = getPipelineManager();
	auto* descriptorMgr = getDescriptorManager();
	auto* stateTracker = getStateTracker();
	auto* bufferMgr = getBufferManager();

	if (!pipelineMgr || !descriptorMgr || !stateTracker || !bufferMgr) {
		return;
	}

	// Build pipeline config for tonemapping (fullscreen, no depth, no blending)
	// sRGB conversion is controlled by the linearOut UBO field, not shader variants
	PipelineConfig config;
	config.shaderType = SDR_TYPE_POST_PROCESS_TONEMAPPING;
	config.vertexLayoutHash = 0;  // Empty vertex layout
	config.primitiveType = PRIM_TYPE_TRIS;
	config.depthMode = ZBUFFER_TYPE_NONE;
	config.blendMode = ALPHA_BLEND_NONE;
	config.cullEnabled = false;
	config.depthWriteEnabled = false;
	config.renderPass = stateTracker->getCurrentRenderPass();

	// Get or create the pipeline
	vertex_layout emptyLayout;  // No vertex components
	vk::Pipeline pipeline = pipelineMgr->getPipeline(config, emptyLayout);
	if (!pipeline) {
		mprintf(("VulkanPostProcessor: Failed to get tonemapping pipeline!\n"));
		return;
	}

	vk::PipelineLayout pipelineLayout = pipelineMgr->getPipelineLayout();
	stateTracker->bindPipeline(pipeline, pipelineLayout);

	// Set viewport (non-flipped for post-processing — textures are already
	// in the correct Vulkan orientation, no Y-flip needed)
	stateTracker->setViewport(0.0f, 0.0f,
		static_cast<float>(m_extent.width),
		static_cast<float>(m_extent.height));

	stateTracker->applyDynamicState();

	// Allocate and write Material descriptor set (Set 1) with source texture
	vk::DescriptorSet materialSet = descriptorMgr->allocateFrameSet(DescriptorSetIndex::Material);
	if (!materialSet) {
		return;
	}

	{
		// Bind source texture based on post-processing chain state:
		// - Post-effects ran: read Scene_luminance (post-effects output)
		// - LDR only (tonemap/FXAA): read Scene_ldr
		// - No LDR: read Scene_color (raw HDR, tonemapping applied by this shader)
		vk::DescriptorImageInfo imageInfo;
		imageInfo.sampler = m_linearSampler;
		imageInfo.imageView = m_postEffectsApplied ? m_sceneLuminance.view
		                    : useLdr ? m_sceneLdr.view
		                    : m_sceneColor.view;
		imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

		vk::WriteDescriptorSet write;
		write.dstSet = materialSet;
		write.dstBinding = 1;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		write.pImageInfo = &imageInfo;

		// Pre-initialize binding 0 (ModelData UBO) with fallback zero buffer
		auto fallbackBuffer = bufferMgr->getFallbackUniformBuffer();
		vk::DescriptorBufferInfo bufferInfo;
		bufferInfo.buffer = fallbackBuffer;
		bufferInfo.offset = 0;
		bufferInfo.range = 4096;

		vk::WriteDescriptorSet uboWrite;
		uboWrite.dstSet = materialSet;
		uboWrite.dstBinding = 0;
		uboWrite.dstArrayElement = 0;
		uboWrite.descriptorCount = 1;
		uboWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
		uboWrite.pBufferInfo = &bufferInfo;

		// Pre-initialize binding 2 (DecalGlobals UBO) with fallback
		vk::WriteDescriptorSet decalWrite;
		decalWrite.dstSet = materialSet;
		decalWrite.dstBinding = 2;
		decalWrite.dstArrayElement = 0;
		decalWrite.descriptorCount = 1;
		decalWrite.descriptorType = vk::DescriptorType::eUniformBuffer;
		decalWrite.pBufferInfo = &bufferInfo;

		// Fill remaining texture array elements with fallback
		auto* texMgr = getTextureManager();
		vk::ImageView fallbackView = texMgr->getFallbackTextureView();
		vk::Sampler defaultSampler = texMgr->getDefaultSampler();

		SCP_vector<vk::DescriptorImageInfo> fallbackImages(VulkanDescriptorManager::MAX_TEXTURE_BINDINGS - 1);
		for (auto& fi : fallbackImages) {
			fi.sampler = defaultSampler;
			fi.imageView = fallbackView;
			fi.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		}

		vk::WriteDescriptorSet fallbackTexWrite;
		fallbackTexWrite.dstSet = materialSet;
		fallbackTexWrite.dstBinding = 1;
		fallbackTexWrite.dstArrayElement = 1;
		fallbackTexWrite.descriptorCount = static_cast<uint32_t>(fallbackImages.size());
		fallbackTexWrite.descriptorType = vk::DescriptorType::eCombinedImageSampler;
		fallbackTexWrite.pImageInfo = fallbackImages.data();

		std::array<vk::WriteDescriptorSet, 4> writes = {write, uboWrite, decalWrite, fallbackTexWrite};
		m_device.updateDescriptorSets(writes, {});
	}

	stateTracker->bindDescriptorSet(DescriptorSetIndex::Material, materialSet);

	// Allocate and write PerDraw descriptor set (Set 2) with tonemapping UBO
	// For now, use fallback (zero) UBO — exposure=0 would give black,
	// so we need a valid graphics::generic_data::tonemapping_data with exposure=1.0 and tonemapper=0 (linear).
	vk::DescriptorSet perDrawSet = descriptorMgr->allocateFrameSet(DescriptorSetIndex::PerDraw);
	if (!perDrawSet) {
		return;
	}

	{
		// When blitting LDR, use passthrough tonemapping (exposure=1, linear)
		// Otherwise, use the real tonemapping UBO (already updated above)
		if (useLdr) {
			auto* mapped = static_cast<graphics::generic_data::tonemapping_data*>(
				m_memoryManager->mapMemory(m_tonemapUBOAlloc));
			if (mapped) {
				memset(mapped, 0, sizeof(graphics::generic_data::tonemapping_data));
				mapped->exposure = 1.0f;
				mapped->tonemapper = 0;  // Linear passthrough
				mapped->linearOut = 1;   // Skip sRGB — LDR input already has sRGB applied
				m_memoryManager->unmapMemory(m_tonemapUBOAlloc);
			}
		}

		vk::DescriptorBufferInfo uboInfo;
		uboInfo.buffer = m_tonemapUBO;
		uboInfo.offset = 0;
		uboInfo.range = sizeof(graphics::generic_data::tonemapping_data);

		vk::WriteDescriptorSet write;
		write.dstSet = perDrawSet;
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = vk::DescriptorType::eUniformBuffer;
		write.pBufferInfo = &uboInfo;

		// Pre-initialize other bindings with fallback
		auto fallbackBuffer = bufferMgr->getFallbackUniformBuffer();
		vk::DescriptorBufferInfo fallbackInfo;
		fallbackInfo.buffer = fallbackBuffer;
		fallbackInfo.offset = 0;
		fallbackInfo.range = 4096;

		SCP_vector<vk::WriteDescriptorSet> writes;
		writes.push_back(write);

		// Bindings 1-4: Matrices, NanoVGData, DecalInfo, MovieData
		for (uint32_t b = 1; b <= 4; ++b) {
			vk::WriteDescriptorSet fw;
			fw.dstSet = perDrawSet;
			fw.dstBinding = b;
			fw.dstArrayElement = 0;
			fw.descriptorCount = 1;
			fw.descriptorType = vk::DescriptorType::eUniformBuffer;
			fw.pBufferInfo = &fallbackInfo;
			writes.push_back(fw);
		}

		m_device.updateDescriptorSets(writes, {});
	}

	stateTracker->bindDescriptorSet(DescriptorSetIndex::PerDraw, perDrawSet);

	// Draw fullscreen triangle (3 vertices from gl_VertexIndex, no vertex buffer)
	cmd.draw(3, 1, 0, 0);
}

bool VulkanPostProcessor::createImage(uint32_t width, uint32_t height, vk::Format format,
                                      vk::ImageUsageFlags usage, vk::ImageAspectFlags aspect,
                                      vk::Image& outImage, vk::ImageView& outView,
                                      VulkanAllocation& outAllocation)
{
	// Create image
	vk::ImageCreateInfo imageInfo;
	imageInfo.imageType = vk::ImageType::e2D;
	imageInfo.format = format;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = vk::SampleCountFlagBits::e1;
	imageInfo.tiling = vk::ImageTiling::eOptimal;
	imageInfo.usage = usage;
	imageInfo.sharingMode = vk::SharingMode::eExclusive;
	imageInfo.initialLayout = vk::ImageLayout::eUndefined;

	try {
		outImage = m_device.createImage(imageInfo);
	} catch (const vk::SystemError& e) {
		mprintf(("VulkanPostProcessor: Failed to create image: %s\n", e.what()));
		return false;
	}

	// Allocate memory
	if (!m_memoryManager->allocateImageMemory(outImage, MemoryUsage::GpuOnly, outAllocation)) {
		mprintf(("VulkanPostProcessor: Failed to allocate image memory!\n"));
		m_device.destroyImage(outImage);
		outImage = nullptr;
		return false;
	}

	// Create image view (plain 2D, not array)
	vk::ImageViewCreateInfo viewInfo;
	viewInfo.image = outImage;
	viewInfo.viewType = vk::ImageViewType::e2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = aspect;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	try {
		outView = m_device.createImageView(viewInfo);
	} catch (const vk::SystemError& e) {
		mprintf(("VulkanPostProcessor: Failed to create image view: %s\n", e.what()));
		m_device.destroyImage(outImage);
		m_memoryManager->freeAllocation(outAllocation);
		outImage = nullptr;
		return false;
	}

	return true;
}

} // namespace vulkan
} // namespace graphics

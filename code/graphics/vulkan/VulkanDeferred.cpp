
#include "VulkanDeferred.h"
#include "VulkanRenderer.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanDescriptorManager.h"
#include "VulkanPipeline.h"
#include "VulkanState.h"
#include "VulkanDraw.h"
#include "VulkanPostProcessing.h"
#include "gr_vulkan.h"

#include "graphics/2d.h"
#include "graphics/matrix.h"
#include "graphics/material.h"
#include "graphics/grinternal.h"
#include "graphics/shadows.h"
#include "lighting/lighting.h"
#include "mission/missionparse.h"
#include "nebula/neb.h"
#include "nebula/volumetrics.h"

namespace graphics {
namespace vulkan {

namespace {

static bool s_vulkanOverrideFog = false;

} // anonymous namespace

// ========== Deferred Lighting ==========

void vulkan_deferred_lighting_begin(bool clearNonColorBufs)
{
	if (!light_deferred_enabled()) {
		return;
	}

	auto* pp = getPostProcessor();
	if (!pp || !pp->isGbufInitialized()) {
		return;
	}

	auto* renderer = getRendererInstance();
	if (!renderer->isSceneRendering()) {
		return;
	}

	auto* stateTracker = getStateTracker();
	vk::CommandBuffer cmd = stateTracker->getCommandBuffer();

	// End the current G-buffer render pass to perform the color→emissive copy.
	// All 6 color attachments transition to eShaderReadOnlyOptimal (finalLayout).
	cmd.endRenderPass();

	// Transition scene color (attachment 0): eShaderReadOnlyOptimal → eTransferSrc
	// Transition emissive (attachment 4): eShaderReadOnlyOptimal → eTransferDst
	{
		std::array<vk::ImageMemoryBarrier, 2> barriers;

		// Scene color → transfer source
		barriers[0].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		barriers[0].dstAccessMask = vk::AccessFlagBits::eTransferRead;
		barriers[0].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barriers[0].newLayout = vk::ImageLayout::eTransferSrcOptimal;
		barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].image = pp->getSceneColorImage();
		barriers[0].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

		// Emissive → transfer destination
		barriers[1].srcAccessMask = {};
		barriers[1].dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		barriers[1].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barriers[1].newLayout = vk::ImageLayout::eTransferDstOptimal;
		barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[1].image = pp->getGbufEmissiveImage();
		barriers[1].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::PipelineStageFlagBits::eTransfer,
			{}, nullptr, nullptr, barriers);
	}

	// Copy scene color → emissive (pre-deferred content becomes emissive)
	{
		auto extent = pp->getSceneExtent();
		vk::ImageCopy region;
		region.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
		region.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
		region.extent = vk::Extent3D(extent.width, extent.height, 1);
		cmd.copyImage(
			pp->getSceneColorImage(), vk::ImageLayout::eTransferSrcOptimal,
			pp->getGbufEmissiveImage(), vk::ImageLayout::eTransferDstOptimal,
			region);
	}

	// Transition scene color back to eColorAttachmentOptimal.
	// Transition emissive to eShaderReadOnlyOptimal (where transitionGbufForResume expects it).
	{
		std::array<vk::ImageMemoryBarrier, 2> barriers;

		barriers[0].srcAccessMask = vk::AccessFlagBits::eTransferRead;
		barriers[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		barriers[0].oldLayout = vk::ImageLayout::eTransferSrcOptimal;
		barriers[0].newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].image = pp->getSceneColorImage();
		barriers[0].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

		barriers[1].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barriers[1].dstAccessMask = {};
		barriers[1].oldLayout = vk::ImageLayout::eTransferDstOptimal;
		barriers[1].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[1].image = pp->getGbufEmissiveImage();
		barriers[1].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			{}, nullptr, nullptr, barriers);
	}

	// Transition G-buffer attachments 1-5 from eShaderReadOnlyOptimal → eColorAttachmentOptimal
	pp->transitionGbufForResume(cmd);

	// Resume G-buffer render pass with eLoad
	{
		auto extent = pp->getSceneExtent();
		vk::RenderPassBeginInfo rpBegin;
		rpBegin.renderPass = pp->getGbufRenderPassLoad();
		rpBegin.framebuffer = pp->getGbufFramebuffer();
		rpBegin.renderArea.offset = vk::Offset2D(0, 0);
		rpBegin.renderArea.extent = extent;
		std::array<vk::ClearValue, 7> clearValues{};
		clearValues[6].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);
		rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpBegin.pClearValues = clearValues.data();
		cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
		stateTracker->setRenderPass(pp->getGbufRenderPassLoad(), 0);
	}

	// Don't clear attachment 0 — keep the pre-deferred content (starfield, backgrounds).
	// The model shader writes fragOut0 = baseColor on top via depth test.
	// In the full pipeline, attachment 0 will be cleared here and the
	// background will be recovered from emissive during light accumulation.
	//
	// Optionally clear non-color G-buffer attachments (position, normal, specular, composite).
	// These were already cleared by the eClear render pass but the caller may want to
	// re-clear them (e.g. between cockpit and external rendering passes).
	if (clearNonColorBufs) {
		vk::ClearAttachment clearAtt;
		clearAtt.aspectMask = vk::ImageAspectFlagBits::eColor;
		clearAtt.clearValue.color.setFloat32({0.0f, 0.0f, 0.0f, 0.0f});

		auto extent = pp->getSceneExtent();
		vk::ClearRect clearRect;
		clearRect.rect.offset = vk::Offset2D(0, 0);
		clearRect.rect.extent = extent;
		clearRect.baseArrayLayer = 0;
		clearRect.layerCount = 1;

		for (uint32_t att : {1u, 2u, 3u, 5u}) {
			clearAtt.colorAttachment = att;
			cmd.clearAttachments(clearAtt, clearRect);
		}
	}

	Deferred_lighting = true;
}

void vulkan_deferred_lighting_msaa()
{
	// No MSAA support in Vulkan deferred yet
}

void vulkan_deferred_lighting_end()
{
	if (!Deferred_lighting) {
		return;
	}

	Deferred_lighting = false;

	// After this, rendering goes back to writing only attachment 0.
	// The pipeline still has 6 blend states (matching the G-buffer render pass)
	// but the shader only outputs to location 0. Attachments 1-5 are untouched.
}

void vulkan_deferred_lighting_finish()
{
	if (!light_deferred_enabled()) {
		return;
	}

	auto* pp = getPostProcessor();
	if (!pp || !pp->isGbufInitialized()) {
		return;
	}

	auto* renderer = getRendererInstance();
	if (!renderer->isSceneRendering()) {
		return;
	}

	auto* stateTracker = getStateTracker();
	vk::CommandBuffer cmd = stateTracker->getCommandBuffer();

	// 1. End G-buffer render pass
	// All 6 color attachments → eShaderReadOnlyOptimal
	// Depth → eDepthStencilAttachmentOptimal
	cmd.endRenderPass();

	// 2. Copy emissive → composite (the emissive data becomes the base for light accumulation)
	{
		// Transition emissive: eShaderReadOnlyOptimal → eTransferSrcOptimal
		// Transition composite: eShaderReadOnlyOptimal → eTransferDstOptimal
		std::array<vk::ImageMemoryBarrier, 2> barriers;

		barriers[0].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		barriers[0].dstAccessMask = vk::AccessFlagBits::eTransferRead;
		barriers[0].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barriers[0].newLayout = vk::ImageLayout::eTransferSrcOptimal;
		barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].image = pp->getGbufEmissiveImage();
		barriers[0].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

		barriers[1].srcAccessMask = {};
		barriers[1].dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		barriers[1].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barriers[1].newLayout = vk::ImageLayout::eTransferDstOptimal;
		barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[1].image = pp->getGbufCompositeImage();
		barriers[1].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::PipelineStageFlagBits::eTransfer,
			{}, nullptr, nullptr, barriers);

		// Copy
		auto extent = pp->getSceneExtent();
		vk::ImageCopy region;
		region.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
		region.srcOffset = vk::Offset3D(0, 0, 0);
		region.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
		region.dstOffset = vk::Offset3D(0, 0, 0);
		region.extent = vk::Extent3D(extent.width, extent.height, 1);

		cmd.copyImage(
			pp->getGbufEmissiveImage(), vk::ImageLayout::eTransferSrcOptimal,
			pp->getGbufCompositeImage(), vk::ImageLayout::eTransferDstOptimal,
			region);

		// Transition emissive back to eShaderReadOnlyOptimal (done with it)
		// Transition composite to eColorAttachmentOptimal (for light accum render pass)
		barriers[0].srcAccessMask = vk::AccessFlagBits::eTransferRead;
		barriers[0].dstAccessMask = vk::AccessFlagBits::eShaderRead;
		barriers[0].oldLayout = vk::ImageLayout::eTransferSrcOptimal;
		barriers[0].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barriers[0].image = pp->getGbufEmissiveImage();

		barriers[1].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barriers[1].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		barriers[1].oldLayout = vk::ImageLayout::eTransferDstOptimal;
		barriers[1].newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		barriers[1].image = pp->getGbufCompositeImage();

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader,
			{}, nullptr, nullptr, barriers);
	}

	// 3. Render deferred lights (begins + ends light accum render pass internally)
	// After this, composite is in eShaderReadOnlyOptimal
	pp->renderDeferredLights(cmd);

	// 4. Fog rendering (between light accumulation and forward rendering)
	// Matches OpenGL flow in opengl_deferred_lighting_finish()
	bool bDrawFullNeb = The_mission.flags[Mission::Mission_Flags::Fullneb]
		&& Neb2_render_mode != NEB2_RENDER_NONE && !s_vulkanOverrideFog;
	bool bDrawNebVolumetrics = The_mission.volumetrics
		&& The_mission.volumetrics->get_enabled() && !s_vulkanOverrideFog;

	bool fogRendered = false;
	if (bDrawFullNeb) {
		// Scene fog reads composite + depth → writes scene color
		pp->renderSceneFog(cmd);
		fogRendered = true;

		if (bDrawNebVolumetrics) {
			// Copy scene color → composite so volumetric reads the fogged result
			pp->copySceneColorToComposite(cmd);
		}
	}
	if (bDrawNebVolumetrics) {
		// Volumetric fog reads composite + emissive + depth + 3D volumes → writes scene color
		pp->renderVolumetricFog(cmd);
		fogRendered = true;
	}

	if (!fogRendered) {
		// No fog — copy composite → scene color (existing behavior)
		auto extent = pp->getSceneExtent();

		// Transition composite: eShaderReadOnlyOptimal → eTransferSrcOptimal
		// Transition scene color: eShaderReadOnlyOptimal → eTransferDstOptimal
		std::array<vk::ImageMemoryBarrier, 2> barriers;

		barriers[0].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		barriers[0].dstAccessMask = vk::AccessFlagBits::eTransferRead;
		barriers[0].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barriers[0].newLayout = vk::ImageLayout::eTransferSrcOptimal;
		barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].image = pp->getGbufCompositeImage();
		barriers[0].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

		barriers[1].srcAccessMask = {};
		barriers[1].dstAccessMask = vk::AccessFlagBits::eTransferWrite;
		barriers[1].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barriers[1].newLayout = vk::ImageLayout::eTransferDstOptimal;
		barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[1].image = pp->getSceneColorImage();
		barriers[1].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			vk::PipelineStageFlagBits::eTransfer,
			{}, nullptr, nullptr, barriers);

		vk::ImageCopy region;
		region.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
		region.srcOffset = vk::Offset3D(0, 0, 0);
		region.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
		region.dstOffset = vk::Offset3D(0, 0, 0);
		region.extent = vk::Extent3D(extent.width, extent.height, 1);

		cmd.copyImage(
			pp->getGbufCompositeImage(), vk::ImageLayout::eTransferSrcOptimal,
			pp->getSceneColorImage(), vk::ImageLayout::eTransferDstOptimal,
			region);

		// Transition scene color: eTransferDstOptimal → eColorAttachmentOptimal
		vk::ImageMemoryBarrier sceneBarrier;
		sceneBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		sceneBarrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite;
		sceneBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
		sceneBarrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		sceneBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		sceneBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		sceneBarrier.image = pp->getSceneColorImage();
		sceneBarrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eTransfer,
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			{}, nullptr, nullptr, sceneBarrier);
	}

	// 5. Switch to scene render pass for forward transparent objects
	// After light accumulation, use the 2-attachment scene render pass instead
	// of the 6-attachment G-buffer pass. Forward-rendered transparent objects
	// only write to fragOut0 — using the G-buffer pass would leave undefined
	// values at attachment locations 1-5.
	renderer->setUseGbufRenderPass(false);
	stateTracker->setColorAttachmentCount(1);

	// Resume scene render pass (loadOp=eLoad) with depth preserved
	{
		auto extent = pp->getSceneExtent();
		vk::RenderPassBeginInfo rpBegin;
		rpBegin.renderPass = pp->getSceneRenderPassLoad();
		rpBegin.framebuffer = pp->getSceneFramebuffer();
		rpBegin.renderArea.offset = vk::Offset2D(0, 0);
		rpBegin.renderArea.extent = extent;
		std::array<vk::ClearValue, 2> clearValues;
		clearValues[0].color.setFloat32({0.0f, 0.0f, 0.0f, 1.0f});
		clearValues[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);
		rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpBegin.pClearValues = clearValues.data();
		cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
		stateTracker->setRenderPass(pp->getSceneRenderPassLoad(), 0);
	}
}

void vulkan_override_fog(bool set_override) {
	s_vulkanOverrideFog = set_override;
}

// ========== Shadow Map Rendering ==========

} // namespace vulkan
} // namespace graphics

extern bool Glowpoint_override;
extern bool gr_htl_projection_matrix_set;

namespace graphics {
namespace vulkan {

namespace {
static bool Glowpoint_override_save = false;
} // anonymous namespace

void vulkan_shadow_map_start(matrix4* shadow_view_matrix, const matrix* light_matrix, vec3d* eye_pos)
{
	if (Shadow_quality == ShadowQuality::Disabled || !getRendererInstance()->supportsShaderViewportLayerOutput()) {
		return;
	}

	// Shadows require the G-buffer render pass (deferred lighting).
	// In contexts without deferred lighting (e.g. tech room), the active
	// render pass is the swap chain or 2-attachment scene pass — ending it
	// and resuming the G-buffer pass would break rendering.
	if (!getRendererInstance()->isUsingGbufRenderPass()) {
		return;
	}

	auto* pp = getPostProcessor();
	if (!pp) {
		return;
	}

	// Lazy-init shadow resources
	if (!pp->isShadowInitialized()) {
		if (!pp->initShadowPass()) {
			return;
		}
	}

	auto* stateTracker = getStateTracker();
	vk::CommandBuffer cmd = stateTracker->getCommandBuffer();

	// End the current G-buffer render pass
	cmd.endRenderPass();

	// Begin shadow render pass (eClear for both color and depth)
	{
		int shadowSize = pp->getShadowTextureSize();
		vk::RenderPassBeginInfo rpBegin;
		rpBegin.renderPass = pp->getShadowRenderPass();
		rpBegin.framebuffer = pp->getShadowFramebuffer();
		rpBegin.renderArea.offset = vk::Offset2D(0, 0);
		rpBegin.renderArea.extent = vk::Extent2D(static_cast<uint32_t>(shadowSize), static_cast<uint32_t>(shadowSize));

		std::array<vk::ClearValue, 2> clearValues;
		clearValues[0].color.setFloat32({0.0f, 0.0f, 0.0f, 1.0f});
		clearValues[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);
		rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpBegin.pClearValues = clearValues.data();

		cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
		stateTracker->setRenderPass(pp->getShadowRenderPass(), 0);
		stateTracker->setColorAttachmentCount(1);
	}

	// Set viewport and scissor to shadow texture size
	{
		int shadowSize = pp->getShadowTextureSize();
		vk::Viewport viewport;
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(shadowSize);
		viewport.height = static_cast<float>(shadowSize);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		cmd.setViewport(0, viewport);

		vk::Rect2D scissor;
		scissor.offset = vk::Offset2D(0, 0);
		scissor.extent = vk::Extent2D(static_cast<uint32_t>(shadowSize), static_cast<uint32_t>(shadowSize));
		cmd.setScissor(0, scissor);
	}

	Rendering_to_shadow_map = true;
	Glowpoint_override_save = Glowpoint_override;
	Glowpoint_override = true;

	gr_htl_projection_matrix_set = true;

	gr_set_view_matrix(eye_pos, light_matrix);

	*shadow_view_matrix = gr_view_matrix;
}

void vulkan_shadow_map_end()
{
	if (!Rendering_to_shadow_map) {
		return;
	}

	auto* pp = getPostProcessor();
	auto* stateTracker = getStateTracker();
	vk::CommandBuffer cmd = stateTracker->getCommandBuffer();

	gr_end_view_matrix();
	Rendering_to_shadow_map = false;

	gr_zbuffer_set(ZBUFFER_TYPE_FULL);

	Glowpoint_override = Glowpoint_override_save;
	gr_htl_projection_matrix_set = false;

	// End shadow render pass (color transitions to eShaderReadOnlyOptimal via finalLayout)
	cmd.endRenderPass();

	// Transition scene color: eShaderReadOnlyOptimal → eColorAttachmentOptimal
	// (Scene color was in eShaderReadOnlyOptimal from ending G-buffer pass before shadow start)
	{
		vk::ImageMemoryBarrier barrier;
		barrier.srcAccessMask = {};
		barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = pp->getSceneColorImage();
		barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe,
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			{}, nullptr, nullptr, barrier);
	}

	// Transition G-buffer attachments 1-5 for resume
	pp->transitionGbufForResume(cmd);

	// Resume G-buffer render pass with eLoad
	{
		auto extent = pp->getSceneExtent();
		vk::RenderPassBeginInfo rpBegin;
		rpBegin.renderPass = pp->getGbufRenderPassLoad();
		rpBegin.framebuffer = pp->getGbufFramebuffer();
		rpBegin.renderArea.offset = vk::Offset2D(0, 0);
		rpBegin.renderArea.extent = extent;

		std::array<vk::ClearValue, 7> clearValues{};
		clearValues[6].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);
		rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpBegin.pClearValues = clearValues.data();

		cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
		stateTracker->setRenderPass(pp->getGbufRenderPassLoad(), 0);
		stateTracker->setColorAttachmentCount(VulkanPostProcessor::GBUF_COLOR_ATTACHMENT_COUNT);
	}

	// Restore viewport and scissor to scene size
	{
		vk::Viewport viewport;
		viewport.x = static_cast<float>(gr_screen.offset_x);
		viewport.y = static_cast<float>(gr_screen.offset_y);
		viewport.width = static_cast<float>(gr_screen.clip_width);
		viewport.height = static_cast<float>(gr_screen.clip_height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		cmd.setViewport(0, viewport);

		vk::Rect2D scissor;
		scissor.offset = vk::Offset2D(gr_screen.offset_x, gr_screen.offset_y);
		scissor.extent = vk::Extent2D(static_cast<uint32_t>(gr_screen.clip_width), static_cast<uint32_t>(gr_screen.clip_height));
		cmd.setScissor(0, scissor);
	}
}

// ========== Decal Pass ==========

void vulkan_start_decal_pass()
{
	auto* renderer = getRendererInstance();
	auto* pp = getPostProcessor();
	auto* stateTracker = getStateTracker();

	if (!renderer->isSceneRendering() || !pp || !pp->isGbufInitialized()) {
		return;
	}

	vk::CommandBuffer cmd = stateTracker->getCommandBuffer();

	// End the G-buffer render pass (transitions all color attachments to eShaderReadOnlyOptimal)
	cmd.endRenderPass();

	// Copy scene depth → samplable depth copy (for fragment depth reconstruction)
	pp->copySceneDepth(cmd);

	// Copy G-buffer normal → samplable normal copy (for angle rejection)
	pp->copyGbufNormal(cmd);

	// Transition scene color: eShaderReadOnlyOptimal → eColorAttachmentOptimal
	{
		vk::ImageMemoryBarrier barrier;
		barrier.srcAccessMask = {};
		barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = pp->getSceneColorImage();
		barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

		cmd.pipelineBarrier(
			vk::PipelineStageFlagBits::eTopOfPipe,
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
			{}, nullptr, nullptr, barrier);
	}

	// Transition G-buffer attachments 1-5 for render pass resume
	pp->transitionGbufForResume(cmd);

	// Resume G-buffer render pass with eLoad
	{
		auto extent = pp->getSceneExtent();
		vk::RenderPassBeginInfo rpBegin;
		rpBegin.renderPass = pp->getGbufRenderPassLoad();
		rpBegin.framebuffer = pp->getGbufFramebuffer();
		rpBegin.renderArea.offset = vk::Offset2D(0, 0);
		rpBegin.renderArea.extent = extent;

		std::array<vk::ClearValue, 7> clearValues{};
		clearValues[6].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);
		rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
		rpBegin.pClearValues = clearValues.data();

		cmd.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
		stateTracker->setRenderPass(pp->getGbufRenderPassLoad(), 0);
		stateTracker->setColorAttachmentCount(VulkanPostProcessor::GBUF_COLOR_ATTACHMENT_COUNT);
	}

	// Restore viewport (Y-flipped for Vulkan scene rendering)
	auto extent = pp->getSceneExtent();
	stateTracker->setViewport(0.0f,
		static_cast<float>(extent.height),
		static_cast<float>(extent.width),
		-static_cast<float>(extent.height));
}

void vulkan_stop_decal_pass()
{
	// No-op — decals draw within the resumed G-buffer render pass
}

void vulkan_render_decals(decal_material* material_info,
                          primitive_type prim_type,
                          vertex_layout* layout,
                          int num_elements,
                          const indexed_vertex_source& buffers,
                          const gr_buffer_handle& instance_buffer,
                          int num_instances)
{
	if (!material_info || !layout || num_instances <= 0) {
		return;
	}

	auto* stateTracker = getStateTracker();
	auto* pipelineManager = getPipelineManager();
	auto* descManager = getDescriptorManager();
	auto* bufferManager = getBufferManager();
	auto* drawManager = getDrawManager();
	auto* texManager = getTextureManager();
	auto* pp = getPostProcessor();

	// Set up matrices
	gr_matrix_set_uniforms();

	// Build pipeline config for decal rendering
	PipelineConfig config;
	config.shaderType = SDR_TYPE_DECAL;
	config.primitiveType = prim_type;
	config.depthMode = material_info->get_depth_mode();
	config.depthWriteEnabled = false;
	config.cullEnabled = false;
	config.frontFaceCW = false;
	config.blendMode = material_info->get_blend_mode();
	config.renderPass = stateTracker->getCurrentRenderPass();
	config.colorAttachmentCount = stateTracker->getColorAttachmentCount();

	// Per-attachment blend: active attachments (0=color, 2=normal, 4=emissive) get
	// the material's blend mode with RGB-only write mask. Inactive attachments get
	// write mask = 0 to avoid corrupting G-buffer data.
	config.perAttachmentBlendEnabled = true;
	for (uint32_t i = 0; i < config.colorAttachmentCount; ++i) {
		config.attachmentBlends[i].blendMode = ALPHA_BLEND_NONE;
		config.attachmentBlends[i].writeMask = {false, false, false, false};
	}
	// Attachment 0: color/diffuse — use material blend mode 0
	config.attachmentBlends[0].blendMode = material_info->get_blend_mode(0);
	config.attachmentBlends[0].writeMask = {true, true, true, false};
	// Attachment 2: normal — always additive
	config.attachmentBlends[2].blendMode = ALPHA_BLEND_ADDITIVE;
	config.attachmentBlends[2].writeMask = {true, true, true, false};
	// Attachment 4: emissive — use material blend mode 2
	config.attachmentBlends[4].blendMode = material_info->get_blend_mode(2);
	config.attachmentBlends[4].writeMask = {true, true, true, false};

	// Get or create pipeline
	vk::Pipeline pipeline = pipelineManager->getPipeline(config, *layout);
	if (!pipeline) {
		mprintf(("vulkan_render_decals: Failed to get pipeline!\n"));
		return;
	}

	stateTracker->bindPipeline(pipeline, pipelineManager->getPipelineLayout());

	// Get fallback resources
	vk::Buffer fallbackUBO = bufferManager->getFallbackUniformBuffer();
	vk::DeviceSize fallbackUBOSize = static_cast<vk::DeviceSize>(bufferManager->getFallbackUniformBufferSize());
	vk::Sampler fallbackSampler = texManager->getDefaultSampler();
	vk::ImageView fallbackView = texManager->getFallbackTextureView();
	vk::ImageView fallbackView2D = texManager->getFallbackTextureView2D();

	// Set 0: Global
	vk::DescriptorSet globalSet = descManager->allocateFrameSet(DescriptorSetIndex::Global);
	if (globalSet) {
		if (fallbackUBO) {
			descManager->updateUniformBuffer(globalSet, 0, fallbackUBO, 0, fallbackUBOSize);
			descManager->updateUniformBuffer(globalSet, 1, fallbackUBO, 0, fallbackUBOSize);
		}
		if (fallbackSampler && fallbackView) {
			descManager->updateTexture(globalSet, 2, fallbackView, fallbackSampler);
		}
		// Bindings 3-4 are samplerCube — use fallback cubemap view
		vk::ImageView fallbackCubeView = texManager->getFallbackCubeView();
		if (fallbackSampler && fallbackCubeView) {
			descManager->updateTexture(globalSet, 3, fallbackCubeView, fallbackSampler);
			descManager->updateTexture(globalSet, 4, fallbackCubeView, fallbackSampler);
		}
		stateTracker->bindDescriptorSet(DescriptorSetIndex::Global, globalSet);
	}

	// Set 1: Material
	vk::DescriptorSet materialSet = descManager->allocateFrameSet(DescriptorSetIndex::Material);
	if (materialSet) {
		// Binding 0: ModelData UBO (fallback)
		if (fallbackUBO) {
			descManager->updateUniformBuffer(materialSet, 0, fallbackUBO, 0, fallbackUBOSize);
			descManager->updateStorageBuffer(materialSet, 3, fallbackUBO, 0, fallbackUBOSize);
		}

		// Binding 1: decal textures (diffuse, glow, normal as texture array)
		drawManager->bindMaterialTextures(material_info, materialSet);

		// Binding 2: DecalGlobals UBO
		{
			size_t idx = static_cast<size_t>(uniform_block_type::DecalGlobals);
			const auto& binding = drawManager->getPendingUniformBinding(idx);
			if (binding.valid) {
				descManager->updateUniformBuffer(materialSet, 2,
					bufferManager->getVkBuffer(binding.bufferHandle),
					binding.offset, binding.size);
			} else if (fallbackUBO) {
				descManager->updateUniformBuffer(materialSet, 2, fallbackUBO, 0, fallbackUBOSize);
			}
		}

		// Binding 4: scene depth copy (for fragment depth reconstruction)
		{
			vk::Sampler nearestSampler = texManager->getSampler(
				vk::Filter::eNearest, vk::Filter::eNearest,
				vk::SamplerAddressMode::eClampToEdge, false, 0.0f, false);
			vk::ImageView depthView = pp->getSceneDepthCopyView();
			if (depthView && nearestSampler) {
				descManager->updateTexture(materialSet, 4, depthView, nearestSampler);
			} else if (fallbackView2D && fallbackSampler) {
				descManager->updateTexture(materialSet, 4, fallbackView2D, fallbackSampler);
			}
		}

		// Binding 5: scene color (fallback — not used by decals)
		if (fallbackView2D && fallbackSampler) {
			descManager->updateTexture(materialSet, 5, fallbackView2D, fallbackSampler);
		}

		// Binding 6: G-buffer normal copy (for angle rejection)
		{
			vk::Sampler nearestSampler = texManager->getSampler(
				vk::Filter::eNearest, vk::Filter::eNearest,
				vk::SamplerAddressMode::eClampToEdge, false, 0.0f, false);
			vk::ImageView normalView = pp->getGbufNormalCopyView();
			if (normalView && nearestSampler) {
				descManager->updateTexture(materialSet, 6, normalView, nearestSampler);
			} else if (fallbackView2D && fallbackSampler) {
				descManager->updateTexture(materialSet, 6, fallbackView2D, fallbackSampler);
			}
		}

		stateTracker->bindDescriptorSet(DescriptorSetIndex::Material, materialSet);
	}

	// Set 2: PerDraw
	vk::DescriptorSet perDrawSet = descManager->allocateFrameSet(DescriptorSetIndex::PerDraw);
	if (perDrawSet) {
		// Pre-initialize all bindings with fallback
		if (fallbackUBO) {
			descManager->updateUniformBuffer(perDrawSet, 0, fallbackUBO, 0, fallbackUBOSize);
			descManager->updateUniformBuffer(perDrawSet, 1, fallbackUBO, 0, fallbackUBOSize);
			descManager->updateUniformBuffer(perDrawSet, 2, fallbackUBO, 0, fallbackUBOSize);
			descManager->updateUniformBuffer(perDrawSet, 3, fallbackUBO, 0, fallbackUBOSize);
			descManager->updateUniformBuffer(perDrawSet, 4, fallbackUBO, 0, fallbackUBOSize);
		}

		// Binding 1: Matrices UBO
		{
			size_t idx = static_cast<size_t>(uniform_block_type::Matrices);
			const auto& binding = drawManager->getPendingUniformBinding(idx);
			if (binding.valid) {
				descManager->updateUniformBuffer(perDrawSet, 1,
					bufferManager->getVkBuffer(binding.bufferHandle),
					binding.offset, binding.size);
			}
		}

		// Binding 3: DecalInfo UBO
		{
			size_t idx = static_cast<size_t>(uniform_block_type::DecalInfo);
			const auto& binding = drawManager->getPendingUniformBinding(idx);
			if (binding.valid) {
				descManager->updateUniformBuffer(perDrawSet, 3,
					bufferManager->getVkBuffer(binding.bufferHandle),
					binding.offset, binding.size);
			}
		}

		stateTracker->bindDescriptorSet(DescriptorSetIndex::PerDraw, perDrawSet);
	}

	// Bind vertex buffers: binding 0 = box VBO, binding 1 = instance buffer
	vk::Buffer boxVBO = bufferManager->getVkBuffer(buffers.Vbuffer_handle);
	vk::Buffer boxIBO = bufferManager->getVkBuffer(buffers.Ibuffer_handle);
	vk::Buffer instBuf = bufferManager->getVkBuffer(instance_buffer);

	if (!boxVBO || !boxIBO || !instBuf) {
		mprintf(("vulkan_render_decals: Missing buffer(s)!\n"));
		return;
	}

	stateTracker->bindVertexBuffer(0, boxVBO, 0);

	// Instance buffer needs frame base offset for streaming buffers
	size_t instFrameOffset = bufferManager->getFrameBaseOffset(instance_buffer);
	stateTracker->bindVertexBuffer(1, instBuf, static_cast<vk::DeviceSize>(instFrameOffset));

	stateTracker->bindIndexBuffer(boxIBO, 0, vk::IndexType::eUint32);

	// Flush dynamic state and draw
	stateTracker->applyDynamicState();

	auto cmdBuffer = stateTracker->getCommandBuffer();
	cmdBuffer.drawIndexed(
		static_cast<uint32_t>(num_elements),  // index count
		static_cast<uint32_t>(num_instances), // instance count
		0,                                     // first index
		0,                                     // vertex offset
		0                                      // first instance
	);
}

} // namespace vulkan
} // namespace graphics

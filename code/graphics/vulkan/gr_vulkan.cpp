
#include "gr_vulkan.h"
#include "VulkanRenderer.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanShader.h"
#include "VulkanDescriptorManager.h"
#include "VulkanPipeline.h"
#include "VulkanQuery.h"
#include "VulkanState.h"
#include "VulkanDraw.h"

#include "backends/imgui_impl_sdl.h"
#include "backends/imgui_impl_vulkan.h"
#include "osapi/osapi.h"

#include "bmpman/bmpman.h"
#include "cfile/cfile.h"
#include "cmdline/cmdline.h"
#include "graphics/2d.h"
#include "graphics/matrix.h"
#include "graphics/material.h"
#include "graphics/post_processing.h"
#include "graphics/grinternal.h"
#include "graphics/shadows.h"
#include "lighting/lighting.h"
#include "mission/missionparse.h"
#include "nebula/neb.h"
#include "nebula/volumetrics.h"
#include "pngutils/pngutils.h"

namespace graphics {
namespace vulkan {

namespace {

static bool s_vulkanOverrideFog = false;

std::unique_ptr<VulkanRenderer> renderer_instance;

// Sync object for tracking frame completion
struct VulkanSyncObject {
	uint64_t frameNumber;
};

// ========== Renderer-level functions ==========

void vulkan_setup_frame()
{
	auto* renderer = getRendererInstance();
	renderer->setupFrame();
}

void vulkan_flip()
{
	renderer_instance->flip();
}

bool vulkan_is_capable(gr_capability capability)
{
	switch (capability) {
	case gr_capability::CAPABILITY_ENVIRONMENT_MAP:
		return true;
	case gr_capability::CAPABILITY_NORMAL_MAP:
		return Cmdline_normal != 0;
	case gr_capability::CAPABILITY_HEIGHT_MAP:
		return Cmdline_height != 0;
	case gr_capability::CAPABILITY_SOFT_PARTICLES:
		return Gr_post_processing_enabled;
	case gr_capability::CAPABILITY_DISTORTION:
		return Gr_post_processing_enabled;
	case gr_capability::CAPABILITY_POST_PROCESSING:
		return Gr_post_processing_enabled;
	case gr_capability::CAPABILITY_DEFERRED_LIGHTING:
		return light_deferred_enabled();
	case gr_capability::CAPABILITY_SHADOWS:
		return getRendererInstance()->supportsShaderViewportLayerOutput();
	case gr_capability::CAPABILITY_THICK_OUTLINE:
		return false;
	case gr_capability::CAPABILITY_BATCHED_SUBMODELS:
		return true;
	case gr_capability::CAPABILITY_TIMESTAMP_QUERY:
		return getQueryManager() != nullptr;
	case gr_capability::CAPABILITY_SEPARATE_BLEND_FUNCTIONS:
		// Vulkan supports per-attachment blend by spec
		return true;
	case gr_capability::CAPABILITY_PERSISTENT_BUFFER_MAPPING:
		// Vulkan has persistently mappable host-visible memory
		return true;
	case gr_capability::CAPABILITY_BPTC:
		return getRendererInstance()->isTextureCompressionBCSupported();
	case gr_capability::CAPABILITY_LARGE_SHADER:
		// Always true for Vulkan: we use pre-compiled SPIR-V uber-shaders with
		// runtime branching on modelData.flags. The variant approach would require
		// compiling exponentially many SPIR-V permutations. Unbound texture slots
		// are handled via fallback descriptors, so there's no driver issue.
		return true;
	case gr_capability::CAPABILITY_INSTANCED_RENDERING:
		return true;
	case gr_capability::CAPABILITY_QUERIES_REUSABLE:
		// Vulkan queries require explicit reset between read and write.
		// The backend manages this lifecycle internally via deleteQueryObject.
		return false;
	}
	return false;
}

bool vulkan_get_property(gr_property prop, void* dest)
{
	auto* renderer = getRendererInstance();

	switch (prop) {
	case gr_property::UNIFORM_BUFFER_OFFSET_ALIGNMENT:
		*reinterpret_cast<int*>(dest) = static_cast<int>(renderer->getMinUniformBufferOffsetAlignment());
		return true;
	case gr_property::UNIFORM_BUFFER_MAX_SIZE:
		*reinterpret_cast<int*>(dest) = static_cast<int>(renderer->getMaxUniformBufferSize());
		return true;
	case gr_property::MAX_ANISOTROPY:
		*reinterpret_cast<float*>(dest) = renderer->getMaxAnisotropy();
		return true;
	default:
		return false;
	}
}

void vulkan_push_debug_group(const char* name)
{
	auto* renderer = getRendererInstance();
	if (!renderer->isDebugUtilsEnabled()) {
		return;
	}

	auto* stateTracker = getStateTracker();

	vk::DebugUtilsLabelEXT label;
	label.pLabelName = name;
	label.color = {{ 1.0f, 1.0f, 1.0f, 1.0f }};
	stateTracker->getCommandBuffer().beginDebugUtilsLabelEXT(label);
}

void vulkan_pop_debug_group()
{
	auto* renderer = getRendererInstance();
	if (!renderer->isDebugUtilsEnabled()) {
		return;
	}

	auto* stateTracker = getStateTracker();
	stateTracker->getCommandBuffer().endDebugUtilsLabelEXT();
}

void vulkan_imgui_new_frame()
{
	ImGui_ImplVulkan_NewFrame();
}

void vulkan_imgui_render_draw_data()
{
	auto* renderer = getRendererInstance();
	if (renderer) {
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), renderer->getVkCurrentCommandBuffer());
	}
}

gr_sync vulkan_sync_fence()
{
	auto* renderer = getRendererInstance();
	auto* sync = new VulkanSyncObject();
	sync->frameNumber = renderer->getCurrentFrameNumber();
	return static_cast<gr_sync>(sync);
}

bool vulkan_sync_wait(gr_sync sync, uint64_t /*timeoutns*/)
{
	if (!sync) {
		return true;
	}

	auto* renderer = getRendererInstance();
	auto* syncObj = static_cast<VulkanSyncObject*>(sync);

	// Wait on the specific frame's fence (no-op if already complete)
	renderer->waitForFrame(syncObj->frameNumber);
	return true;
}

void vulkan_sync_delete(gr_sync sync)
{
	if (sync) {
		delete static_cast<VulkanSyncObject*>(sync);
	}
}

// ========== Screen capture (save/restore, screenshots) ==========

static ubyte* Vulkan_saved_screen = nullptr;
static int Vulkan_saved_screen_id = -1;

int vulkan_save_screen()
{
	if (Vulkan_saved_screen) {
		// Already have a saved screen
		return -1;
	}

	ubyte* pixels = nullptr;
	uint32_t w, h;
	if (!renderer_instance->readbackFramebuffer(&pixels, &w, &h)) {
		return -1;
	}

	int bmpId = bm_create(32, static_cast<int>(w), static_cast<int>(h), pixels, 0);
	if (bmpId < 0) {
		vm_free(pixels);
		return -1;
	}

	Vulkan_saved_screen = pixels;
	Vulkan_saved_screen_id = bmpId;
	return Vulkan_saved_screen_id;
}

void vulkan_restore_screen(int bmp_id)
{
	gr_reset_clip();

	if (!Vulkan_saved_screen) {
		gr_clear();
		return;
	}

	Assert((bmp_id < 0) || (bmp_id == Vulkan_saved_screen_id));

	if (Vulkan_saved_screen_id < 0) {
		return;
	}

	gr_set_bitmap(Vulkan_saved_screen_id);
	gr_bitmap(0, 0, GR_RESIZE_NONE);
}

void vulkan_free_screen(int bmp_id)
{
	if (!Vulkan_saved_screen) {
		return;
	}

	vm_free(Vulkan_saved_screen);
	Vulkan_saved_screen = nullptr;

	Assert((bmp_id < 0) || (bmp_id == Vulkan_saved_screen_id));

	if (Vulkan_saved_screen_id >= 0) {
		bm_release(Vulkan_saved_screen_id);
		Vulkan_saved_screen_id = -1;
	}
}

// Swizzle BGRA→RGBA in-place for PNG output (swap chain is B8G8R8A8)
static void swizzle_bgra_to_rgba(ubyte* pixels, size_t pixelCount)
{
	for (size_t i = 0; i < pixelCount; i++) {
		size_t off = i * 4;
		std::swap(pixels[off + 0], pixels[off + 2]);
	}
}

void vulkan_print_screen(const char* filename)
{
	ubyte* pixels = nullptr;
	uint32_t w, h;
	if (!renderer_instance->readbackFramebuffer(&pixels, &w, &h)) {
		return;
	}

	swizzle_bgra_to_rgba(pixels, static_cast<size_t>(w) * h);

	char tmp[MAX_PATH_LEN];
	snprintf(tmp, MAX_PATH_LEN - 1, "screenshots/%s.png", filename);

	_mkdir(os_get_config_path("screenshots").c_str());

	if (!png_write_bitmap(os_get_config_path(tmp).c_str(), w, h, false, pixels)) {
		ReleaseWarning(LOCATION, "Failed to write screenshot to \"%s\".", os_get_config_path(tmp).c_str());
	}

	vm_free(pixels);
}

SCP_string vulkan_blob_screen()
{
	ubyte* pixels = nullptr;
	uint32_t w, h;
	if (!renderer_instance->readbackFramebuffer(&pixels, &w, &h)) {
		return "";
	}

	swizzle_bgra_to_rgba(pixels, static_cast<size_t>(w) * h);

	SCP_string result = png_b64_bitmap(w, h, false, pixels);

	vm_free(pixels);

	return "data:image/png;base64," + result;
}

// get_region: intentional no-op. The only caller is neb2_pre_render() in
// NEB2_RENDER_POF mode, which renders a 32x32 background thumbnail into a
// CPU buffer that is never actually read — the pixel data, ex_scale, and
// ey_scale it computes have no consumers. Modern nebula rendering uses
// NEB2_RENDER_HTL (fog color + gr_clear) and doesn't need get_region at all.
void vulkan_get_region(int /*front*/, int /*w*/, int /*h*/, ubyte* /*data*/) {}

void vulkan_post_process_set_effect(const char* name, int value, const vec3d* rgb)
{
	if (!Gr_post_processing_enabled || !graphics::Post_processing_manager) {
		return;
	}
	if (name == nullptr) {
		return;
	}

	auto& ls_params = graphics::Post_processing_manager->getLightshaftParams();
	if (!stricmp("lightshafts", name)) {
		ls_params.intensity = value / 100.0f;
		ls_params.on = !!value;
		return;
	}

	auto& postEffects = graphics::Post_processing_manager->getPostEffects();
	for (size_t idx = 0; idx < postEffects.size(); idx++) {
		if (!stricmp(postEffects[idx].name.c_str(), name)) {
			postEffects[idx].intensity = (value / postEffects[idx].div) + postEffects[idx].add;
			if ((rgb != nullptr) && !(vmd_zero_vector == *rgb)) {
				postEffects[idx].rgb = *rgb;
			}
			break;
		}
	}
}

void vulkan_post_process_set_defaults()
{
	if (!graphics::Post_processing_manager) {
		return;
	}

	auto& postEffects = graphics::Post_processing_manager->getPostEffects();
	for (auto& effect : postEffects) {
		effect.intensity = effect.default_intensity;
	}
}
// ========== Stub functions (not yet implemented) ==========

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

void stub_dump_envmap(const char* /*filename*/) {}

void vulkan_override_fog(bool set_override) {
	s_vulkanOverrideFog = set_override;
}

} // close anonymous namespace temporarily for shadow externs

} // close namespace vulkan
} // close namespace graphics

extern bool Glowpoint_override;
extern bool gr_htl_projection_matrix_set;

namespace graphics {
namespace vulkan {

namespace {

// Saved state for shadow map rendering
static bool Glowpoint_override_save = false;

void vulkan_shadow_map_start(matrix4* shadow_view_matrix, const matrix* light_matrix, vec3d* eye_pos)
{
	if (Shadow_quality == ShadowQuality::Disabled || !getRendererInstance()->supportsShaderViewportLayerOutput()) {
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
std::unique_ptr<os::Viewport> stub_create_viewport(const os::ViewPortProperties& /*props*/)
{
	return std::unique_ptr<os::Viewport>();
}
void stub_use_viewport(os::Viewport* /*view*/) {}
SCP_vector<const char*> stub_openxr_get_extensions() { return {}; }
bool stub_openxr_test_capabilities() { return false; }
bool stub_openxr_create_session() { return false; }
int64_t stub_openxr_get_swapchain_format(const SCP_vector<int64_t>& /*allowed*/) { return 0; }
bool stub_openxr_acquire_swapchain_buffers() { return false; }
bool stub_openxr_flip() { return false; }

// ========== Function pointer table ==========
// Implementations are defined in their respective files:
// VulkanDraw.cpp, VulkanBuffer.cpp, VulkanTexture.cpp, VulkanShader.cpp, VulkanState.cpp

void init_function_pointers()
{
	// function pointers...
	gr_screen.gf_setup_frame = vulkan_setup_frame;
	gr_screen.gf_set_clip = vulkan_set_clip;
	gr_screen.gf_reset_clip = vulkan_reset_clip;

	gr_screen.gf_clear = vulkan_clear;

	gr_screen.gf_print_screen = vulkan_print_screen;
	gr_screen.gf_blob_screen = vulkan_blob_screen;

	gr_screen.gf_zbuffer_get = vulkan_zbuffer_get;
	gr_screen.gf_zbuffer_set = vulkan_zbuffer_set;
	gr_screen.gf_zbuffer_clear = vulkan_zbuffer_clear;

	gr_screen.gf_stencil_set = vulkan_stencil_set;
	gr_screen.gf_stencil_clear = vulkan_stencil_clear;

	gr_screen.gf_alpha_mask_set = vulkan_alpha_mask_set;

	gr_screen.gf_save_screen = vulkan_save_screen;
	gr_screen.gf_restore_screen = vulkan_restore_screen;
	gr_screen.gf_free_screen = vulkan_free_screen;

	gr_screen.gf_get_region = vulkan_get_region;

	// now for the bitmap functions
	gr_screen.gf_bm_free_data = vulkan_bm_free_data;
	gr_screen.gf_bm_create = vulkan_bm_create;
	gr_screen.gf_bm_init = vulkan_bm_init;
	gr_screen.gf_bm_page_in_start = vulkan_bm_page_in_start;
	gr_screen.gf_bm_data = vulkan_bm_data;
	gr_screen.gf_bm_make_render_target = vulkan_bm_make_render_target;
	gr_screen.gf_bm_set_render_target = vulkan_bm_set_render_target;

	gr_screen.gf_set_cull = vulkan_set_cull;
	gr_screen.gf_set_color_buffer = vulkan_set_color_buffer;

	gr_screen.gf_set_clear_color = vulkan_set_clear_color;

	gr_screen.gf_preload = vulkan_preload;

	gr_screen.gf_set_texture_addressing = vulkan_set_texture_addressing;
	gr_screen.gf_zbias = vulkan_zbias;
	gr_screen.gf_set_fill_mode = vulkan_set_fill_mode;

	gr_screen.gf_create_buffer = vulkan_create_buffer;
	gr_screen.gf_delete_buffer = vulkan_delete_buffer;

	gr_screen.gf_update_transform_buffer = vulkan_update_transform_buffer;
	gr_screen.gf_update_buffer_data = vulkan_update_buffer_data;
	gr_screen.gf_update_buffer_data_offset = vulkan_update_buffer_data_offset;
	gr_screen.gf_map_buffer = vulkan_map_buffer;
	gr_screen.gf_flush_mapped_buffer = vulkan_flush_mapped_buffer;

	gr_screen.gf_post_process_set_effect = vulkan_post_process_set_effect;
	gr_screen.gf_post_process_set_defaults = vulkan_post_process_set_defaults;

	gr_screen.gf_post_process_begin = vulkan_post_process_begin;
	gr_screen.gf_post_process_end = vulkan_post_process_end;
	gr_screen.gf_post_process_save_zbuffer = vulkan_post_process_save_zbuffer;
	gr_screen.gf_post_process_restore_zbuffer = vulkan_post_process_restore_zbuffer;

	gr_screen.gf_scene_texture_begin = vulkan_scene_texture_begin;
	gr_screen.gf_scene_texture_end = vulkan_scene_texture_end;
	gr_screen.gf_copy_effect_texture = vulkan_copy_effect_texture;

	gr_screen.gf_deferred_lighting_begin = vulkan_deferred_lighting_begin;
	gr_screen.gf_deferred_lighting_msaa = vulkan_deferred_lighting_msaa;
	gr_screen.gf_deferred_lighting_end = vulkan_deferred_lighting_end;
	gr_screen.gf_deferred_lighting_finish = vulkan_deferred_lighting_finish;

	gr_screen.gf_calculate_irrmap = vulkan_calculate_irrmap;
	gr_screen.gf_dump_envmap = stub_dump_envmap;
	gr_screen.gf_override_fog = vulkan_override_fog;

	gr_screen.gf_imgui_new_frame = vulkan_imgui_new_frame;
	gr_screen.gf_imgui_render_draw_data = vulkan_imgui_render_draw_data;

	gr_screen.gf_set_line_width = vulkan_set_line_width;

	gr_screen.gf_sphere = vulkan_draw_sphere;

	gr_screen.gf_shadow_map_start = vulkan_shadow_map_start;
	gr_screen.gf_shadow_map_end = vulkan_shadow_map_end;

	gr_screen.gf_start_decal_pass = vulkan_start_decal_pass;
	gr_screen.gf_stop_decal_pass = vulkan_stop_decal_pass;
	gr_screen.gf_render_decals = vulkan_render_decals;

	gr_screen.gf_render_shield_impact = vulkan_render_shield_impact;

	gr_screen.gf_maybe_create_shader = vulkan_maybe_create_shader;
	gr_screen.gf_recompile_all_shaders = vulkan_recompile_all_shaders;

	gr_screen.gf_clear_states = vulkan_clear_states;

	gr_screen.gf_update_texture = vulkan_update_texture;
	gr_screen.gf_get_bitmap_from_texture = vulkan_get_bitmap_from_texture;

	gr_screen.gf_render_model = vulkan_render_model;
	gr_screen.gf_render_primitives = vulkan_render_primitives;
	gr_screen.gf_render_primitives_particle = vulkan_render_primitives_particle;
	gr_screen.gf_render_primitives_distortion = vulkan_render_primitives_distortion;
	gr_screen.gf_render_movie = vulkan_render_movie;
	gr_screen.gf_render_nanovg = vulkan_render_nanovg;
	gr_screen.gf_render_primitives_batched = vulkan_render_primitives_batched;
	gr_screen.gf_render_rocket_primitives = vulkan_render_rocket_primitives;

	gr_screen.gf_is_capable = vulkan_is_capable;
	gr_screen.gf_get_property = vulkan_get_property;

	gr_screen.gf_push_debug_group = vulkan_push_debug_group;
	gr_screen.gf_pop_debug_group = vulkan_pop_debug_group;

	gr_screen.gf_create_query_object = vulkan_create_query_object;
	gr_screen.gf_query_value = vulkan_query_value;
	gr_screen.gf_query_value_available = vulkan_query_value_available;
	gr_screen.gf_get_query_value = vulkan_get_query_value;
	gr_screen.gf_delete_query_object = vulkan_delete_query_object;

	gr_screen.gf_create_viewport = stub_create_viewport;
	gr_screen.gf_use_viewport = stub_use_viewport;

	gr_screen.gf_bind_uniform_buffer = vulkan_bind_uniform_buffer;

	gr_screen.gf_sync_fence = vulkan_sync_fence;
	gr_screen.gf_sync_wait = vulkan_sync_wait;
	gr_screen.gf_sync_delete = vulkan_sync_delete;

	gr_screen.gf_set_viewport = vulkan_set_viewport;

	gr_screen.gf_openxr_get_extensions = stub_openxr_get_extensions;
	gr_screen.gf_openxr_test_capabilities = stub_openxr_test_capabilities;
	gr_screen.gf_openxr_create_session = stub_openxr_create_session;
	gr_screen.gf_openxr_get_swapchain_format = stub_openxr_get_swapchain_format;
	gr_screen.gf_openxr_acquire_swapchain_buffers = stub_openxr_acquire_swapchain_buffers;
	gr_screen.gf_openxr_flip = stub_openxr_flip;
}

} // anonymous namespace

void initialize_function_pointers() {
	init_function_pointers();
}

bool initialize(std::unique_ptr<os::GraphicsOperations>&& graphicsOps)
{
	renderer_instance.reset(new VulkanRenderer(std::move(graphicsOps)));
	if (!renderer_instance->initialize()) {
		return false;
	}

	// Initialize ImGui SDL2 backend for input handling.
	// The Vulkan rendering backend (ImGui_ImplVulkan) is initialized
	// inside VulkanRenderer::initImGui() after all Vulkan objects are ready.
	SDL_Window* window = os::getSDLMainWindow();
	if (window) {
		ImGui_ImplSDL2_InitForVulkan(window);
	}

	gr_screen.gf_flip = vulkan_flip;

	// Initialize matrices and viewport (matching OpenGL backend initialization)
	gr_reset_matrices();
	gr_setup_viewport();

	// Start first frame so a command buffer is active before the first draw calls.
	// The engine draws the title screen during game_init(), before the main loop's
	// first gr_flip() → setupFrame(). Without this, any gr_clear/gr_bitmap before
	// the first flip would hit a null command buffer. Matches OpenGL init behavior.
	gr_setup_frame();

	mprintf(("Vulkan: Initialization complete\n"));
	return true;
}

VulkanRenderer* getRendererInstance()
{
	return renderer_instance.get();
}

void cleanup()
{
	renderer_instance->shutdown();
	renderer_instance = nullptr;
}

} // namespace vulkan
} // namespace graphics

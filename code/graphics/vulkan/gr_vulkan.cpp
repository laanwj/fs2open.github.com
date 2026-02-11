
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
#include "lighting/lighting.h"
#include "pngutils/pngutils.h"

namespace graphics {
namespace vulkan {

namespace {

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
		// Cubemap rendering is not fully implemented yet (faces aren't uploaded,
		// env slot gets a fallback 2D texture), but we return true because:
		// 1. It degrades gracefully (no reflections, not a crash)
		// 2. Mods can declare this as a required capability — returning false
		//    would block them from loading even though the game runs fine
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
	case gr_capability::CAPABILITY_THICK_OUTLINE:
		// Requires geometry shaders / shadow map pipeline (not yet implemented)
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
		// Gates the decal system which requires render_decals (not yet implemented)
		return false;
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

	// Clear color attachment (0) to black inside the render pass
	// (emissive was just copied, so don't clear it; other G-buffer attachments
	// were already cleared by the eClear pass at beginSceneRendering)
	{
		vk::ClearAttachment clearAtt;
		clearAtt.aspectMask = vk::ImageAspectFlagBits::eColor;
		clearAtt.colorAttachment = 0;
		clearAtt.clearValue.color.setFloat32({0.0f, 0.0f, 0.0f, 0.0f});

		auto extent = pp->getSceneExtent();
		vk::ClearRect clearRect;
		clearRect.rect.offset = vk::Offset2D(0, 0);
		clearRect.rect.extent = extent;
		clearRect.baseArrayLayer = 0;
		clearRect.layerCount = 1;
		cmd.clearAttachments(clearAtt, clearRect);

		// Optionally clear non-color G-buffer attachments (position, normal, specular, composite)
		if (clearNonColorBufs) {
			for (uint32_t att : {1u, 2u, 3u, 5u}) {
				clearAtt.colorAttachment = att;
				cmd.clearAttachments(clearAtt, clearRect);
			}
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

	// Stub implementation for phase 7a: copy emissive → color.
	// Full light accumulation pass will be implemented in phase 7b.
	// This makes the scene appear as emissive-only (no dynamic lights).

	// End the G-buffer render pass
	cmd.endRenderPass();

	// Transition emissive → eTransferSrc, color → eTransferDst
	{
		std::array<vk::ImageMemoryBarrier, 2> barriers;

		barriers[0].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		barriers[0].dstAccessMask = vk::AccessFlagBits::eTransferRead;
		barriers[0].oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		barriers[0].newLayout = vk::ImageLayout::eTransferSrcOptimal;
		barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].image = pp->getGbufEmissiveImage();
		barriers[0].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

		barriers[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
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
	}

	// Copy emissive → color
	{
		auto extent = pp->getSceneExtent();
		vk::ImageCopy region;
		region.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
		region.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
		region.extent = vk::Extent3D(extent.width, extent.height, 1);
		cmd.copyImage(
			pp->getGbufEmissiveImage(), vk::ImageLayout::eTransferSrcOptimal,
			pp->getSceneColorImage(), vk::ImageLayout::eTransferDstOptimal,
			region);
	}

	// Transition color → eColorAttachmentOptimal for resumed render pass.
	// Transition emissive → eShaderReadOnlyOptimal (where transitionGbufForResume expects it).
	{
		std::array<vk::ImageMemoryBarrier, 2> barriers;

		barriers[0].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
		barriers[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
		barriers[0].oldLayout = vk::ImageLayout::eTransferDstOptimal;
		barriers[0].newLayout = vk::ImageLayout::eColorAttachmentOptimal;
		barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barriers[0].image = pp->getSceneColorImage();
		barriers[0].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

		barriers[1].srcAccessMask = vk::AccessFlagBits::eTransferRead;
		barriers[1].dstAccessMask = {};
		barriers[1].oldLayout = vk::ImageLayout::eTransferSrcOptimal;
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
}
void stub_calculate_irrmap() {}
void stub_dump_envmap(const char* /*filename*/) {}
void stub_override_fog(bool /*set_override*/) {}
void stub_shadow_map_start(matrix4* /*shadow_view_matrix*/, const matrix* /*light_matrix*/, vec3d* /*eye_pos*/) {}
void stub_shadow_map_end() {}
void stub_start_decal_pass() {}
void stub_stop_decal_pass() {}
void stub_render_decals(decal_material* /*material_info*/,
                       primitive_type /*prim_type*/,
                       vertex_layout* /*layout*/,
                       int /*num_elements*/,
                       const indexed_vertex_source& /*buffers*/,
                       const gr_buffer_handle& /*instance_buffer*/,
                       int /*num_instances*/) {}
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

	gr_screen.gf_calculate_irrmap = stub_calculate_irrmap;
	gr_screen.gf_dump_envmap = stub_dump_envmap;
	gr_screen.gf_override_fog = stub_override_fog;

	gr_screen.gf_imgui_new_frame = vulkan_imgui_new_frame;
	gr_screen.gf_imgui_render_draw_data = vulkan_imgui_render_draw_data;

	gr_screen.gf_set_line_width = vulkan_set_line_width;

	gr_screen.gf_sphere = vulkan_draw_sphere;

	gr_screen.gf_shadow_map_start = stub_shadow_map_start;
	gr_screen.gf_shadow_map_end = stub_shadow_map_end;

	gr_screen.gf_start_decal_pass = stub_start_decal_pass;
	gr_screen.gf_stop_decal_pass = stub_stop_decal_pass;
	gr_screen.gf_render_decals = stub_render_decals;

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

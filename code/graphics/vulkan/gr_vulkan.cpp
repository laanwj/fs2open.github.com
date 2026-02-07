
#include "gr_vulkan.h"
#include "VulkanRenderer.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanShader.h"
#include "VulkanDescriptorManager.h"
#include "VulkanPipeline.h"
#include "VulkanState.h"
#include "VulkanDraw.h"

#include "backends/imgui_impl_sdl.h"
#include "backends/imgui_impl_vulkan.h"
#include "mod_table/mod_table.h"
#include "osapi/osapi.h"

#include "graphics/2d.h"
#include "graphics/matrix.h"
#include "graphics/material.h"
#include "graphics/util/uniform_structs.h"
#include "graphics/util/UniformBuffer.h"
#include "graphics/shaders/compiled/default-material_structs.vert.h"

#define BMPMAN_INTERNAL
#include "bmpman/bm_internal.h"

// GL_alpha_threshold is defined in gropengl.cpp, need extern here
extern float GL_alpha_threshold;

// PostProcessing_override is defined in globalincs/systemvars.cpp
extern bool PostProcessing_override;

namespace graphics {
namespace vulkan {

namespace {

std::unique_ptr<VulkanRenderer> renderer_instance;

// Sync object for tracking frame completion
struct VulkanSyncObject {
	uint64_t frameNumber;
};

// Helper to set up GenericData uniform for default material shader
// Similar to opengl_shader_set_default_material() in gropenglshader.cpp
void vulkan_set_default_material_uniforms(material* material_info)
{
	if (!material_info) {
		return;
	}

	// Get uniform buffer for GenericData
	auto buffer = gr_get_uniform_buffer(uniform_block_type::GenericData, 1, sizeof(genericData_default_material_vert));
	auto* data = buffer.aligner().addTypedElement<genericData_default_material_vert>();

	// Get base map from material
	int base_map = material_info->get_texture_map(TM_BASE_TYPE);
	bool textured = (base_map >= 0);
	bool alpha = (material_info->get_texture_type() == TCACHE_TYPE_AABITMAP);

	// Texturing flags
	if (textured) {
		data->noTexturing = 0;
		data->baseMapIndex = 0;  // Array index in texture array
	} else {
		data->noTexturing = 1;
		data->baseMapIndex = 0;
	}

	// Alpha texture flag
	data->alphaTexture = alpha ? 1 : 0;

	// HDR / intensity settings
	if (High_dynamic_range) {
		data->srgb = 1;
		data->intensity = material_info->get_color_scale();
	} else {
		data->srgb = 0;
		data->intensity = 1.0f;
	}

	// Alpha threshold
	data->alphaThreshold = GL_alpha_threshold;

	// Color from material
	vec4 clr = material_info->get_color();
	data->color.a1d[0] = clr.xyzw.x;
	data->color.a1d[1] = clr.xyzw.y;
	data->color.a1d[2] = clr.xyzw.z;
	data->color.a1d[3] = clr.xyzw.w;

	static int debugCount = 0;
	if (debugCount < 5) {
		mprintf(("Vulkan GenericData: noTex=%d, alphaTex=%d, srgb=%d, intensity=%.2f, color=(%.2f,%.2f,%.2f,%.2f), alphaThresh=%.2f\n",
			data->noTexturing, data->alphaTexture, data->srgb, data->intensity,
			clr.xyzw.x, clr.xyzw.y, clr.xyzw.z, clr.xyzw.w, data->alphaThreshold));
		debugCount++;
	}

	// Clip plane
	const auto& clip_plane = material_info->get_clip_plane();
	if (clip_plane.enabled) {
		data->clipEnabled = 1;

		data->clipEquation.a1d[0] = clip_plane.normal.xyz.x;
		data->clipEquation.a1d[1] = clip_plane.normal.xyz.y;
		data->clipEquation.a1d[2] = clip_plane.normal.xyz.z;
		// Calculate 'd' value: d = -dot(normal, position)
		data->clipEquation.a1d[3] = -(clip_plane.normal.xyz.x * clip_plane.position.xyz.x +
		                              clip_plane.normal.xyz.y * clip_plane.position.xyz.y +
		                              clip_plane.normal.xyz.z * clip_plane.position.xyz.z);

		// Model matrix (identity for now, material doesn't provide one)
		vm_matrix4_set_identity(&data->modelMatrix);
	} else {
		data->clipEnabled = 0;
		vm_matrix4_set_identity(&data->modelMatrix);
		data->clipEquation.a1d[0] = 0.0f;
		data->clipEquation.a1d[1] = 0.0f;
		data->clipEquation.a1d[2] = 0.0f;
		data->clipEquation.a1d[3] = 0.0f;
	}

	buffer.submitData();
	gr_bind_uniform_buffer(uniform_block_type::GenericData, buffer.getBufferOffset(0),
	                       sizeof(genericData_default_material_vert), buffer.bufferHandle());
}

gr_buffer_handle vulkan_create_buffer(BufferType type, BufferUsageHint usage)
{
	auto* bufferManager = getBufferManager();
	return bufferManager->createBuffer(type, usage);
}

void vulkan_setup_frame()
{
	auto* renderer = getRendererInstance();
	renderer->setupFrame();
}

void vulkan_delete_buffer(gr_buffer_handle handle)
{
	auto* bufferManager = getBufferManager();
	bufferManager->deleteBuffer(handle);
}

int vulkan_preload(int bitmap_num, int /*is_aabitmap*/)
{
	auto* texManager = getTextureManager();

	// Check if texture is already loaded
	auto* slot = texManager->getTextureSlot(bitmap_num);
	if (slot && slot->imageView) {
		return 1;  // Already loaded
	}

	// Lock bitmap to get data pointer - use 32bpp for best compatibility
	bitmap* bmp = bm_lock(bitmap_num, 32, BMP_TEX_XPARENT);
	if (!bmp) {
		static int warnCount = 0;
		if (warnCount < 10) {
			mprintf(("vulkan_preload: Failed to lock bitmap %d\n", bitmap_num));
			warnCount++;
		}
		return 0;
	}

	// Upload the texture
	bool success = texManager->bm_data(bitmap_num, bmp);

	// Unlock bitmap
	bm_unlock(bitmap_num);

	if (success) {
		static int successCount = 0;
		if (successCount < 10) {
			mprintf(("vulkan_preload: Successfully uploaded texture %d\n", bitmap_num));
			successCount++;
		}
	}

	return success ? 1 : 0;
}

int stub_save_screen() { return 1; }

int vulkan_zbuffer_get()
{
	auto* drawManager = getDrawManager();
	return drawManager->zbufferGet();
}

int vulkan_zbuffer_set(int mode)
{
	auto* drawManager = getDrawManager();
	return drawManager->zbufferSet(mode);
}

void vulkan_set_fill_mode(int mode)
{
	auto* drawManager = getDrawManager();
	// GR_FILL_MODE_WIRE = 1, GR_FILL_MODE_SOLID = 2
	drawManager->setFillMode(mode);
}

void vulkan_clear()
{
	auto* drawManager = getDrawManager();
	drawManager->clear();
}

void stub_free_screen(int /*id*/) {}

void stub_get_region(int /*front*/, int /*w*/, int /*h*/, ubyte* /*data*/) {}

void stub_print_screen(const char* /*filename*/) {}

SCP_string stub_blob_screen() { return ""; }

void vulkan_reset_clip()
{
	auto* drawManager = getDrawManager();
	drawManager->resetClip();
}

void stub_restore_screen(int /*id*/) {}

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

void stub_update_transform_buffer(void* /*data*/, size_t /*size*/) {}

void vulkan_set_clear_color(int r, int g, int b)
{
	auto* drawManager = getDrawManager();
	drawManager->setClearColor(r, g, b);
}

void vulkan_set_clip(int x, int y, int w, int h, int resize_mode)
{
	auto* drawManager = getDrawManager();
	drawManager->setClip(x, y, w, h, resize_mode);
}

int vulkan_set_cull(int cull)
{
	auto* drawManager = getDrawManager();
	return drawManager->setCull(cull);
}

int vulkan_set_color_buffer(int mode)
{
	auto* drawManager = getDrawManager();
	return drawManager->setColorBuffer(mode);
}

void vulkan_set_texture_addressing(int mode)
{
	auto* drawManager = getDrawManager();
	drawManager->setTextureAddressing(mode);
}

void vulkan_zbias(int bias)
{
	auto* stateTracker = getStateTracker();
	auto* drawManager = getDrawManager();

	if (bias) {
		drawManager->setDepthBiasEnabled(true);
		if (bias < 0) {
			stateTracker->setDepthBias(1.0f, static_cast<float>(-bias));
		} else {
			stateTracker->setDepthBias(0.0f, static_cast<float>(-bias));
		}
	} else {
		drawManager->setDepthBiasEnabled(false);
		stateTracker->setDepthBias(0.0f, 0.0f);
	}
}

void vulkan_zbuffer_clear(int mode)
{
	auto* drawManager = getDrawManager();
	drawManager->zbufferClear(mode);
}

int vulkan_stencil_set(int mode)
{
	auto* drawManager = getDrawManager();
	return drawManager->stencilSet(mode);
}

void vulkan_stencil_clear()
{
	auto* drawManager = getDrawManager();
	drawManager->stencilClear();
}

int vulkan_alpha_mask_set(int mode, float alpha)
{
	if (mode) {
		GL_alpha_threshold = alpha;
	} else {
		GL_alpha_threshold = 0.0f;
	}
	return mode;
}

void stub_post_process_set_effect(const char* /*name*/, int /*x*/, const vec3d* /*rgb*/) {}

void stub_post_process_set_defaults() {}

void stub_post_process_save_zbuffer() {}

void stub_post_process_begin() {}

void stub_post_process_end() {}

void vulkan_scene_texture_begin()
{
	// Minimal implementation matching OpenGL's gr_opengl_scene_texture_begin():
	// 1. Clear color + depth for the 3D scene
	// 2. Set High_dynamic_range flag if post-processing is enabled
	//
	// Full implementation would switch to an offscreen FBO, but for now
	// we render directly to the swap chain.

	auto* stateTracker = getStateTracker();

	if (stateTracker->hasCommandBuffer()) {
		// Clear color buffer to black (matching OpenGL behavior)
		auto cmdBuffer = stateTracker->getCommandBuffer();

		vk::ClearAttachment clearAttachments[2];
		clearAttachments[0].aspectMask = vk::ImageAspectFlagBits::eColor;
		clearAttachments[0].colorAttachment = 0;
		clearAttachments[0].clearValue.color.setFloat32({0.0f, 0.0f, 0.0f, 1.0f});

		clearAttachments[1].aspectMask = vk::ImageAspectFlagBits::eDepth;
		clearAttachments[1].clearValue.depthStencil = vk::ClearDepthStencilValue(1.0f, 0);

		vk::ClearRect clearRect;
		clearRect.rect.offset = vk::Offset2D(0, 0);
		clearRect.rect.extent = vk::Extent2D(static_cast<uint32_t>(gr_screen.max_w),
		                                      static_cast<uint32_t>(gr_screen.max_h));
		clearRect.baseArrayLayer = 0;
		clearRect.layerCount = 1;

		cmdBuffer.clearAttachments(2, clearAttachments, 1, &clearRect);
	}

	// Enable HDR for 3D scene rendering (affects intensity/srgb in shaders)
	if (Gr_post_processing_enabled && !PostProcessing_override) {
		High_dynamic_range = true;
	}
}

void vulkan_scene_texture_end()
{
	// Minimal implementation matching OpenGL's gr_opengl_scene_texture_end():
	// Reset HDR flag after 3D scene rendering
	//
	// Full implementation would composite the scene texture to screen with
	// post-processing (bloom, FXAA, tonemapping). For now we just reset the flag.

	High_dynamic_range = false;
}

void stub_copy_effect_texture() {}

void stub_deferred_lighting_begin(bool /*clearNonColorBufs*/) {}

void stub_deferred_lighting_msaa() {}

void stub_deferred_lighting_end() {}

void stub_deferred_lighting_finish() {}

void vulkan_set_line_width(float width)
{
	auto* stateTracker = getStateTracker();
	if (width <= 1.0f) {
		stateTracker->setLineWidth(width);
	}
	gr_screen.line_width = width;
}

void stub_draw_sphere(material* /*material_def*/, float /*rad*/) {}

void vulkan_clear_states()
{
	auto* drawManager = getDrawManager();
	drawManager->clearStates();
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

void stub_bm_page_in_start() {}

bool vulkan_bm_data(int handle, bitmap* bm)
{
	auto* texManager = getTextureManager();
	return texManager->bm_data(handle, bm);
}

int vulkan_maybe_create_shader(shader_type shader_t, unsigned int flags)
{
	auto* shaderManager = getShaderManager();
	return shaderManager->maybeCreateShader(shader_t, flags);
}

void vulkan_recompile_all_shaders(const std::function<void(size_t, size_t)>& progressCallback)
{
	auto* shaderManager = getShaderManager();
	shaderManager->recompileAllShaders(progressCallback);
}

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

void stub_render_shield_impact(shield_material* /*material_info*/,
	primitive_type /*prim_type*/,
	vertex_layout* /*layout*/,
	gr_buffer_handle /*buffer_handle*/,
	int /*n_verts*/)
{
}

void vulkan_render_model(model_material* material_info,
	indexed_vertex_source* vert_source,
	vertex_buffer* bufferp,
	size_t texi)
{
	// ModelData UBO (matrices, lights, material params) is already bound by the model
	// rendering pipeline (model_draw_list::render_buffer) before this function is called.
	// Do NOT call vulkan_set_default_material_uniforms here - that would set GenericData
	// uniforms for SDR_TYPE_DEFAULT_MATERIAL, but models use SDR_TYPE_MODEL with ModelData.

	auto* drawManager = getDrawManager();
	drawManager->renderModel(material_info, vert_source, bufferp, texi);
}

void vulkan_render_primitives(material* material_info,
	primitive_type prim_type,
	vertex_layout* layout,
	int offset,
	int n_verts,
	gr_buffer_handle buffer_handle,
	size_t buffer_offset)
{
	// Set up uniform buffers before rendering (like OpenGL does)
	gr_matrix_set_uniforms();
	vulkan_set_default_material_uniforms(material_info);

	auto* drawManager = getDrawManager();
	drawManager->renderPrimitives(material_info, prim_type, layout, offset, n_verts, buffer_handle, buffer_offset);
}

void vulkan_render_primitives_particle(particle_material* material_info,
	primitive_type prim_type,
	vertex_layout* layout,
	int offset,
	int n_verts,
	gr_buffer_handle buffer_handle)
{
	gr_matrix_set_uniforms();
	vulkan_set_default_material_uniforms(material_info);

	auto* drawManager = getDrawManager();
	drawManager->renderPrimitivesParticle(material_info, prim_type, layout, offset, n_verts, buffer_handle);
}

void vulkan_render_primitives_distortion(distortion_material* material_info,
	primitive_type prim_type,
	vertex_layout* layout,
	int offset,
	int n_verts,
	gr_buffer_handle buffer_handle)
{
	gr_matrix_set_uniforms();
	vulkan_set_default_material_uniforms(material_info);

	auto* drawManager = getDrawManager();
	drawManager->renderPrimitivesDistortion(material_info, prim_type, layout, n_verts, buffer_handle);
}
void vulkan_render_movie(movie_material* material_info,
	primitive_type prim_type,
	vertex_layout* layout,
	int n_verts,
	gr_buffer_handle buffer,
	size_t buffer_offset)
{
	gr_matrix_set_uniforms();
	vulkan_set_default_material_uniforms(material_info);

	auto* drawManager = getDrawManager();
	drawManager->renderMovie(material_info, prim_type, layout, n_verts, buffer);
}

void vulkan_render_nanovg(nanovg_material* material_info,
	primitive_type prim_type,
	vertex_layout* layout,
	int offset,
	int n_verts,
	gr_buffer_handle buffer_handle)
{
	// NanoVG shader reads from NanoVGData UBO (set 2 binding 2), not GenericData.
	// The NanoVGRenderer binds NanoVGData before calling gr_render_nanovg().

	// NanoVG uses its own software scissor (scissorMat/scissorExt in the fragment shader).
	// Disable hardware scissor to match nanovg_gl.h which calls glDisable(GL_SCISSOR_TEST).
	// Without this, NanoVG draws get clipped by gr_set_clip's hardware scissor.
	auto* stateTracker = getStateTracker();
	bool savedScissorEnabled = stateTracker->isScissorEnabled();
	stateTracker->setScissorEnabled(false);

	auto* drawManager = getDrawManager();
	drawManager->renderNanoVG(material_info, prim_type, layout, offset, n_verts, buffer_handle);

	// Restore scissor state
	stateTracker->setScissorEnabled(savedScissorEnabled);
}

void vulkan_render_primitives_batched(batched_bitmap_material* material_info,
	primitive_type prim_type,
	vertex_layout* layout,
	int offset,
	int n_verts,
	gr_buffer_handle buffer_handle)
{
	gr_matrix_set_uniforms();
	vulkan_set_default_material_uniforms(material_info);

	auto* drawManager = getDrawManager();
	drawManager->renderPrimitivesBatched(material_info, prim_type, layout, offset, n_verts, buffer_handle);
}

void vulkan_render_rocket_primitives(interface_material* material_info,
	primitive_type prim_type,
	vertex_layout* layout,
	int n_indices,
	gr_buffer_handle vertex_buffer,
	gr_buffer_handle index_buffer)
{
	gr_matrix_set_uniforms();
	vulkan_set_default_material_uniforms(material_info);

	auto* drawManager = getDrawManager();
	drawManager->renderRocketPrimitives(material_info, prim_type, layout, n_indices, vertex_buffer, index_buffer);
}

bool stub_is_capable(gr_capability /*capability*/) { return false; }
bool stub_get_property(gr_property p, void* dest)
{
	if (p == gr_property::UNIFORM_BUFFER_OFFSET_ALIGNMENT) {
		// Query actual alignment from Vulkan physical device
		auto* renderer = getRendererInstance();
		*reinterpret_cast<int*>(dest) = static_cast<int>(renderer->getMinUniformBufferOffsetAlignment());
		return true;
	}
	return false;
};

void vulkan_push_debug_group(const char* name)
{
	auto* renderer = getRendererInstance();
	if (!renderer->isDebugUtilsEnabled()) {
		return;
	}

	auto* stateTracker = getStateTracker();
	if (!stateTracker->hasCommandBuffer()) {
		return;
	}

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
	if (!stateTracker->hasCommandBuffer()) {
		return;
	}

	stateTracker->getCommandBuffer().endDebugUtilsLabelEXT();
}

int stub_create_query_object() { return -1; }

void stub_query_value(int /*obj*/, QueryType /*type*/) {}

bool stub_query_value_available(int /*obj*/) { return false; }

std::uint64_t stub_get_query_value(int /*obj*/) { return 0; }

void stub_delete_query_object(int /*obj*/) {}

SCP_vector<const char*> stub_openxr_get_extensions() { return {}; }

bool stub_openxr_test_capabilities() { return false; }

bool stub_openxr_create_session() { return false; }

int64_t stub_openxr_get_swapchain_format(const SCP_vector<int64_t>& /*allowed*/) { return 0; }

bool stub_openxr_acquire_swapchain_buffers() { return false; }

bool stub_openxr_flip() { return false; }

void* vulkan_map_buffer(gr_buffer_handle handle)
{
	auto* bufferManager = getBufferManager();
	return bufferManager->mapBuffer(handle);
}

void vulkan_flush_mapped_buffer(gr_buffer_handle handle, size_t offset, size_t size)
{
	auto* bufferManager = getBufferManager();
	bufferManager->flushMappedBuffer(handle, offset, size);
}

void stub_post_process_restore_zbuffer() {}

void stub_calculate_irrmap() {}

void stub_dump_envmap(const char* /*filename*/) {}

void stub_override_fog(bool /*set_override*/) {}

std::unique_ptr<os::Viewport> stub_create_viewport(const os::ViewPortProperties& /*props*/)
{
	return std::unique_ptr<os::Viewport>();
}

void stub_use_viewport(os::Viewport* /*view*/) {}

void vulkan_bind_uniform_buffer(uniform_block_type blockType, size_t offset, size_t size, gr_buffer_handle buffer)
{
	auto* bufferManager = getBufferManager();
	bufferManager->bindUniformBuffer(blockType, offset, size, buffer);
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

void vulkan_set_viewport(int x, int y, int width, int height)
{
	auto* stateTracker = graphics::vulkan::getStateTracker();
	if (gr_screen.rendering_to_texture == -1) {
		// Screen rendering: use negative viewport height for OpenGL-compatible Y-up NDC
		// (VK_KHR_maintenance1, core since Vulkan 1.1)
		stateTracker->setViewport(
			static_cast<float>(x),
			static_cast<float>(gr_screen.max_h - y),
			static_cast<float>(width),
			static_cast<float>(-height));
	} else {
		// RTT: standard positive viewport (RTT projection matrix handles Y-flip)
		stateTracker->setViewport(
			static_cast<float>(x), static_cast<float>(y),
			static_cast<float>(width), static_cast<float>(height));
	}
}

void vulkan_flip()
{
	renderer_instance->flip();
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

void init_function_pointers()
{
	// function pointers...
	gr_screen.gf_setup_frame = vulkan_setup_frame;
	gr_screen.gf_set_clip = vulkan_set_clip;
	gr_screen.gf_reset_clip = vulkan_reset_clip;

	gr_screen.gf_clear = vulkan_clear;

	gr_screen.gf_print_screen = stub_print_screen;
	gr_screen.gf_blob_screen = stub_blob_screen;

	gr_screen.gf_zbuffer_get = vulkan_zbuffer_get;
	gr_screen.gf_zbuffer_set = vulkan_zbuffer_set;
	gr_screen.gf_zbuffer_clear = vulkan_zbuffer_clear;

	gr_screen.gf_stencil_set = vulkan_stencil_set;
	gr_screen.gf_stencil_clear = vulkan_stencil_clear;

	gr_screen.gf_alpha_mask_set = vulkan_alpha_mask_set;

	gr_screen.gf_save_screen = stub_save_screen;
	gr_screen.gf_restore_screen = stub_restore_screen;
	gr_screen.gf_free_screen = stub_free_screen;

	gr_screen.gf_get_region = stub_get_region;

	// now for the bitmap functions
	gr_screen.gf_bm_free_data = vulkan_bm_free_data;
	gr_screen.gf_bm_create = vulkan_bm_create;
	gr_screen.gf_bm_init = vulkan_bm_init;
	gr_screen.gf_bm_page_in_start = stub_bm_page_in_start;
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

	gr_screen.gf_update_transform_buffer = stub_update_transform_buffer;
	gr_screen.gf_update_buffer_data = vulkan_update_buffer_data;
	gr_screen.gf_update_buffer_data_offset = vulkan_update_buffer_data_offset;
	gr_screen.gf_map_buffer = vulkan_map_buffer;
	gr_screen.gf_flush_mapped_buffer = vulkan_flush_mapped_buffer;

	gr_screen.gf_post_process_set_effect = stub_post_process_set_effect;
	gr_screen.gf_post_process_set_defaults = stub_post_process_set_defaults;

	gr_screen.gf_post_process_begin = stub_post_process_begin;
	gr_screen.gf_post_process_end = stub_post_process_end;
	gr_screen.gf_post_process_save_zbuffer = stub_post_process_save_zbuffer;
	gr_screen.gf_post_process_restore_zbuffer = stub_post_process_restore_zbuffer;

	gr_screen.gf_scene_texture_begin = vulkan_scene_texture_begin;
	gr_screen.gf_scene_texture_end = vulkan_scene_texture_end;
	gr_screen.gf_copy_effect_texture = stub_copy_effect_texture;

	gr_screen.gf_deferred_lighting_begin = stub_deferred_lighting_begin;
	gr_screen.gf_deferred_lighting_msaa = stub_deferred_lighting_msaa;
	gr_screen.gf_deferred_lighting_end = stub_deferred_lighting_end;
	gr_screen.gf_deferred_lighting_finish = stub_deferred_lighting_finish;

	gr_screen.gf_calculate_irrmap = stub_calculate_irrmap;
	gr_screen.gf_dump_envmap = stub_dump_envmap;
	gr_screen.gf_override_fog = stub_override_fog;

	gr_screen.gf_imgui_new_frame = vulkan_imgui_new_frame;
	gr_screen.gf_imgui_render_draw_data = vulkan_imgui_render_draw_data;

	gr_screen.gf_set_line_width = vulkan_set_line_width;

	gr_screen.gf_sphere = stub_draw_sphere;

	gr_screen.gf_shadow_map_start = stub_shadow_map_start;
	gr_screen.gf_shadow_map_end = stub_shadow_map_end;

	gr_screen.gf_start_decal_pass = stub_start_decal_pass;
	gr_screen.gf_stop_decal_pass = stub_stop_decal_pass;
	gr_screen.gf_render_decals = stub_render_decals;

	gr_screen.gf_render_shield_impact = stub_render_shield_impact;

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

	gr_screen.gf_is_capable = stub_is_capable;
	gr_screen.gf_get_property = stub_get_property;

	gr_screen.gf_push_debug_group = vulkan_push_debug_group;
	gr_screen.gf_pop_debug_group = vulkan_pop_debug_group;

	gr_screen.gf_create_query_object = stub_create_query_object;
	gr_screen.gf_query_value = stub_query_value;
	gr_screen.gf_query_value_available = stub_query_value_available;
	gr_screen.gf_get_query_value = stub_get_query_value;
	gr_screen.gf_delete_query_object = stub_delete_query_object;

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

	// Nothing else is finished so always fail here
	mprintf(("Vulkan support is not finished yet so graphics initialization will always fail...\n"));
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


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
#include "osapi/osapi.h"

#include "bmpman/bmpman.h"
#include "cmdline/cmdline.h"
#include "graphics/2d.h"
#include "graphics/matrix.h"
#include "graphics/material.h"

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
	case gr_capability::CAPABILITY_DISTORTION:
		// Requires post-processing / scene texture pipeline (not yet implemented)
		return false;
	case gr_capability::CAPABILITY_POST_PROCESSING:
		// Not yet implemented
		return false;
	case gr_capability::CAPABILITY_DEFERRED_LIGHTING:
		// Not yet implemented
		return false;
	case gr_capability::CAPABILITY_SHADOWS:
	case gr_capability::CAPABILITY_THICK_OUTLINE:
		// Requires geometry shaders / shadow map pipeline (not yet implemented)
		return false;
	case gr_capability::CAPABILITY_BATCHED_SUBMODELS:
		// Requires update_transform_buffer (not yet implemented)
		return false;
	case gr_capability::CAPABILITY_TIMESTAMP_QUERY:
		// Query objects not yet implemented
		return false;
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

// ========== Save/Restore screen (for popups) ==========

static ubyte* Vulkan_saved_screen = nullptr;
static int Vulkan_saved_screen_id = -1;

int vulkan_save_screen()
{
	if (Vulkan_saved_screen) {
		// Already have a saved screen
		return -1;
	}

	ubyte* pixels = nullptr;
	int bmpId = renderer_instance->saveScreen(&pixels);

	if (bmpId < 0) {
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

// ========== Stub functions (not yet implemented) ==========

void stub_print_screen(const char* /*filename*/) {}
SCP_string stub_blob_screen() { return ""; }
void stub_get_region(int /*front*/, int /*w*/, int /*h*/, ubyte* /*data*/) {}
void stub_bm_page_in_start() {}
void stub_update_transform_buffer(void* /*data*/, size_t /*size*/) {}
void stub_post_process_set_effect(const char* /*name*/, int /*x*/, const vec3d* /*rgb*/) {}
void stub_post_process_set_defaults() {}
void stub_post_process_save_zbuffer() {}
void stub_post_process_begin() {}
void stub_post_process_end() {}
void stub_post_process_restore_zbuffer() {}
void stub_copy_effect_texture() {}
void stub_deferred_lighting_begin(bool /*clearNonColorBufs*/) {}
void stub_deferred_lighting_msaa() {}
void stub_deferred_lighting_end() {}
void stub_deferred_lighting_finish() {}
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
int stub_create_query_object() { return -1; }
void stub_query_value(int /*obj*/, QueryType /*type*/) {}
bool stub_query_value_available(int /*obj*/) { return false; }
std::uint64_t stub_get_query_value(int /*obj*/) { return 0; }
void stub_delete_query_object(int /*obj*/) {}
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

	gr_screen.gf_print_screen = stub_print_screen;
	gr_screen.gf_blob_screen = stub_blob_screen;

	gr_screen.gf_zbuffer_get = vulkan_zbuffer_get;
	gr_screen.gf_zbuffer_set = vulkan_zbuffer_set;
	gr_screen.gf_zbuffer_clear = vulkan_zbuffer_clear;

	gr_screen.gf_stencil_set = vulkan_stencil_set;
	gr_screen.gf_stencil_clear = vulkan_stencil_clear;

	gr_screen.gf_alpha_mask_set = vulkan_alpha_mask_set;

	gr_screen.gf_save_screen = vulkan_save_screen;
	gr_screen.gf_restore_screen = vulkan_restore_screen;
	gr_screen.gf_free_screen = vulkan_free_screen;

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

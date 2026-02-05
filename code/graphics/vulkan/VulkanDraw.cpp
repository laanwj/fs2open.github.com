#include "VulkanDraw.h"
#include "VulkanState.h"
#include "VulkanBuffer.h"
#include "VulkanPipeline.h"
#include "VulkanShader.h"
#include "VulkanTexture.h"
#include "VulkanDescriptorManager.h"
#include "VulkanVertexFormat.h"
#include "bmpman/bmpman.h"
#include "graphics/grinternal.h"
#include "graphics/material.h"

namespace graphics {
namespace vulkan {

// Texture slot mapping - material texture types to descriptor binding indices
// Binding 1 in Material set is a texture array with up to 16 textures
static constexpr uint32_t TEXTURE_BINDING_BASE_MAP = 0;
static constexpr uint32_t TEXTURE_BINDING_GLOW_MAP = 1;
static constexpr uint32_t TEXTURE_BINDING_SPEC_MAP = 2;
static constexpr uint32_t TEXTURE_BINDING_NORMAL_MAP = 3;
static constexpr uint32_t TEXTURE_BINDING_HEIGHT_MAP = 4;
static constexpr uint32_t TEXTURE_BINDING_AMBIENT_MAP = 5;
static constexpr uint32_t TEXTURE_BINDING_MISC_MAP = 6;

// Global draw manager pointer
static VulkanDrawManager* g_drawManager = nullptr;

VulkanDrawManager* getDrawManager()
{
	return g_drawManager;
}

void setDrawManager(VulkanDrawManager* manager)
{
	g_drawManager = manager;
}

bool VulkanDrawManager::init(vk::Device device)
{
	if (m_initialized) {
		return true;
	}

	m_device = device;

	m_initialized = true;
	mprintf(("VulkanDrawManager: Initialized\n"));
	return true;
}

void VulkanDrawManager::shutdown()
{
	if (!m_initialized) {
		return;
	}

	m_initialized = false;
	mprintf(("VulkanDrawManager: Shutdown complete\n"));
}

void VulkanDrawManager::clear()
{
	auto* stateTracker = getStateTracker();
	if (!stateTracker || !stateTracker->hasCommandBuffer()) {
		return;
	}

	// In Vulkan, clears are typically done at render pass begin or via vkCmdClearAttachments
	// For now, we'll use vkCmdClearAttachments within an active render pass
	vk::ClearAttachment clearAttachment;
	clearAttachment.aspectMask = vk::ImageAspectFlagBits::eColor;
	clearAttachment.colorAttachment = 0;
	clearAttachment.clearValue.color = stateTracker->getClearColor();

	vk::ClearRect clearRect;
	clearRect.rect.offset = vk::Offset2D(0, 0);
	clearRect.rect.extent = vk::Extent2D(static_cast<uint32_t>(gr_screen.max_w),
	                                      static_cast<uint32_t>(gr_screen.max_h));
	clearRect.baseArrayLayer = 0;
	clearRect.layerCount = 1;

	auto cmdBuffer = stateTracker->getCommandBuffer();
	cmdBuffer.clearAttachments(1, &clearAttachment, 1, &clearRect);
}

void VulkanDrawManager::setClearColor(int r, int g, int b)
{
	auto* stateTracker = getStateTracker();
	if (!stateTracker) {
		return;
	}

	float fr = static_cast<float>(r) / 255.0f;
	float fg = static_cast<float>(g) / 255.0f;
	float fb = static_cast<float>(b) / 255.0f;

	// Apply HDR gamma if needed
	if (High_dynamic_range) {
		const float SRGB_GAMMA = 2.2f;
		fr = powf(fr, SRGB_GAMMA);
		fg = powf(fg, SRGB_GAMMA);
		fb = powf(fb, SRGB_GAMMA);
	}

	stateTracker->setClearColor(fr, fg, fb, 1.0f);

	// Also update gr_screen for compatibility
	gr_screen.current_clear_color.red = static_cast<ubyte>(r);
	gr_screen.current_clear_color.green = static_cast<ubyte>(g);
	gr_screen.current_clear_color.blue = static_cast<ubyte>(b);
	gr_screen.current_clear_color.alpha = 255;
}

void VulkanDrawManager::setClip(int x, int y, int w, int h, int resize_mode)
{
	auto* stateTracker = getStateTracker();
	if (!stateTracker) {
		return;
	}

	// Clamp values
	if (x < 0) x = 0;
	if (y < 0) y = 0;

	int max_w = gr_screen.max_w;
	int max_h = gr_screen.max_h;

	if (x >= max_w) x = max_w - 1;
	if (y >= max_h) y = max_h - 1;

	if (x + w > max_w) w = max_w - x;
	if (y + h > max_h) h = max_h - y;

	if (w > max_w) w = max_w;
	if (h > max_h) h = max_h;

	// Update gr_screen clip state
	gr_screen.offset_x = x;
	gr_screen.offset_y = y;
	gr_screen.clip_left = 0;
	gr_screen.clip_top = 0;
	gr_screen.clip_right = w - 1;
	gr_screen.clip_bottom = h - 1;
	gr_screen.clip_width = w;
	gr_screen.clip_height = h;

	// Calculate center and aspect
	gr_screen.clip_center_x = (gr_screen.clip_left + gr_screen.clip_right) * 0.5f;
	gr_screen.clip_center_y = (gr_screen.clip_top + gr_screen.clip_bottom) * 0.5f;
	gr_screen.clip_aspect = static_cast<float>(w) / static_cast<float>(h);

	// Check if full screen (disable scissor)
	if ((x == 0) && (y == 0) && (w == max_w) && (h == max_h)) {
		stateTracker->setScissorEnabled(false);
		return;
	}

	// Enable scissor test
	stateTracker->setScissorEnabled(true);

	// Vulkan Y coordinate may need adjustment depending on render target
	// For now, use direct coordinates (may need flip for framebuffer vs texture)
	stateTracker->setScissor(x, y, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
}

void VulkanDrawManager::resetClip()
{
	auto* stateTracker = getStateTracker();
	if (!stateTracker) {
		return;
	}

	int max_w = gr_screen.max_w;
	int max_h = gr_screen.max_h;

	gr_screen.offset_x = 0;
	gr_screen.offset_y = 0;
	gr_screen.clip_left = 0;
	gr_screen.clip_top = 0;
	gr_screen.clip_right = max_w - 1;
	gr_screen.clip_bottom = max_h - 1;
	gr_screen.clip_width = max_w;
	gr_screen.clip_height = max_h;

	gr_screen.clip_center_x = (gr_screen.clip_left + gr_screen.clip_right) * 0.5f;
	gr_screen.clip_center_y = (gr_screen.clip_top + gr_screen.clip_bottom) * 0.5f;
	gr_screen.clip_aspect = static_cast<float>(max_w) / static_cast<float>(max_h);

	stateTracker->setScissorEnabled(false);
}

int VulkanDrawManager::zbufferGet()
{
	if (!gr_global_zbuffering) {
		return GR_ZBUFF_NONE;
	}
	return m_zbufferMode;
}

int VulkanDrawManager::zbufferSet(int mode)
{
	auto* stateTracker = getStateTracker();

	int prev = m_zbufferMode;
	m_zbufferMode = mode;

	// Update FSO global state
	if (mode == GR_ZBUFF_NONE) {
		gr_zbuffering = 0;
	} else {
		gr_zbuffering = 1;
	}
	gr_zbuffering_mode = mode;

	if (stateTracker) {
		gr_zbuffer_type zbufType;
		switch (mode) {
		case GR_ZBUFF_NONE:
			zbufType = ZBUFFER_TYPE_NONE;
			break;
		case GR_ZBUFF_READ:
			zbufType = ZBUFFER_TYPE_READ;
			break;
		case GR_ZBUFF_WRITE:
			zbufType = ZBUFFER_TYPE_WRITE;
			break;
		case GR_ZBUFF_FULL:
		default:
			zbufType = ZBUFFER_TYPE_FULL;
			break;
		}
		stateTracker->setZBufferMode(zbufType);
	}

	return prev;
}

void VulkanDrawManager::zbufferClear(int mode)
{
	auto* stateTracker = getStateTracker();
	if (!stateTracker || !stateTracker->hasCommandBuffer()) {
		return;
	}

	if (mode) {
		// Enable zbuffering and clear
		gr_zbuffering = 1;
		gr_zbuffering_mode = GR_ZBUFF_FULL;
		gr_global_zbuffering = 1;
		m_zbufferMode = GR_ZBUFF_FULL;
		stateTracker->setZBufferMode(ZBUFFER_TYPE_FULL);

		// Clear depth buffer
		vk::ClearAttachment clearAttachment;
		clearAttachment.aspectMask = vk::ImageAspectFlagBits::eDepth;
		clearAttachment.clearValue.depthStencil.depth = 1.0f;
		clearAttachment.clearValue.depthStencil.stencil = 0;

		vk::ClearRect clearRect;
		clearRect.rect.offset = vk::Offset2D(0, 0);
		clearRect.rect.extent = vk::Extent2D(static_cast<uint32_t>(gr_screen.max_w),
		                                      static_cast<uint32_t>(gr_screen.max_h));
		clearRect.baseArrayLayer = 0;
		clearRect.layerCount = 1;

		stateTracker->getCommandBuffer().clearAttachments(1, &clearAttachment, 1, &clearRect);
	} else {
		// Disable zbuffering
		gr_zbuffering = 0;
		gr_zbuffering_mode = GR_ZBUFF_NONE;
		gr_global_zbuffering = 0;
		m_zbufferMode = GR_ZBUFF_NONE;
		stateTracker->setZBufferMode(ZBUFFER_TYPE_NONE);
	}
}

int VulkanDrawManager::stencilSet(int mode)
{
	auto* stateTracker = getStateTracker();

	int prev = m_stencilMode;
	m_stencilMode = mode;
	gr_stencil_mode = mode;

	if (stateTracker) {
		stateTracker->setStencilMode(mode);

		// Set stencil reference based on mode
		if (mode == GR_STENCIL_READ || mode == GR_STENCIL_WRITE) {
			stateTracker->setStencilReference(1);
		} else {
			stateTracker->setStencilReference(0);
		}
	}

	return prev;
}

void VulkanDrawManager::stencilClear()
{
	auto* stateTracker = getStateTracker();
	if (!stateTracker || !stateTracker->hasCommandBuffer()) {
		return;
	}

	// Clear stencil buffer
	vk::ClearAttachment clearAttachment;
	clearAttachment.aspectMask = vk::ImageAspectFlagBits::eStencil;
	clearAttachment.clearValue.depthStencil.depth = 1.0f;
	clearAttachment.clearValue.depthStencil.stencil = 0;

	vk::ClearRect clearRect;
	clearRect.rect.offset = vk::Offset2D(0, 0);
	clearRect.rect.extent = vk::Extent2D(static_cast<uint32_t>(gr_screen.max_w),
	                                      static_cast<uint32_t>(gr_screen.max_h));
	clearRect.baseArrayLayer = 0;
	clearRect.layerCount = 1;

	stateTracker->getCommandBuffer().clearAttachments(1, &clearAttachment, 1, &clearRect);
}

int VulkanDrawManager::setCull(int cull)
{
	auto* stateTracker = getStateTracker();

	int prev = m_cullEnabled ? 1 : 0;
	m_cullEnabled = (cull != 0);

	if (stateTracker) {
		stateTracker->setCullMode(m_cullEnabled);
	}

	return prev;
}

void VulkanDrawManager::renderPrimitives(material* material_info, primitive_type prim_type,
                                          vertex_layout* layout, int offset, int n_verts,
                                          gr_buffer_handle buffer_handle, size_t buffer_offset)
{
	if (!material_info || !layout || n_verts <= 0) {
		return;
	}

	auto* stateTracker = getStateTracker();
	if (!stateTracker || !stateTracker->hasCommandBuffer()) {
		nprintf(("Vulkan", "VulkanDrawManager: No command buffer for renderPrimitives\n"));
		return;
	}

	// Apply material state and bind pipeline
	if (!applyMaterial(material_info, prim_type, layout)) {
		return;
	}

	// Bind vertex buffer
	bindVertexBuffer(buffer_handle, buffer_offset);

	// Issue draw call
	draw(prim_type, offset, n_verts);
}

void VulkanDrawManager::renderPrimitivesBatched(batched_bitmap_material* material_info,
                                                 primitive_type prim_type, vertex_layout* layout,
                                                 int offset, int n_verts, gr_buffer_handle buffer_handle)
{
	if (!material_info || !layout || n_verts <= 0) {
		return;
	}

	auto* stateTracker = getStateTracker();
	if (!stateTracker || !stateTracker->hasCommandBuffer()) {
		return;
	}

	// Apply base material state and bind pipeline
	if (!applyMaterial(material_info, prim_type, layout)) {
		return;
	}

	// Bind vertex buffer
	bindVertexBuffer(buffer_handle, 0);

	// Issue draw call
	draw(prim_type, offset, n_verts);
}

void VulkanDrawManager::renderPrimitivesParticle(particle_material* material_info,
                                                  primitive_type prim_type, vertex_layout* layout,
                                                  int offset, int n_verts, gr_buffer_handle buffer_handle)
{
	if (!material_info || !layout || n_verts <= 0) {
		return;
	}

	auto* stateTracker = getStateTracker();
	if (!stateTracker || !stateTracker->hasCommandBuffer()) {
		return;
	}

	if (!applyMaterial(material_info, prim_type, layout)) {
		return;
	}
	bindVertexBuffer(buffer_handle, 0);
	draw(prim_type, offset, n_verts);
}

void VulkanDrawManager::renderPrimitivesDistortion(distortion_material* material_info,
                                                    primitive_type prim_type, vertex_layout* layout,
                                                    int n_verts, gr_buffer_handle buffer_handle)
{
	if (!material_info || !layout || n_verts <= 0) {
		return;
	}

	auto* stateTracker = getStateTracker();
	if (!stateTracker || !stateTracker->hasCommandBuffer()) {
		return;
	}

	if (!applyMaterial(material_info, prim_type, layout)) {
		return;
	}
	bindVertexBuffer(buffer_handle, 0);
	draw(prim_type, 0, n_verts);
}

void VulkanDrawManager::renderMovie(movie_material* material_info, primitive_type prim_type,
                                     vertex_layout* layout, int n_verts, gr_buffer_handle buffer_handle)
{
	if (!material_info || !layout || n_verts <= 0) {
		return;
	}

	auto* stateTracker = getStateTracker();
	if (!stateTracker || !stateTracker->hasCommandBuffer()) {
		return;
	}

	if (!applyMaterial(material_info, prim_type, layout)) {
		return;
	}
	bindVertexBuffer(buffer_handle, 0);
	draw(prim_type, 0, n_verts);
}

void VulkanDrawManager::renderNanoVG(nanovg_material* material_info, primitive_type prim_type,
                                      vertex_layout* layout, int offset, int n_verts,
                                      gr_buffer_handle buffer_handle)
{
	if (!material_info || !layout || n_verts <= 0) {
		return;
	}

	auto* stateTracker = getStateTracker();
	if (!stateTracker || !stateTracker->hasCommandBuffer()) {
		return;
	}

	if (!applyMaterial(material_info, prim_type, layout)) {
		return;
	}
	bindVertexBuffer(buffer_handle, 0);
	draw(prim_type, offset, n_verts);
}

void VulkanDrawManager::renderRocketPrimitives(interface_material* material_info,
                                                primitive_type prim_type, vertex_layout* layout,
                                                int n_indices, gr_buffer_handle vertex_buffer,
                                                gr_buffer_handle index_buffer)
{
	if (!material_info || !layout || n_indices <= 0) {
		return;
	}

	auto* stateTracker = getStateTracker();
	if (!stateTracker || !stateTracker->hasCommandBuffer()) {
		return;
	}

	if (!applyMaterial(material_info, prim_type, layout)) {
		return;
	}
	bindVertexBuffer(vertex_buffer, 0);
	bindIndexBuffer(index_buffer);
	drawIndexed(prim_type, n_indices, 0, 0);
}

void VulkanDrawManager::renderModel(model_material* material_info, indexed_vertex_source* vert_source,
                                     vertex_buffer* bufferp, size_t texi)
{
	if (!material_info || !vert_source || !bufferp) {
		return;
	}

	// Validate buffers
	if (!vert_source->Vbuffer_handle.isValid() || !vert_source->Ibuffer_handle.isValid()) {
		nprintf(("Vulkan", "VulkanDrawManager: renderModel called with invalid buffer handles\n"));
		return;
	}

	if (texi >= bufferp->tex_buf.size()) {
		nprintf(("Vulkan", "VulkanDrawManager: renderModel texi out of range\n"));
		return;
	}

	auto* stateTracker = getStateTracker();
	if (!stateTracker || !stateTracker->hasCommandBuffer()) {
		return;
	}

	// Get buffer data for this texture/draw
	buffer_data* datap = &bufferp->tex_buf[texi];

	if (datap->n_verts == 0) {
		return;  // Nothing to draw
	}

	// Apply model material state and bind pipeline
	// Model rendering always uses triangles
	if (!applyMaterial(material_info, PRIM_TYPE_TRIS, &bufferp->layout)) {
		return;
	}

	// Bind vertex buffer with the model's vertex offset
	auto* bufferManager = getBufferManager();
	if (!bufferManager) {
		return;
	}

	vk::Buffer vbuffer = bufferManager->getVkBuffer(vert_source->Vbuffer_handle);
	vk::Buffer ibuffer = bufferManager->getVkBuffer(vert_source->Ibuffer_handle);

	if (!vbuffer || !ibuffer) {
		nprintf(("Vulkan", "VulkanDrawManager: renderModel failed to get Vulkan buffers\n"));
		return;
	}

	// Bind vertex buffer with vertex source offset
	stateTracker->bindVertexBuffer(0, vbuffer, static_cast<vk::DeviceSize>(vert_source->Vertex_offset));

	// Determine index type based on VB_FLAG_LARGE_INDEX flag
	vk::IndexType indexType = (datap->flags & VB_FLAG_LARGE_INDEX) ?
	                          vk::IndexType::eUint32 : vk::IndexType::eUint16;

	// Bind index buffer with source offset
	stateTracker->bindIndexBuffer(ibuffer, static_cast<vk::DeviceSize>(vert_source->Index_offset), indexType);

	// Calculate the base vertex offset
	int32_t baseVertex = static_cast<int32_t>(vert_source->Base_vertex_offset + bufferp->vertex_num_offset);

	// Calculate first index
	// The index_offset in buffer_data is in bytes, need to convert to index count
	uint32_t firstIndex;
	if (indexType == vk::IndexType::eUint32) {
		firstIndex = static_cast<uint32_t>(datap->index_offset / sizeof(uint32_t));
	} else {
		firstIndex = static_cast<uint32_t>(datap->index_offset / sizeof(uint16_t));
	}

	// Issue indexed draw call
	auto cmdBuffer = stateTracker->getCommandBuffer();
	cmdBuffer.drawIndexed(
		static_cast<uint32_t>(datap->n_verts),  // index count
		1,                                       // instance count
		firstIndex,                              // first index
		baseVertex,                              // vertex offset
		0                                        // first instance
	);
}

void VulkanDrawManager::clearStates()
{
	auto* stateTracker = getStateTracker();

	// Reset to default state
	m_zbufferMode = GR_ZBUFF_FULL;
	m_stencilMode = GR_STENCIL_NONE;
	m_cullEnabled = true;

	gr_zbuffering = 1;
	gr_zbuffering_mode = GR_ZBUFF_FULL;
	gr_global_zbuffering = 1;
	gr_stencil_mode = GR_STENCIL_NONE;

	if (stateTracker) {
		stateTracker->setZBufferMode(ZBUFFER_TYPE_FULL);
		stateTracker->setStencilMode(GR_STENCIL_NONE);
		stateTracker->setCullMode(true);
		stateTracker->setScissorEnabled(false);
		stateTracker->setDepthBias(0.0f, 0.0f);
		stateTracker->setLineWidth(1.0f);
	}

	// Clear pending uniform bindings
	clearPendingUniformBindings();

	// Reset clip
	resetClip();
}

void VulkanDrawManager::setPendingUniformBinding(uniform_block_type blockType, gr_buffer_handle bufferHandle,
                                                  vk::DeviceSize offset, vk::DeviceSize size)
{
	size_t index = static_cast<size_t>(blockType);
	if (index >= NUM_UNIFORM_BLOCK_TYPES) {
		return;
	}

	m_pendingUniformBindings[index].bufferHandle = bufferHandle;
	m_pendingUniformBindings[index].offset = offset;
	m_pendingUniformBindings[index].size = size;
	m_pendingUniformBindings[index].valid = bufferHandle.isValid();
}

void VulkanDrawManager::clearPendingUniformBindings()
{
	for (auto& binding : m_pendingUniformBindings) {
		binding.valid = false;
		binding.bufferHandle = gr_buffer_handle();
		binding.offset = 0;
		binding.size = 0;
	}
}

void VulkanDrawManager::applyPendingUniformBindings()
{
	auto* stateTracker = getStateTracker();
	auto* descManager = getDescriptorManager();
	auto* bufferManager = getBufferManager();

	if (!stateTracker || !descManager || !bufferManager) {
		return;
	}

	// Helper to get vk::Buffer from handle at bind time (survives buffer recreation)
	auto getBuffer = [bufferManager](const PendingUniformBinding& binding) -> vk::Buffer {
		return bufferManager->getVkBuffer(binding.bufferHandle);
	};

	// Helper to adjust offset for ring buffer - adds frame base offset
	auto getAdjustedOffset = [bufferManager](const PendingUniformBinding& binding) -> vk::DeviceSize {
		size_t frameOffset = bufferManager->getFrameBaseOffset(binding.bufferHandle);
		return static_cast<vk::DeviceSize>(frameOffset + binding.offset);
	};

	// Track which descriptor sets we need to allocate and bind
	bool needGlobalSet = false;
	bool needMaterialSet = false;
	bool needPerDrawSet = false;

	// Check which sets have pending bindings
	for (size_t i = 0; i < NUM_UNIFORM_BLOCK_TYPES; ++i) {
		if (!m_pendingUniformBindings[i].valid) {
			continue;
		}

		uniform_block_type blockType = static_cast<uniform_block_type>(i);
		DescriptorSetIndex setIndex;
		uint32_t binding;

		if (VulkanDescriptorManager::getUniformBlockBinding(blockType, setIndex, binding)) {
			switch (setIndex) {
			case DescriptorSetIndex::Global:
				needGlobalSet = true;
				break;
			case DescriptorSetIndex::Material:
				needMaterialSet = true;
				break;
			case DescriptorSetIndex::PerDraw:
				needPerDrawSet = true;
				break;
			default:
				break;
			}
		}
	}

	// Allocate and update descriptor sets as needed
	if (needGlobalSet) {
		vk::DescriptorSet globalSet = descManager->allocateFrameSet(DescriptorSetIndex::Global);
		if (globalSet) {
			// Update bindings for global set
			for (size_t i = 0; i < NUM_UNIFORM_BLOCK_TYPES; ++i) {
				if (!m_pendingUniformBindings[i].valid) {
					continue;
				}

				uniform_block_type blockType = static_cast<uniform_block_type>(i);
				DescriptorSetIndex setIndex;
				uint32_t binding;

				if (VulkanDescriptorManager::getUniformBlockBinding(blockType, setIndex, binding) &&
				    setIndex == DescriptorSetIndex::Global) {
					descManager->updateUniformBuffer(globalSet, binding,
					                                  getBuffer(m_pendingUniformBindings[i]),
					                                  getAdjustedOffset(m_pendingUniformBindings[i]),
					                                  m_pendingUniformBindings[i].size);
				}
			}
			stateTracker->bindDescriptorSet(DescriptorSetIndex::Global, globalSet);
		}
	}

	// Note: Material set is typically handled in applyMaterial() along with textures
	// We handle it here for uniform-only bindings (when no textures are involved)
	if (needMaterialSet) {
		vk::DescriptorSet materialSet = descManager->allocateFrameSet(DescriptorSetIndex::Material);
		if (materialSet) {
			for (size_t i = 0; i < NUM_UNIFORM_BLOCK_TYPES; ++i) {
				if (!m_pendingUniformBindings[i].valid) {
					continue;
				}

				uniform_block_type blockType = static_cast<uniform_block_type>(i);
				DescriptorSetIndex setIndex;
				uint32_t binding;

				if (VulkanDescriptorManager::getUniformBlockBinding(blockType, setIndex, binding) &&
				    setIndex == DescriptorSetIndex::Material) {
					descManager->updateUniformBuffer(materialSet, binding,
					                                  getBuffer(m_pendingUniformBindings[i]),
					                                  getAdjustedOffset(m_pendingUniformBindings[i]),
					                                  m_pendingUniformBindings[i].size);
				}
			}
			stateTracker->bindDescriptorSet(DescriptorSetIndex::Material, materialSet);
		}
	}

	if (needPerDrawSet) {
		vk::DescriptorSet perDrawSet = descManager->allocateFrameSet(DescriptorSetIndex::PerDraw);
		if (perDrawSet) {
			for (size_t i = 0; i < NUM_UNIFORM_BLOCK_TYPES; ++i) {
				if (!m_pendingUniformBindings[i].valid) {
					continue;
				}

				uniform_block_type blockType = static_cast<uniform_block_type>(i);
				DescriptorSetIndex setIndex;
				uint32_t binding;

				if (VulkanDescriptorManager::getUniformBlockBinding(blockType, setIndex, binding) &&
				    setIndex == DescriptorSetIndex::PerDraw) {
					descManager->updateUniformBuffer(perDrawSet, binding,
					                                  getBuffer(m_pendingUniformBindings[i]),
					                                  getAdjustedOffset(m_pendingUniformBindings[i]),
					                                  m_pendingUniformBindings[i].size);
				}
			}
			stateTracker->bindDescriptorSet(DescriptorSetIndex::PerDraw, perDrawSet);
		}
	}
}

PipelineConfig VulkanDrawManager::buildPipelineConfig(material* mat, primitive_type prim_type)
{
	PipelineConfig config;

	// Get shader info from material
	int shaderHandle = mat->get_shader_handle();
	auto* shaderManager = getShaderManager();
	if (shaderManager && shaderHandle >= 0) {
		const auto* shaderModule = shaderManager->getShaderByHandle(shaderHandle);
		if (shaderModule) {
			config.shaderType = shaderModule->type;
			config.shaderFlags = shaderModule->flags;
		}
	}

	// Primitive type
	config.primitiveType = prim_type;

	// Depth mode
	config.depthMode = mat->get_depth_mode();

	// Blend mode
	config.blendMode = mat->get_blend_mode();

	// Cull mode
	config.cullEnabled = mat->get_cull_mode();

	// Depth write
	config.depthWriteEnabled = (config.depthMode == ZBUFFER_TYPE_FULL ||
	                             config.depthMode == ZBUFFER_TYPE_WRITE);

	// Stencil state
	config.stencilEnabled = mat->is_stencil_enabled();
	if (config.stencilEnabled) {
		config.stencilFunc = mat->get_stencil_func().compare;
		config.stencilMask = mat->get_stencil_func().mask;
	}

	// Get current render pass from state tracker
	auto* stateTracker = getStateTracker();
	if (stateTracker) {
		config.renderPass = stateTracker->getCurrentRenderPass();
	}

	return config;
}

bool VulkanDrawManager::bindMaterialTextures(material* mat, vk::DescriptorSet materialSet)
{
	auto* texManager = getTextureManager();
	auto* descManager = getDescriptorManager();

	if (!texManager || !descManager || !materialSet) {
		return false;
	}

	// Get default sampler and fallback texture for unbound slots
	vk::Sampler defaultSampler = texManager->getDefaultSampler();
	vk::ImageView fallbackView = texManager->getFallbackTextureView();

	// Check for movie material - needs special YUV texture handling
	auto* movieMat = dynamic_cast<movie_material*>(mat);
	if (movieMat) {
		// Movie materials use 3 YUV textures in the texture array at indices 0, 1, 2
		SCP_vector<vk::DescriptorImageInfo> textureInfos;
		textureInfos.resize(VulkanDescriptorManager::MAX_TEXTURE_BINDINGS);

		// Initialize all slots with fallback
		for (auto& info : textureInfos) {
			info.sampler = defaultSampler;
			info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
			info.imageView = fallbackView;
		}

		auto loadYuvTexture = [&](int handle, uint32_t slot) {
			if (handle < 0 || slot >= textureInfos.size()) return;
			auto* texSlot = texManager->getTextureSlot(handle);
			if (!texSlot || !texSlot->imageView) {
				// Load on demand - YUV planes are 8bpp grayscale
				bitmap* bmp = bm_lock(handle, 8, BMP_TEX_OTHER);
				if (bmp) {
					texManager->bm_data(handle, bmp);
					bm_unlock(handle);
					texSlot = texManager->getTextureSlot(handle);
				}
			}
			if (texSlot && texSlot->imageView) {
				textureInfos[slot].imageView = texSlot->imageView;
			}
		};

		loadYuvTexture(movieMat->getYtex(), 0);  // Y at index 0
		loadYuvTexture(movieMat->getUtex(), 1);  // U at index 1
		loadYuvTexture(movieMat->getVtex(), 2);  // V at index 2

		descManager->updateTextureArray(materialSet, 1, textureInfos);
		return true;
	}

	// Build texture info array for all material texture slots
	SCP_vector<vk::DescriptorImageInfo> textureInfos;
	textureInfos.resize(VulkanDescriptorManager::MAX_TEXTURE_BINDINGS);

	// Initialize all slots with fallback texture (1x1 white)
	for (auto& info : textureInfos) {
		info.sampler = defaultSampler;
		info.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		info.imageView = fallbackView;  // Fallback texture for unbound slots
	}

	// Helper to set texture at a specific slot - loads on-demand if not present
	static int texLogCount = 0;

	// Get material's expected texture type for the base map
	int materialTextureType = mat->get_texture_type();

	auto setTexture = [&](int textureHandle, uint32_t slot, bool isBaseMap = false) {
		if (textureHandle < 0 || slot >= textureInfos.size()) {
			return;
		}

		auto* texSlot = texManager->getTextureSlot(textureHandle);

		// If texture isn't loaded, try to load it on-demand (like OpenGL does)
		if (!texSlot || !texSlot->imageView) {
			// For base map, use material's texture type. For others, use XPARENT/NORMAL
			int bitmapType = isBaseMap ? materialTextureType : TCACHE_TYPE_XPARENT;
			ushort lockFlags = 0;
			int bpp = 32;

			switch (bitmapType) {
				case TCACHE_TYPE_AABITMAP:
					lockFlags = BMP_AABITMAP;
					bpp = 8;
					break;
				case TCACHE_TYPE_INTERFACE:
				case TCACHE_TYPE_XPARENT:
					lockFlags = BMP_TEX_XPARENT;
					bpp = 32;
					break;
				default:
					lockFlags = BMP_TEX_OTHER;
					bpp = bm_has_alpha_channel(textureHandle) ? 32 : 24;
					break;
			}

			// Lock bitmap with appropriate flags
			bitmap* bmp = bm_lock(textureHandle, bpp, lockFlags);
			if (bmp) {
				// Upload texture
				texManager->bm_data(textureHandle, bmp);
				bm_unlock(textureHandle);

				// Re-get the slot after upload
				texSlot = texManager->getTextureSlot(textureHandle);

				if (texLogCount < 20) {
					mprintf(("bindMaterialTextures: on-demand loaded texture %d (type=%d bpp=%d), slot now=%p\n",
						textureHandle, bitmapType, bpp, texSlot));
					texLogCount++;
				}
			}
		}

		if (texSlot && texSlot->imageView) {
			textureInfos[slot].imageView = texSlot->imageView;
			if (texLogCount < 20) {
				mprintf(("bindMaterialTextures: slot %u bound to handle %d (view=%p)\n",
					slot, textureHandle, static_cast<void*>(static_cast<VkImageView>(texSlot->imageView))));
				texLogCount++;
			}
		} else {
			if (texLogCount < 20) {
				mprintf(("bindMaterialTextures: slot %u handle %d FAILED to load\n",
					slot, textureHandle));
				texLogCount++;
			}
		}
	};

	// Bind material textures to their slots
	// Base map uses material's texture type (may be AABITMAP for fonts)
	setTexture(mat->get_texture_map(TM_BASE_TYPE), TEXTURE_BINDING_BASE_MAP, true);
	setTexture(mat->get_texture_map(TM_GLOW_TYPE), TEXTURE_BINDING_GLOW_MAP);

	// Specular - prefer spec_gloss if available
	int specMap = mat->get_texture_map(TM_SPEC_GLOSS_TYPE);
	if (specMap < 0) {
		specMap = mat->get_texture_map(TM_SPECULAR_TYPE);
	}
	setTexture(specMap, TEXTURE_BINDING_SPEC_MAP);

	setTexture(mat->get_texture_map(TM_NORMAL_TYPE), TEXTURE_BINDING_NORMAL_MAP);
	setTexture(mat->get_texture_map(TM_HEIGHT_TYPE), TEXTURE_BINDING_HEIGHT_MAP);
	setTexture(mat->get_texture_map(TM_AMBIENT_TYPE), TEXTURE_BINDING_AMBIENT_MAP);
	setTexture(mat->get_texture_map(TM_MISC_TYPE), TEXTURE_BINDING_MISC_MAP);

	// Update the texture array in the descriptor set
	// All slots now have valid views (either actual texture or fallback)
	descManager->updateTextureArray(materialSet, 1, textureInfos);

	return true;
}

bool VulkanDrawManager::applyMaterial(material* mat, primitive_type prim_type, vertex_layout* layout)
{
	auto* stateTracker = getStateTracker();
	auto* pipelineManager = getPipelineManager();
	auto* descManager = getDescriptorManager();
	auto* bufferManager = getBufferManager();

	if (!stateTracker || !pipelineManager || !mat || !layout || !bufferManager) {
		return false;
	}

	// Helper to get vk::Buffer from handle at bind time (survives buffer recreation)
	auto getBuffer = [bufferManager](const PendingUniformBinding& binding) -> vk::Buffer {
		return bufferManager->getVkBuffer(binding.bufferHandle);
	};

	// Helper to adjust offset for ring buffer - adds frame base offset
	auto getAdjustedOffset = [bufferManager](const PendingUniformBinding& binding) -> vk::DeviceSize {
		size_t frameOffset = bufferManager->getFrameBaseOffset(binding.bufferHandle);
		return static_cast<vk::DeviceSize>(frameOffset + binding.offset);
	};

	// Build pipeline configuration from material
	PipelineConfig config = buildPipelineConfig(mat, prim_type);

	// Debug: Log material and shader type
	static int matLogCount = 0;
	if (matLogCount < 50) {
		const char* matType = "unknown";
		if (dynamic_cast<movie_material*>(mat)) matType = "movie";
		else if (dynamic_cast<batched_bitmap_material*>(mat)) matType = "batched";
		else if (dynamic_cast<interface_material*>(mat)) matType = "interface";
		else if (dynamic_cast<nanovg_material*>(mat)) matType = "nanovg";
		else if (dynamic_cast<particle_material*>(mat)) matType = "particle";
		else if (dynamic_cast<distortion_material*>(mat)) matType = "distortion";
		else if (dynamic_cast<shield_material*>(mat)) matType = "shield";
		else if (dynamic_cast<model_material*>(mat)) matType = "model";
		mprintf(("applyMaterial #%d: matType=%s shaderType=%d\n", matLogCount++, matType, static_cast<int>(config.shaderType)));
	}

	// Check if we have a valid render pass
	if (!config.renderPass) {
		nprintf(("Vulkan", "VulkanDrawManager: No active render pass for drawing\n"));
		return false;
	}

	// Get or create pipeline
	vk::Pipeline pipeline = pipelineManager->getPipeline(config, *layout);
	if (!pipeline) {
		nprintf(("Vulkan", "VulkanDrawManager: Failed to get pipeline for shader type %d\n",
			static_cast<int>(config.shaderType)));
		return false;
	}

	// Bind pipeline with layout
	stateTracker->bindPipeline(pipeline, pipelineManager->getPipelineLayout());

	// Bind fallback color buffer if vertex data doesn't have color
	if (pipelineManager->needsFallbackColor(*layout)) {
		vk::Buffer fallbackColor = bufferManager->getFallbackColorBuffer();
		if (fallbackColor) {
			stateTracker->bindVertexBuffer(FALLBACK_COLOR_BINDING, fallbackColor, 0);
		}
	}

	// Bind fallback texcoord buffer if vertex data doesn't have texcoords
	if (pipelineManager->needsFallbackTexCoord(*layout)) {
		vk::Buffer fallbackTexCoord = bufferManager->getFallbackTexCoordBuffer();
		if (fallbackTexCoord) {
			stateTracker->bindVertexBuffer(FALLBACK_TEXCOORD_BINDING, fallbackTexCoord, 0);
		}
	}

	// Apply any pending uniform buffer bindings
	applyPendingUniformBindings();

	// Allocate and bind descriptor sets for this draw
	// Vulkan requires all sets in the pipeline layout to be bound
	if (descManager) {
		// Set 0: Global - allocate and bind (even if shader doesn't use it)
		vk::DescriptorSet globalSet = descManager->allocateFrameSet(DescriptorSetIndex::Global);
		if (globalSet) {
			stateTracker->bindDescriptorSet(DescriptorSetIndex::Global, globalSet);
		}

		// Set 1: Material - allocate, update textures, and bind
		vk::DescriptorSet materialSet = descManager->allocateFrameSet(DescriptorSetIndex::Material);
		if (materialSet) {
			// Bind textures
			bindMaterialTextures(mat, materialSet);

			// Also bind any pending uniform buffers for the Material set
			for (size_t i = 0; i < NUM_UNIFORM_BLOCK_TYPES; ++i) {
				if (!m_pendingUniformBindings[i].valid) {
					continue;
				}

				uniform_block_type blockType = static_cast<uniform_block_type>(i);
				DescriptorSetIndex setIndex;
				uint32_t binding;

				if (VulkanDescriptorManager::getUniformBlockBinding(blockType, setIndex, binding) &&
				    setIndex == DescriptorSetIndex::Material) {
					descManager->updateUniformBuffer(materialSet, binding,
					                                  getBuffer(m_pendingUniformBindings[i]),
					                                  getAdjustedOffset(m_pendingUniformBindings[i]),
					                                  m_pendingUniformBindings[i].size);
				}
			}

			// Bind the descriptor set
			stateTracker->bindDescriptorSet(DescriptorSetIndex::Material, materialSet);
		}

		// Set 2: PerDraw - allocate, update uniforms, and bind
		vk::DescriptorSet perDrawSet = descManager->allocateFrameSet(DescriptorSetIndex::PerDraw);
		if (perDrawSet) {
			// Bind any pending uniform buffers for the PerDraw set
			for (size_t i = 0; i < NUM_UNIFORM_BLOCK_TYPES; ++i) {
				if (!m_pendingUniformBindings[i].valid) {
					continue;
				}

				uniform_block_type blockType = static_cast<uniform_block_type>(i);
				DescriptorSetIndex setIndex;
				uint32_t binding;

				if (VulkanDescriptorManager::getUniformBlockBinding(blockType, setIndex, binding) &&
				    setIndex == DescriptorSetIndex::PerDraw) {
					descManager->updateUniformBuffer(perDrawSet, binding,
					                                  getBuffer(m_pendingUniformBindings[i]),
					                                  getAdjustedOffset(m_pendingUniformBindings[i]),
					                                  m_pendingUniformBindings[i].size);
				}
			}

			stateTracker->bindDescriptorSet(DescriptorSetIndex::PerDraw, perDrawSet);
		}
	}

	// Update tracked state for FSO compatibility
	stateTracker->setZBufferMode(mat->get_depth_mode());
	stateTracker->setCullMode(mat->get_cull_mode());

	if (mat->is_stencil_enabled()) {
		stateTracker->setStencilMode(GR_STENCIL_READ);
		stateTracker->setStencilReference(mat->get_stencil_func().ref);
	} else {
		stateTracker->setStencilMode(GR_STENCIL_NONE);
	}

	// Set depth bias if needed
	stateTracker->setDepthBias(static_cast<float>(mat->get_depth_bias()), 0.0f);

	return true;
}

void VulkanDrawManager::bindVertexBuffer(gr_buffer_handle handle, size_t offset)
{
	auto* bufferManager = getBufferManager();
	auto* stateTracker = getStateTracker();

	if (!bufferManager || !stateTracker || !handle.isValid()) {
		static int warnCount = 0;
		if (warnCount < 5) {
			mprintf(("VulkanDrawManager::bindVertexBuffer - SKIP: bufMgr=%p tracker=%p handle.valid=%d\n",
				bufferManager, stateTracker, handle.isValid() ? 1 : 0));
			warnCount++;
		}
		return;
	}

	vk::Buffer buffer = bufferManager->getVkBuffer(handle);
	if (buffer) {
		// Add frame base offset for ring buffer support
		// This maps the caller's offset into the current frame's span
		size_t frameOffset = bufferManager->getFrameBaseOffset(handle);
		size_t totalOffset = frameOffset + offset;

		static int bindCount = 0;
		if (bindCount < 10) {
			mprintf(("VulkanDrawManager::bindVertexBuffer #%d - handle=%d offset=%zu frameOffset=%zu total=%zu\n",
				bindCount++, handle.value(), offset, frameOffset, totalOffset));
		}
		stateTracker->bindVertexBuffer(0, buffer, static_cast<vk::DeviceSize>(totalOffset));
	}
}

void VulkanDrawManager::bindIndexBuffer(gr_buffer_handle handle)
{
	auto* bufferManager = getBufferManager();
	auto* stateTracker = getStateTracker();

	if (!bufferManager || !stateTracker || !handle.isValid()) {
		return;
	}

	vk::Buffer buffer = bufferManager->getVkBuffer(handle);
	if (buffer) {
		stateTracker->bindIndexBuffer(buffer, 0, vk::IndexType::eUint32);
	}
}

void VulkanDrawManager::draw(primitive_type prim_type, int first_vertex, int vertex_count)
{
	auto* stateTracker = getStateTracker();
	if (!stateTracker || !stateTracker->hasCommandBuffer()) {
		// No state tracker or no command buffer - skip silently
		return;
	}

	// Check if pipeline is bound - drawing without a pipeline causes corruption
	if (!stateTracker->getCurrentPipeline()) {
		static int warnCount = 0;
		if (warnCount < 5) {
			mprintf(("VulkanDrawManager::draw - WARNING: no pipeline bound! Skipping draw.\n"));
			warnCount++;
		}
		return;
	}

	// Per-frame draw counter for debugging
	static int totalDraws = 0;
	if (totalDraws < 50) {
		mprintf(("VulkanDrawManager::draw #%d - vertices=%d first=%d pipeline=%p\n",
			totalDraws, vertex_count, first_vertex,
			static_cast<void*>(static_cast<VkPipeline>(stateTracker->getCurrentPipeline()))));
	}
	totalDraws++;

	auto cmdBuffer = stateTracker->getCommandBuffer();
	cmdBuffer.draw(static_cast<uint32_t>(vertex_count),
	               1,
	               static_cast<uint32_t>(first_vertex),
	               0);
}

void VulkanDrawManager::drawIndexed(primitive_type prim_type, int index_count, int first_index, int vertex_offset)
{
	auto* stateTracker = getStateTracker();
	if (!stateTracker || !stateTracker->hasCommandBuffer()) {
		return;
	}

	// Check if pipeline is bound - drawing without a pipeline causes corruption
	if (!stateTracker->getCurrentPipeline()) {
		static int warnCount = 0;
		if (warnCount < 5) {
			mprintf(("VulkanDrawManager::drawIndexed - WARNING: no pipeline bound! Skipping draw.\n"));
			warnCount++;
		}
		return;
	}

	auto cmdBuffer = stateTracker->getCommandBuffer();
	cmdBuffer.drawIndexed(static_cast<uint32_t>(index_count),
	                      1,
	                      static_cast<uint32_t>(first_index),
	                      vertex_offset,
	                      0);
}

} // namespace vulkan
} // namespace graphics

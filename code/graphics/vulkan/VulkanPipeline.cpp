#include "VulkanPipeline.h"
#include "VulkanRenderState.h"

#include "cfile/cfile.h"

namespace graphics {
namespace vulkan {

// Global pipeline manager pointer
static VulkanPipelineManager* g_pipelineManager = nullptr;

VulkanPipelineManager* getPipelineManager()
{
	return g_pipelineManager;
}

void setPipelineManager(VulkanPipelineManager* manager)
{
	g_pipelineManager = manager;
}

bool PipelineConfig::operator==(const PipelineConfig& other) const
{
	return shaderType == other.shaderType &&
	       shaderFlags == other.shaderFlags &&
	       vertexLayoutHash == other.vertexLayoutHash &&
	       primitiveType == other.primitiveType &&
	       depthMode == other.depthMode &&
	       blendMode == other.blendMode &&
	       cullEnabled == other.cullEnabled &&
	       depthWriteEnabled == other.depthWriteEnabled &&
	       stencilEnabled == other.stencilEnabled &&
	       stencilFunc == other.stencilFunc &&
	       stencilMask == other.stencilMask &&
	       frontStencilOp.stencilFailOperation == other.frontStencilOp.stencilFailOperation &&
	       frontStencilOp.depthFailOperation == other.frontStencilOp.depthFailOperation &&
	       frontStencilOp.successOperation == other.frontStencilOp.successOperation &&
	       backStencilOp.stencilFailOperation == other.backStencilOp.stencilFailOperation &&
	       backStencilOp.depthFailOperation == other.backStencilOp.depthFailOperation &&
	       backStencilOp.successOperation == other.backStencilOp.successOperation &&
	       colorWriteMask.x == other.colorWriteMask.x &&
	       colorWriteMask.y == other.colorWriteMask.y &&
	       colorWriteMask.z == other.colorWriteMask.z &&
	       colorWriteMask.w == other.colorWriteMask.w &&
	       renderPass == other.renderPass &&
	       subpass == other.subpass &&
	       colorAttachmentCount == other.colorAttachmentCount;
}

size_t PipelineConfig::hash() const
{
	size_t h = 0;

	// Combine all fields into hash
	h ^= std::hash<int>()(static_cast<int>(shaderType)) << 0;
	h ^= std::hash<uint32_t>()(shaderFlags) << 4;
	h ^= std::hash<size_t>()(vertexLayoutHash) << 8;
	h ^= std::hash<int>()(static_cast<int>(primitiveType)) << 12;
	h ^= std::hash<int>()(static_cast<int>(depthMode)) << 16;
	h ^= std::hash<int>()(static_cast<int>(blendMode)) << 20;
	h ^= std::hash<bool>()(cullEnabled) << 24;
	h ^= std::hash<bool>()(depthWriteEnabled) << 25;
	h ^= std::hash<bool>()(stencilEnabled) << 26;
	h ^= std::hash<int>()(static_cast<int>(stencilFunc)) << 27;
	h ^= std::hash<uint32_t>()(stencilMask) << 31;
	h ^= std::hash<int>()(static_cast<int>(frontStencilOp.stencilFailOperation)) << 33;
	h ^= std::hash<int>()(static_cast<int>(frontStencilOp.depthFailOperation)) << 35;
	h ^= std::hash<int>()(static_cast<int>(frontStencilOp.successOperation)) << 37;
	h ^= std::hash<int>()(static_cast<int>(backStencilOp.stencilFailOperation)) << 39;
	h ^= std::hash<int>()(static_cast<int>(backStencilOp.depthFailOperation)) << 41;
	h ^= std::hash<int>()(static_cast<int>(backStencilOp.successOperation)) << 43;
	h ^= std::hash<int>()((colorWriteMask.x ? 1 : 0) | (colorWriteMask.y ? 2 : 0) |
	                      (colorWriteMask.z ? 4 : 0) | (colorWriteMask.w ? 8 : 0)) << 44;
	h ^= std::hash<uint64_t>()(reinterpret_cast<uint64_t>(static_cast<VkRenderPass>(renderPass))) << 45;
	h ^= std::hash<uint32_t>()(subpass) << 49;
	h ^= std::hash<uint32_t>()(colorAttachmentCount) << 53;

	return h;
}

bool VulkanPipelineManager::init(vk::Device device, VulkanShaderManager* shaderManager,
                                  VulkanDescriptorManager* descriptorManager)
{
	if (m_initialized) {
		return true;
	}

	m_device = device;
	m_shaderManager = shaderManager;
	m_descriptorManager = descriptorManager;

	// Create empty pipeline cache
	vk::PipelineCacheCreateInfo cacheInfo;
	m_pipelineCache = m_device.createPipelineCacheUnique(cacheInfo);

	// Create common pipeline layout
	createPipelineLayout();

	m_initialized = true;
	mprintf(("VulkanPipelineManager: Initialized\n"));
	return true;
}

void VulkanPipelineManager::shutdown()
{
	if (!m_initialized) {
		return;
	}

	// Wait for device idle
	m_device.waitIdle();

	// Clear all pipelines
	m_pipelines.clear();
	m_pipelineLayout.reset();
	m_pipelineCache.reset();
	m_vertexFormatCache.clear();

	m_initialized = false;
	mprintf(("VulkanPipelineManager: Shutdown complete\n"));
}

vk::Pipeline VulkanPipelineManager::getPipeline(const PipelineConfig& config, const vertex_layout& vertexLayout)
{
	if (!m_initialized) {
		return {};
	}

	// Update vertex layout hash in config
	PipelineConfig fullConfig = config;
	fullConfig.vertexLayoutHash = vertexLayout.hash();

	size_t configHash = fullConfig.hash();

	// Check cache
	auto it = m_pipelines.find(configHash);
	if (it != m_pipelines.end()) {
		return it->second.get();
	}

	// Create new pipeline
	auto pipeline = createPipeline(fullConfig, vertexLayout);
	if (!pipeline) {
		return {};
	}

	vk::Pipeline result = pipeline.get();
	m_pipelines[configHash] = std::move(pipeline);

	nprintf(("Vulkan", "VulkanPipelineManager: Created pipeline for shader type %d (hash 0x%zx)\n",
		static_cast<int>(config.shaderType), configHash));

	return result;
}

bool VulkanPipelineManager::loadPipelineCache(const SCP_string& filename)
{
	// Try to load cache file
	CFILE* fp = cfopen(filename.c_str(), "rb", CF_TYPE_CACHE);
	if (!fp) {
		nprintf(("Vulkan", "VulkanPipelineManager: No pipeline cache file found: %s\n", filename.c_str()));
		return false;
	}

	// Get file size
	int fileSize = cfilelength(fp);
	if (fileSize <= 0) {
		cfclose(fp);
		return false;
	}

	// Read cache data
	SCP_vector<uint8_t> cacheData(fileSize);
	if (cfread(cacheData.data(), 1, fileSize, fp) != fileSize) {
		cfclose(fp);
		return false;
	}
	cfclose(fp);

	// Create new pipeline cache with data
	vk::PipelineCacheCreateInfo cacheInfo;
	cacheInfo.initialDataSize = cacheData.size();
	cacheInfo.pInitialData = cacheData.data();

	try {
		auto newCache = m_device.createPipelineCacheUnique(cacheInfo);
		m_pipelineCache = std::move(newCache);
		mprintf(("VulkanPipelineManager: Loaded pipeline cache: %s (%d bytes)\n",
			filename.c_str(), fileSize));
		return true;
	} catch (const vk::SystemError& e) {
		mprintf(("VulkanPipelineManager: Failed to load pipeline cache: %s\n", e.what()));
		return false;
	}
}

bool VulkanPipelineManager::savePipelineCache(const SCP_string& filename)
{
	if (!m_pipelineCache) {
		return false;
	}

	// Get cache data
	auto cacheData = m_device.getPipelineCacheData(m_pipelineCache.get());
	if (cacheData.empty()) {
		return false;
	}

	// Write to file
	CFILE* fp = cfopen(filename.c_str(), "wb", CF_TYPE_CACHE);
	if (!fp) {
		mprintf(("VulkanPipelineManager: Could not create cache file: %s\n", filename.c_str()));
		return false;
	}

	bool success = (cfwrite(cacheData.data(), 1, static_cast<int>(cacheData.size()), fp) ==
		static_cast<int>(cacheData.size()));
	cfclose(fp);

	if (success) {
		mprintf(("VulkanPipelineManager: Saved pipeline cache: %s (%zu bytes)\n",
			filename.c_str(), cacheData.size()));
	}

	return success;
}

void VulkanPipelineManager::clearPipelineCache()
{
	m_pipelines.clear();
	m_vertexFormatCache.clear();
}

bool VulkanPipelineManager::needsFallbackColor(const vertex_layout& vertexLayout)
{
	const VertexInputConfig& config = m_vertexFormatCache.getVertexInputConfig(vertexLayout);
	return config.needsFallbackColor;
}

bool VulkanPipelineManager::needsFallbackTexCoord(const vertex_layout& vertexLayout)
{
	const VertexInputConfig& config = m_vertexFormatCache.getVertexInputConfig(vertexLayout);
	return config.needsFallbackTexCoord;
}

void VulkanPipelineManager::createPipelineLayout()
{
	// Get descriptor set layouts from descriptor manager
	auto setLayouts = m_descriptorManager->getAllSetLayouts();

	// Optional: Define push constant range for frequently-changing data
	// For now, we rely entirely on uniform buffers
	// vk::PushConstantRange pushConstantRange;
	// pushConstantRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
	// pushConstantRange.offset = 0;
	// pushConstantRange.size = sizeof(mat4); // Example: MVP matrix

	vk::PipelineLayoutCreateInfo layoutInfo;
	layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
	layoutInfo.pSetLayouts = setLayouts.data();
	layoutInfo.pushConstantRangeCount = 0;
	layoutInfo.pPushConstantRanges = nullptr;

	m_pipelineLayout = m_device.createPipelineLayoutUnique(layoutInfo);

	mprintf(("VulkanPipelineManager: Created pipeline layout with %zu descriptor sets\n",
		setLayouts.size()));
}

vk::UniquePipeline VulkanPipelineManager::createPipeline(const PipelineConfig& config,
                                                          const vertex_layout& vertexLayout)
{
	// Get shader modules
	const VulkanShaderModule* shader = m_shaderManager->getShaderByType(config.shaderType, config.shaderFlags);
	if (!shader || !shader->valid) {
		mprintf(("VulkanPipelineManager: Shader not available for type %d flags 0x%x\n",
			static_cast<int>(config.shaderType), config.shaderFlags));
		return {};
	}

	// Debug: Log which shader and vertex layout is being used
	mprintf(("VulkanPipelineManager: Creating pipeline for shader type %d (%s) flags 0x%x\n",
		static_cast<int>(config.shaderType), shader->description.c_str(), config.shaderFlags));
	mprintf(("  Vertex layout has %zu components:\n", vertexLayout.get_num_vertex_components()));
	for (size_t i = 0; i < vertexLayout.get_num_vertex_components(); ++i) {
		const vertex_format_data* comp = vertexLayout.get_vertex_component(i);
		mprintf(("    [%zu] format=%d offset=%zu stride=%zu\n", i,
			static_cast<int>(comp->format_type), comp->offset, comp->stride));
	}

	// Shader stages
	SCP_vector<vk::PipelineShaderStageCreateInfo> shaderStages;

	vk::PipelineShaderStageCreateInfo vertStage;
	vertStage.stage = vk::ShaderStageFlagBits::eVertex;
	vertStage.module = shader->vertexModule.get();
	vertStage.pName = "main";
	shaderStages.push_back(vertStage);

	vk::PipelineShaderStageCreateInfo fragStage;
	fragStage.stage = vk::ShaderStageFlagBits::eFragment;
	fragStage.module = shader->fragmentModule.get();
	fragStage.pName = "main";
	shaderStages.push_back(fragStage);

	if (shader->hasGeometryShader()) {
		vk::PipelineShaderStageCreateInfo geomStage;
		geomStage.stage = vk::ShaderStageFlagBits::eGeometry;
		geomStage.module = shader->geometryModule.get();
		geomStage.pName = "main";
		shaderStages.push_back(geomStage);
	}

	// Vertex input state
	const VertexInputConfig& vertexInputConfig = m_vertexFormatCache.getVertexInputConfig(vertexLayout);

	// Input assembly
	vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
	inputAssembly.topology = convertPrimitiveType(config.primitiveType);
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	// Viewport state (dynamic)
	vk::PipelineViewportStateCreateInfo viewportState;
	viewportState.viewportCount = 1;
	viewportState.pViewports = nullptr;  // Dynamic
	viewportState.scissorCount = 1;
	viewportState.pScissors = nullptr;   // Dynamic

	// Rasterization state
	vk::PipelineRasterizationStateCreateInfo rasterizer = createRasterizationState(config.cullEnabled);

	// Multisample state
	vk::PipelineMultisampleStateCreateInfo multisampling;
	multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;
	multisampling.sampleShadingEnable = VK_FALSE;

	// Depth stencil state
	vk::PipelineDepthStencilStateCreateInfo depthStencil = createDepthStencilState(
		config.depthMode,
		config.stencilEnabled,
		config.stencilFunc,
		config.stencilEnabled ? &config.frontStencilOp : nullptr,
		config.stencilEnabled ? &config.backStencilOp : nullptr,
		config.stencilMask);

	// Override depth write if specified
	if (!config.depthWriteEnabled) {
		depthStencil.depthWriteEnable = VK_FALSE;
	}

	// Color blend state
	SCP_vector<vk::PipelineColorBlendAttachmentState> colorBlendAttachments;
	for (uint32_t i = 0; i < config.colorAttachmentCount; ++i) {
		colorBlendAttachments.push_back(createColorBlendAttachment(config.blendMode, config.colorWriteMask));
	}

	vk::PipelineColorBlendStateCreateInfo colorBlending;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
	colorBlending.pAttachments = colorBlendAttachments.data();

	// Dynamic state
	std::array<vk::DynamicState, 5> dynamicStates = {
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor,
		vk::DynamicState::eLineWidth,
		vk::DynamicState::eDepthBias,
		vk::DynamicState::eStencilReference,
	};

	vk::PipelineDynamicStateCreateInfo dynamicState;
	dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	// Create pipeline
	vk::GraphicsPipelineCreateInfo pipelineInfo;
	pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineInfo.pStages = shaderStages.data();
	pipelineInfo.pVertexInputState = &vertexInputConfig.createInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = m_pipelineLayout.get();
	pipelineInfo.renderPass = config.renderPass;
	pipelineInfo.subpass = config.subpass;
	pipelineInfo.basePipelineHandle = nullptr;
	pipelineInfo.basePipelineIndex = -1;

	try {
		auto result = m_device.createGraphicsPipelineUnique(m_pipelineCache.get(), pipelineInfo);
		return std::move(result.value);
	} catch (const vk::SystemError& e) {
		mprintf(("VulkanPipelineManager: Failed to create pipeline: %s\n", e.what()));
		return {};
	}
}

} // namespace vulkan
} // namespace graphics

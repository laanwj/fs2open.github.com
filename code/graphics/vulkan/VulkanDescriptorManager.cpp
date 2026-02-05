#include "VulkanDescriptorManager.h"

namespace graphics {
namespace vulkan {

// Global descriptor manager pointer
static VulkanDescriptorManager* g_descriptorManager = nullptr;

VulkanDescriptorManager* getDescriptorManager()
{
	return g_descriptorManager;
}

void setDescriptorManager(VulkanDescriptorManager* manager)
{
	g_descriptorManager = manager;
}

bool VulkanDescriptorManager::init(vk::Device device)
{
	if (m_initialized) {
		return true;
	}

	m_device = device;

	createSetLayouts();
	createDescriptorPools();

	m_initialized = true;
	mprintf(("VulkanDescriptorManager: Initialized\n"));
	return true;
}

void VulkanDescriptorManager::shutdown()
{
	if (!m_initialized) {
		return;
	}

	// Wait for device idle before destroying
	m_device.waitIdle();

	// Destroy pools (automatically frees allocated sets)
	m_persistentPool.reset();
	for (auto& pool : m_framePools) {
		pool.reset();
	}

	// Destroy layouts
	for (auto& layout : m_setLayouts) {
		layout.reset();
	}

	m_initialized = false;
	mprintf(("VulkanDescriptorManager: Shutdown complete\n"));
}

vk::DescriptorSetLayout VulkanDescriptorManager::getSetLayout(DescriptorSetIndex setIndex) const
{
	return m_setLayouts[static_cast<size_t>(setIndex)].get();
}

SCP_vector<vk::DescriptorSetLayout> VulkanDescriptorManager::getAllSetLayouts() const
{
	SCP_vector<vk::DescriptorSetLayout> layouts;
	layouts.reserve(static_cast<size_t>(DescriptorSetIndex::Count));

	for (const auto& layout : m_setLayouts) {
		layouts.push_back(layout.get());
	}

	return layouts;
}

vk::DescriptorSet VulkanDescriptorManager::allocateFrameSet(DescriptorSetIndex setIndex)
{
	if (!m_initialized) {
		return {};
	}

	vk::DescriptorSetAllocateInfo allocInfo;
	allocInfo.descriptorPool = m_framePools[m_currentFrame].get();
	allocInfo.descriptorSetCount = 1;
	vk::DescriptorSetLayout layout = m_setLayouts[static_cast<size_t>(setIndex)].get();
	allocInfo.pSetLayouts = &layout;

	try {
		auto sets = m_device.allocateDescriptorSets(allocInfo);
		return sets[0];
	} catch (const vk::SystemError& e) {
		mprintf(("VulkanDescriptorManager: Failed to allocate frame descriptor set: %s\n", e.what()));
		return {};
	}
}

vk::DescriptorSet VulkanDescriptorManager::allocatePersistentSet(DescriptorSetIndex setIndex)
{
	if (!m_initialized) {
		return {};
	}

	vk::DescriptorSetAllocateInfo allocInfo;
	allocInfo.descriptorPool = m_persistentPool.get();
	allocInfo.descriptorSetCount = 1;
	vk::DescriptorSetLayout layout = m_setLayouts[static_cast<size_t>(setIndex)].get();
	allocInfo.pSetLayouts = &layout;

	try {
		auto sets = m_device.allocateDescriptorSets(allocInfo);
		return sets[0];
	} catch (const vk::SystemError& e) {
		mprintf(("VulkanDescriptorManager: Failed to allocate persistent descriptor set: %s\n", e.what()));
		return {};
	}
}

void VulkanDescriptorManager::updateUniformBuffer(vk::DescriptorSet set, uint32_t binding,
                                                   vk::Buffer buffer, vk::DeviceSize offset, vk::DeviceSize range)
{
	if (!buffer) {
		mprintf(("VulkanDescriptorManager: Skipping null buffer for binding %u\n", binding));
		return;
	}

	vk::DescriptorBufferInfo bufferInfo;
	bufferInfo.buffer = buffer;
	bufferInfo.offset = offset;
	bufferInfo.range = range;

	vk::WriteDescriptorSet write;
	write.dstSet = set;
	write.dstBinding = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType = vk::DescriptorType::eUniformBuffer;  // Regular UBO, not dynamic
	write.pBufferInfo = &bufferInfo;

	m_device.updateDescriptorSets(1, &write, 0, nullptr);
}

void VulkanDescriptorManager::updateTexture(vk::DescriptorSet set, uint32_t binding,
                                            vk::ImageView imageView, vk::Sampler sampler,
                                            vk::ImageLayout layout)
{
	vk::DescriptorImageInfo imageInfo;
	imageInfo.imageView = imageView;
	imageInfo.sampler = sampler;
	imageInfo.imageLayout = layout;

	vk::WriteDescriptorSet write;
	write.dstSet = set;
	write.dstBinding = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
	write.pImageInfo = &imageInfo;

	m_device.updateDescriptorSets(1, &write, 0, nullptr);
}

void VulkanDescriptorManager::updateTextureArray(vk::DescriptorSet set, uint32_t binding,
                                                  const SCP_vector<vk::DescriptorImageInfo>& images)
{
	if (images.empty()) {
		return;
	}

	vk::WriteDescriptorSet write;
	write.dstSet = set;
	write.dstBinding = binding;
	write.dstArrayElement = 0;
	write.descriptorCount = static_cast<uint32_t>(images.size());
	write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
	write.pImageInfo = images.data();

	m_device.updateDescriptorSets(1, &write, 0, nullptr);
}

void VulkanDescriptorManager::beginFrame()
{
	if (!m_initialized) {
		return;
	}

	// Reset the current frame's pool
	m_device.resetDescriptorPool(m_framePools[m_currentFrame].get());
}

void VulkanDescriptorManager::endFrame()
{
	// Advance to next frame
	m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

bool VulkanDescriptorManager::getUniformBlockBinding(uniform_block_type blockType,
                                                      DescriptorSetIndex& setIndex, uint32_t& binding)
{
	// Map uniform_block_type to descriptor set and binding
	// Based on the descriptor layout design in the plan
	switch (blockType) {
	case uniform_block_type::Lights:
		setIndex = DescriptorSetIndex::Global;
		binding = 0;
		return true;

	case uniform_block_type::DeferredGlobals:
		setIndex = DescriptorSetIndex::Global;
		binding = 1;
		return true;

	case uniform_block_type::ModelData:
		setIndex = DescriptorSetIndex::Material;
		binding = 0;
		return true;

	case uniform_block_type::DecalGlobals:
		setIndex = DescriptorSetIndex::Material;
		binding = 2;
		return true;

	case uniform_block_type::GenericData:
		setIndex = DescriptorSetIndex::PerDraw;
		binding = 0;
		return true;

	case uniform_block_type::Matrices:
		setIndex = DescriptorSetIndex::PerDraw;
		binding = 1;
		return true;

	case uniform_block_type::NanoVGData:
		setIndex = DescriptorSetIndex::PerDraw;
		binding = 2;
		return true;

	case uniform_block_type::DecalInfo:
		setIndex = DescriptorSetIndex::PerDraw;
		binding = 3;
		return true;

	case uniform_block_type::MovieData:
		setIndex = DescriptorSetIndex::PerDraw;
		binding = 4;
		return true;

	default:
		return false;
	}
}

void VulkanDescriptorManager::createSetLayouts()
{
	// Set 0: Global (per-frame data)
	// NOTE: Using regular UBOs for now; dynamic UBOs need offset tracking
	{
		SCP_vector<DescriptorBindingInfo> bindings = {
			// Binding 0: Lights UBO
			{ 0, vk::DescriptorType::eUniformBuffer, 1,
			  vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },

			// Binding 1: DeferredGlobals UBO
			{ 1, vk::DescriptorType::eUniformBuffer, 1,
			  vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },

			// Binding 2: Shadow map texture
			{ 2, vk::DescriptorType::eCombinedImageSampler, 1,
			  vk::ShaderStageFlagBits::eFragment },

			// Binding 3: Environment/irradiance maps
			{ 3, vk::DescriptorType::eCombinedImageSampler, 1,
			  vk::ShaderStageFlagBits::eFragment },
		};
		m_setLayouts[static_cast<size_t>(DescriptorSetIndex::Global)] = createSetLayout(bindings);
	}

	// Set 1: Material (per-batch data)
	{
		SCP_vector<DescriptorBindingInfo> bindings = {
			// Binding 0: ModelData UBO
			{ 0, vk::DescriptorType::eUniformBuffer, 1,
			  vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },

			// Binding 1: Texture array (diffuse, glow, spec, normal, ambient, misc, etc.)
			{ 1, vk::DescriptorType::eCombinedImageSampler, MAX_TEXTURE_BINDINGS,
			  vk::ShaderStageFlagBits::eFragment },

			// Binding 2: DecalGlobals UBO
			{ 2, vk::DescriptorType::eUniformBuffer, 1,
			  vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },
		};
		m_setLayouts[static_cast<size_t>(DescriptorSetIndex::Material)] = createSetLayout(bindings);
	}

	// Set 2: Per-Draw (per-draw-call data)
	{
		SCP_vector<DescriptorBindingInfo> bindings = {
			// Binding 0: GenericData UBO
			{ 0, vk::DescriptorType::eUniformBuffer, 1,
			  vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },

			// Binding 1: Matrices UBO
			{ 1, vk::DescriptorType::eUniformBuffer, 1,
			  vk::ShaderStageFlagBits::eVertex },

			// Binding 2: NanoVGData UBO
			{ 2, vk::DescriptorType::eUniformBuffer, 1,
			  vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },

			// Binding 3: DecalInfo UBO
			{ 3, vk::DescriptorType::eUniformBuffer, 1,
			  vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment },

			// Binding 4: MovieData UBO
			{ 4, vk::DescriptorType::eUniformBuffer, 1,
			  vk::ShaderStageFlagBits::eFragment },
		};
		m_setLayouts[static_cast<size_t>(DescriptorSetIndex::PerDraw)] = createSetLayout(bindings);
	}

	mprintf(("VulkanDescriptorManager: Created %zu descriptor set layouts\n",
		static_cast<size_t>(DescriptorSetIndex::Count)));
}

void VulkanDescriptorManager::createDescriptorPools()
{
	// Pool sizes - estimate maximum descriptors per frame
	// These may need tuning based on actual usage
	constexpr uint32_t MAX_SETS_PER_FRAME = 1000;
	constexpr uint32_t MAX_UNIFORM_BUFFERS = 5000;
	constexpr uint32_t MAX_SAMPLERS = 2000;

	SCP_vector<vk::DescriptorPoolSize> poolSizes = {
		{ vk::DescriptorType::eUniformBuffer, MAX_UNIFORM_BUFFERS },
		{ vk::DescriptorType::eUniformBufferDynamic, MAX_UNIFORM_BUFFERS },
		{ vk::DescriptorType::eCombinedImageSampler, MAX_SAMPLERS },
	};

	// Create per-frame pools
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		vk::DescriptorPoolCreateInfo poolInfo;
		poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
		poolInfo.maxSets = MAX_SETS_PER_FRAME;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();

		m_framePools[i] = m_device.createDescriptorPoolUnique(poolInfo);
	}

	// Create persistent pool (smaller, for long-lived sets)
	constexpr uint32_t PERSISTENT_MAX_SETS = 100;
	vk::DescriptorPoolCreateInfo persistentPoolInfo;
	persistentPoolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
	persistentPoolInfo.maxSets = PERSISTENT_MAX_SETS;
	persistentPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	persistentPoolInfo.pPoolSizes = poolSizes.data();

	m_persistentPool = m_device.createDescriptorPoolUnique(persistentPoolInfo);

	mprintf(("VulkanDescriptorManager: Created %u frame pools + 1 persistent pool\n",
		MAX_FRAMES_IN_FLIGHT));
}

vk::UniqueDescriptorSetLayout VulkanDescriptorManager::createSetLayout(
	const SCP_vector<DescriptorBindingInfo>& bindings)
{
	SCP_vector<vk::DescriptorSetLayoutBinding> vkBindings;
	vkBindings.reserve(bindings.size());

	for (const auto& info : bindings) {
		vk::DescriptorSetLayoutBinding binding;
		binding.binding = info.binding;
		binding.descriptorType = info.type;
		binding.descriptorCount = info.count;
		binding.stageFlags = info.stages;
		binding.pImmutableSamplers = nullptr;
		vkBindings.push_back(binding);
	}

	vk::DescriptorSetLayoutCreateInfo layoutInfo;
	layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
	layoutInfo.pBindings = vkBindings.data();

	return m_device.createDescriptorSetLayoutUnique(layoutInfo);
}

} // namespace vulkan
} // namespace graphics

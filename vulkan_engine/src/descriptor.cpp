#include "descriptor.h"
#include <checkVkResult.h>

void UberDescriptorSetManager::add_layout_bindings(uint32_t binding, VkDescriptorType type, uint32_t count, VkShaderStageFlags stages, VkSampler* immutableSamplers) {
	VkDescriptorSetLayoutBinding layoutBinding{};
	layoutBinding.binding = binding;
	layoutBinding.descriptorType = type;
	layoutBinding.descriptorCount = count;
	layoutBinding.stageFlags = stages;
	layoutBinding.pImmutableSamplers = immutableSamplers;

	bindings.push_back(layoutBinding);
}

void UberDescriptorSetManager::clear_bindings() {
	poolSizeCounts = {};
	bindings.clear();
}

void UberDescriptorSetManager::generateLayout(VkDevice device, VkDescriptorSetLayoutCreateFlags flags, void* pnext) {
	//Specify Pool Sizes 
	poolSizeCounts = {};
	for (VkDescriptorSetLayoutBinding& binding : bindings) {
		if (binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER)
			poolSizeCounts.samplerCount += binding.descriptorCount;
		else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			poolSizeCounts.combinedImageSamplerCount += binding.descriptorCount;
		else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
			poolSizeCounts.sampledImageCount += binding.descriptorCount;
		else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
			poolSizeCounts.storageImageCount += binding.descriptorCount;
		else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER)
			poolSizeCounts.uniformTexelBufferCount += binding.descriptorCount;
		else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER)
			poolSizeCounts.storageTexelBufferCount += binding.descriptorCount;
		else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
			poolSizeCounts.uniformBufferCount += binding.descriptorCount;
		else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
			poolSizeCounts.storageBufferCount += binding.descriptorCount;
		else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
			poolSizeCounts.dynamicUniformBufferCount += binding.descriptorCount;
		else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
			poolSizeCounts.dynamicStorageBufferCount += binding.descriptorCount;
		else if (binding.descriptorType == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
			poolSizeCounts.inputAttachmentCount += binding.descriptorCount;
		else
			throw std::runtime_error("Failed to Generate Layout as one of the bindings is an unidentifiable Descriptor Type");
	}

	//Create Descriptor Set Layout
	if (layout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(device, layout, nullptr);

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings = bindings.data();
	layoutInfo.flags = flags;

	if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		throw std::runtime_error("Failed to craate Descriptor Set Layout.");
}

void UberDescriptorSetManager::generateSet(VkDevice device) {
	if (layout == VK_NULL_HANDLE)
		throw std::runtime_error("Can't generate Descriptor Set, missing Descriptor Set Layout.");
	if (pool == VK_NULL_HANDLE)
		generatePool(device);

	if (set != VK_NULL_HANDLE)
		vkResetDescriptorPool(device, pool, 0);

	VkDescriptorSetAllocateInfo allocinfo{};
	allocinfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocinfo.descriptorPool = pool;
	allocinfo.descriptorSetCount = 1;
	allocinfo.pSetLayouts = &layout;

	VkResult result = vkAllocateDescriptorSets(device, &allocinfo, &set);
	if (result != VK_SUCCESS) {
		if (result == VK_ERROR_OUT_OF_POOL_MEMORY) {
			throw std::runtime_error("failed to allocate Descriptor Set due to Pool running out of memory");
		}
		else
			throw std::runtime_error("Failed to create Descriptor Set for some other reasion other than running out of memory in pool");
	}

	writer.dstSet = set;
}

void UberDescriptorSetManager::destroyResources(VkDevice device) {
	vkDestroyDescriptorSetLayout(device, layout, nullptr);
	vkDestroyDescriptorPool(device, pool, nullptr);
}

void UberDescriptorSetManager::updateSet(VkDevice device, uint32_t binding, VkDescriptorType type, VkBuffer& buffer, VkDeviceSize range, uint32_t arrayOffset, uint32_t count) {
	VkDescriptorBufferInfo bufferInfo{};
	bufferInfo.buffer = buffer;
	bufferInfo.offset = 0;
	bufferInfo.range = range;

	writer.descriptorType = type;
	writer.dstBinding = binding;
	writer.dstArrayElement = arrayOffset;
	writer.descriptorCount = count;
	writer.pBufferInfo = &bufferInfo;
	writer.pImageInfo = nullptr;

	vkUpdateDescriptorSets(device, 1, &writer, 0, nullptr);
}

void UberDescriptorSetManager::generatePool(VkDevice device) {
	std::vector<VkDescriptorPoolSize> poolSizes;
	
	VkDescriptorPoolSize poolSize{};
	if (poolSizeCounts.samplerCount > 0) {
		poolSize.type = VK_DESCRIPTOR_TYPE_SAMPLER;
		poolSize.descriptorCount = poolSizeCounts.samplerCount;
		poolSizes.push_back(poolSize);
	}
	if (poolSizeCounts.combinedImageSamplerCount > 0) {
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = poolSizeCounts.combinedImageSamplerCount;
		poolSizes.push_back(poolSize);
	}
	if (poolSizeCounts.sampledImageCount > 0) {
		poolSize.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		poolSize.descriptorCount = poolSizeCounts.sampledImageCount;
		poolSizes.push_back(poolSize);
	}
	if (poolSizeCounts.storageImageCount > 0) {
		poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		poolSize.descriptorCount = poolSizeCounts.storageImageCount;
		poolSizes.push_back(poolSize);
	}
	if (poolSizeCounts.uniformTexelBufferCount > 0) {
		poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		poolSize.descriptorCount = poolSizeCounts.uniformTexelBufferCount;
		poolSizes.push_back(poolSize);
	}
	if (poolSizeCounts.storageTexelBufferCount > 0) {
		poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
		poolSize.descriptorCount = poolSizeCounts.storageTexelBufferCount;
		poolSizes.push_back(poolSize);
	}
	if (poolSizeCounts.uniformBufferCount > 0) {
		poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSize.descriptorCount = poolSizeCounts.uniformBufferCount;
		poolSizes.push_back(poolSize);
	}
	if (poolSizeCounts.storageBufferCount > 0) {
		poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		poolSize.descriptorCount = poolSizeCounts.storageBufferCount;
		poolSizes.push_back(poolSize);
	}
	if (poolSizeCounts.dynamicUniformBufferCount > 0) {
		poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		poolSize.descriptorCount = poolSizeCounts.dynamicUniformBufferCount;
		poolSizes.push_back(poolSize);
	}
	if (poolSizeCounts.dynamicStorageBufferCount > 0) {
		poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
		poolSize.descriptorCount = poolSizeCounts.dynamicStorageBufferCount;
		poolSizes.push_back(poolSize);
	}
	if (poolSizeCounts.inputAttachmentCount > 0) {
		poolSize.type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
		poolSize.descriptorCount = poolSizeCounts.inputAttachmentCount;
		poolSizes.push_back(poolSize);
	}

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets - 1;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create Descriptor Pool");
}

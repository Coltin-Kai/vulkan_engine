#pragma once

#include "vulkan/vulkan.h"

#include <vector>

//Considering refactoring toto a better design. Not used yet.

struct UberDescriptorSetManager {
	struct PoolSizeCounts {
		uint32_t samplerCount = 0;
		uint32_t combinedImageSamplerCount = 0;
		uint32_t sampledImageCount = 0;
		uint32_t storageImageCount = 0;
		uint32_t uniformTexelBufferCount = 0;
		uint32_t storageTexelBufferCount = 0;
		uint32_t uniformBufferCount = 0;
		uint32_t storageBufferCount = 0;
		uint32_t dynamicUniformBufferCount = 0;
		uint32_t dynamicStorageBufferCount = 0;
		uint32_t inputAttachmentCount = 0;
	};

	VkDescriptorSet set = VK_NULL_HANDLE;
	VkDescriptorPool pool = VK_NULL_HANDLE;
	VkDescriptorSetLayout layout = VK_NULL_HANDLE;
	std::vector<VkDescriptorSetLayoutBinding> bindings;
	PoolSizeCounts poolSizeCounts;
	VkWriteDescriptorSet writer{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	
	void add_layout_bindings(uint32_t binding, VkDescriptorType type, uint32_t count, VkShaderStageFlags stages, VkSampler* immutableSamplers);
	void clear_bindings();
	void generateLayout(VkDevice device, VkDescriptorSetLayoutCreateFlags flags, void* pnext = nullptr);
	void generateSet(VkDevice device);
	void destroyResources(VkDevice device);
	void updateSet(VkDevice device, uint32_t binding, VkDescriptorType type, VkBuffer& buffer, VkDeviceSize range, uint32_t arrayOffset = 0, uint32_t count = 1);
private:
	void generatePool(VkDevice device);
};


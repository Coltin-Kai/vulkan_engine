#pragma once

#include "vulkan/vulkan.h"
#include "vk_mem_alloc.h"

struct AllocatedImage {
	VkImage image;
	VkImageView imageView;
	VmaAllocation allocation;
	VkExtent3D extent;
	VkFormat format;

	//void destroy(const VkDevice& device, const VmaAllocator& allocator) {} Unsure if should move deletion operations from engine class to struct types themselves
};

struct AllocatedBuffer {
	VkBuffer buffer;
	VmaAllocation allocation;
	VmaAllocationInfo info;

	void copy_to(void* data, size_t offset, size_t size) {
		memcpy(info.pMappedData, data, size);
	}
};

//Maybe create a allocatedbuffer struct that allows easy suballocation. Maybe.like a dynamic allocated buffer
struct DynamicAllocatedBuffer {
	AllocatedBuffer alloc_buffer;
};



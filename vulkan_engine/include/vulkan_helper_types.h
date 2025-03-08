#pragma once

#include "vulkan/vulkan.h"
#include "vk_mem_alloc.h"
#include <unordered_set>

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

struct DataRegion { //Used to keep track of data and its space inside a buffer. Specificallyy those that havee varied sizes
	size_t offset; //Offset of region in bytes
	size_t size; //SIze of region in bytes
};

//Maybe create a allocatedbuffer struct that allows easy suballocation. Maybe.like a dynamic allocated buffer
struct DynamicAllocatedBuffer {
	AllocatedBuffer alloc_buffer;
};

//Just a wrapper to allow descructor calling the VkDestroySampler
//struct Sampler
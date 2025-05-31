#pragma once

#include "vulkan/vulkan.h"
#include "vk_mem_alloc.h"
#include <unordered_set>

struct AllocatedImage {
	VkImage image;
	VkImageView imageView;
	VmaAllocation allocation;
	VmaAllocationInfo info;
	VkExtent3D extent;
	VkFormat format;
};

struct Image { //Image that is not allocated using VMA
	VkImage image;
	VkImageView imageView;
	VkExtent3D extent;
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

//Just a wrapper to allow descructor calling the VkDestroySampler
//struct Sampler
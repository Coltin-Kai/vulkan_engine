#pragma once

#include "vulkan/vulkan.h"
#include "vk_mem_alloc.h"
#include <unordered_set>

//Note: Might change so that Image View is different than Image. So that it can allow multiple different imageviews for a single image for example

struct Image { 
	VkImage image;
	VkImageView imageView;
	VkExtent3D extent;
	VkFormat format;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED; //Note this only defines the whole layout of the entire image. ANy attempt to transition layout of subresources within the image can make this this symbol inaccurate as subresources would have conflicting layouts
};

//Image that is allocated with VMA
struct AllocatedImage : Image {
	VmaAllocation allocation;
	VmaAllocationInfo info;
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
#pragma once

#include "thirdparty_defines.h"

#include "SDL.h"
#include "SDL_vulkan.h"

#include "vulkan/vulkan.h"
#include "vk_mem_alloc.h"
#include "VkBootstrap.h"

#include "vulkan_helper_types.h"
#include "vulkan_helper_functions.h"
#include "checkVkResult.h"

class VulkanContext {
public:
	//Vulkan
	VkInstance instance;
	VkSurfaceKHR surface;
	VkDebugUtilsMessengerEXT debug_messenger;
	VkPhysicalDevice physicalDevice;
	VkDevice device;
	uint32_t graphicsQueueFamily;
	VkQueue graphicsQueue;

	//VMA
	VmaAllocator allocator;

	//Immediate Commands
	VkCommandPool immCommandPool;
	VkCommandBuffer immCommandBuffer;
	VkFence immFence;

	void init(SDL_Window* window);
	void shutdown();

	//Immediate Command
	VkCommandBuffer start_immediate_recording();
	void submit_immediate_commands();

	//Buffer
	AllocatedBuffer create_buffer(const char* name, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags);
	AllocatedBuffer create_buffer(const char* name, VkBufferCreateInfo bufferInfo, VmaAllocationCreateInfo allocInfo); //WHen Buffer Creation requires more specific details
	void destroy_buffer(const AllocatedBuffer& buffer);
	void update_buffer(const AllocatedBuffer& buffer, void* srcData, size_t srcDataSize, VkBufferCopy& copyInfo);
	void update_buffer(const AllocatedBuffer& buffer, void* srcData, size_t srcDataSize, std::vector<VkBufferCopy>& copyInfos);

	//Image
	AllocatedImage create_image(const char* name, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped); //Only allocates image on GPU
	AllocatedImage create_image(const char* name, VkImageCreateInfo imageInfo, VmaAllocationCreateInfo allocInfo); //When Image Create requires more specific details
	void destroy_image(const AllocatedImage& image);
	void update_image(const AllocatedImage& image, void* srcData, VkExtent3D imageSize); //Uploads raw data to an image

	//Sampler
	VkSampler create_sampler(VkSamplerCreateInfo& samplerCreateInfo);
	void destroy_sampler(const VkSampler& sampler);
};
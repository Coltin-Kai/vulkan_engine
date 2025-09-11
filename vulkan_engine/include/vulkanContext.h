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
	uint32_t primaryQueueFamily;
	VkQueue primaryQueue;

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
	void update_buffer(const AllocatedBuffer& buffer, void* srcData, size_t srcDataSize, VkBufferCopy& copyInfo); //srcDataSize param maybe kind of redundant idk
	void update_buffer(const AllocatedBuffer& buffer, void* srcData, size_t srcDataSize, std::vector<VkBufferCopy>& copyInfos); //!!!Might combine both variants by using size and pointer params like updateImage

	//Image
	AllocatedImage create_image(const char* name, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped); //Only allocates 2D images on GPU normally used just for textures
	AllocatedImage create_image(const char* name, VkImageCreateInfo imageInfo, VmaAllocationCreateInfo allocInfo, VkImageViewCreateInfo imageViewInfo); //When Image Creation requires more specific details
	void destroy_image(const AllocatedImage& image);
	void update_image(AllocatedImage& image, void* srcData, size_t dataSize); //Uploads raw data to an image. !!!Might change param to implement specific settings like vkbufferimagecopy param
	void update_image(AllocatedImage& dstImage, AllocatedImage& srcImage, uint32_t copyCount, const VkImageCopy* copyInfo);

	void transition_image(VkCommandBuffer cmd, Image& image, VkImageLayout targetLayout);

	void generate_mipmaps(VkCommandBuffer cmd, AllocatedImage& image, uint32_t levelCount, uint32_t layerCount);

	//Sampler
	VkSampler create_sampler(VkSamplerCreateInfo& samplerCreateInfo);
	void destroy_sampler(const VkSampler& sampler);
};
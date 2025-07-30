#include "vulkanContext.h"

void VulkanContext::init(SDL_Window* window) {
	//Create Instance
	vkb::InstanceBuilder builder;

	builder.set_app_name("Vulkan Engine");
	builder.request_validation_layers(true);
	builder.use_default_debug_messenger();
	builder.require_api_version(1, 3, 0);

	auto inst_result = builder.build();
	vkb::Instance vkb_inst = inst_result.value();

	instance = vkb_inst.instance;
	debug_messenger = vkb_inst.debug_messenger;

	//Create Surface
	SDL_Vulkan_CreateSurface(window, instance, &surface);

	//Get Physical Device and Features
	VkPhysicalDeviceVulkan13Features features3{};
	features3.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features3.dynamicRendering = true;
	features3.synchronization2 = true;

	VkPhysicalDeviceVulkan12Features features2{};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

	features2.scalarBlockLayout = true;

	features2.bufferDeviceAddress = true;

	features2.descriptorIndexing = true;
	features2.shaderUniformBufferArrayNonUniformIndexing = true;
	features2.shaderStorageBufferArrayNonUniformIndexing = true;
	features2.shaderSampledImageArrayNonUniformIndexing = true;
	features2.shaderStorageImageArrayNonUniformIndexing = true;

	features2.descriptorBindingVariableDescriptorCount = true;
	features2.runtimeDescriptorArray = true;

	features2.descriptorBindingPartiallyBound = true;

	features2.descriptorBindingUniformBufferUpdateAfterBind = true;
	features2.descriptorBindingStorageBufferUpdateAfterBind = true;
	features2.descriptorBindingSampledImageUpdateAfterBind = true;
	features2.descriptorBindingStorageImageUpdateAfterBind = true;

	features2.descriptorBindingUpdateUnusedWhilePending = true;

	VkPhysicalDeviceVulkan11Features features1{};
	features1.shaderDrawParameters = true;

	VkPhysicalDeviceFeatures features{};
	features.multiDrawIndirect = true;

	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	selector.set_minimum_version(1, 3);
	selector.set_required_features_13(features3);
	selector.set_required_features_12(features2);
	selector.set_required_features_11(features1);
	selector.set_required_features(features);
	selector.set_surface(surface);

	auto physical_device_selector_return = selector.select();

	if (!physical_device_selector_return) {
		std::cout << std::format("Physical Device Selector Error: {}", physical_device_selector_return.error().message()) << std::endl;
		throw std::runtime_error("Failed to select Physical Device");
	}

	vkb::PhysicalDevice vkbPhysicalDevice = physical_device_selector_return.value();

	//Create Logical Device
	vkb::DeviceBuilder deviceBuilder{ vkbPhysicalDevice };

	vkb::Device vkbDevice = deviceBuilder.build().value();

	device = vkbDevice.device;
	physicalDevice = vkbPhysicalDevice.physical_device;
	graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	//Create VMA
	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.physicalDevice = physicalDevice;
	allocatorInfo.device = device;
	allocatorInfo.instance = instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &allocator);

	//Immediate Command
	VkCommandPoolCreateInfo cmdPoolInfo{};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.pNext = nullptr;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cmdPoolInfo.queueFamilyIndex = graphicsQueueFamily;

	VK_CHECK(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &immCommandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo{};
	cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAllocInfo.pNext = nullptr;
	cmdAllocInfo.commandPool = immCommandPool;
	cmdAllocInfo.commandBufferCount = 1;
	cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VK_CHECK(vkAllocateCommandBuffers(device, &cmdAllocInfo, &immCommandBuffer));

	//-Init Immediate Command Sync Structure
	VkFenceCreateInfo fenceCreateInfo{};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &immFence));
}

void VulkanContext::shutdown() {
	//Cleanup Commands and Sync Structures
	vkDestroyCommandPool(device, immCommandPool, nullptr);
	vkDestroyFence(device, immFence, nullptr);

	//Cleanup VMA and Vulkan
	vmaDestroyAllocator(allocator);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyDevice(device, nullptr);
	vkb::destroy_debug_utils_messenger(instance, debug_messenger);
	vkDestroyInstance(instance, nullptr);
}

VkCommandBuffer VulkanContext::start_immediate_recording() {
	VK_CHECK(vkResetFences(device, 1, &immFence));
	VK_CHECK(vkResetCommandBuffer(immCommandBuffer, 0));

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(immCommandBuffer, &cmdBeginInfo));
	return immCommandBuffer;
}

void VulkanContext::submit_immediate_commands() {
	VK_CHECK(vkEndCommandBuffer(immCommandBuffer));

	VkCommandBufferSubmitInfo cmdInfo = vkutil::command_buffer_submit_info(immCommandBuffer);
	VkSubmitInfo2 submit = vkutil::submit_info(&cmdInfo, nullptr, nullptr);

	VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submit, immFence));

	VK_CHECK(vkWaitForFences(device, 1, &immFence, true, 9999999999));
}

/*
VMA ALlocation Flags Tips:
	GPU Only - Usage: Auto/Auto prefer Device, Flags: None or Dedicated_Memory if buffer is large like attachment
	Staging Buffer - Usage: Auto/Auto prefer Host, Flags: Create_Mapped & Host_Access_Sequential_Write to ensure both host visbile and wirte-combined
	Readback - Usage: Auto/Auto prefer Host, Flags: Create_Mapped & Host_Access_Random to allow host visible and caching.
	Dynamic Buffers (Frequently updated by CPU and read by GPU - Usage: Auto, Flags: Create_Mapped & Host_Access_Sequential_Write & Host_Access_Allow_Transfer_Instead to have vma decided if host visible or device local based on performance.
*/
AllocatedBuffer VulkanContext::create_buffer(const char* name, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags) {
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;
	if (allocFlags & VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT) //Makes sure that if set with this flag, sets up Transfer_DST flag as chance for buffer to end up as HOst
		bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	else
		bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = memoryUsage;
	vmaAllocInfo.flags = allocFlags;
	AllocatedBuffer newBuffer;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));
	vmaSetAllocationName(allocator, newBuffer.allocation, name);

	return newBuffer;
}

AllocatedBuffer VulkanContext::create_buffer(const char* name, VkBufferCreateInfo bufferInfo, VmaAllocationCreateInfo allocInfo) {
	AllocatedBuffer newBuffer;
	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));
	vmaSetAllocationName(allocator, newBuffer.allocation, name);

	return newBuffer;
}

void VulkanContext::destroy_buffer(const AllocatedBuffer& buffer) {
	vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
}

void VulkanContext::update_buffer(const AllocatedBuffer& buffer, void* srcData, size_t srcDataSize, VkBufferCopy& copyInfo) {
	VkMemoryPropertyFlags buffer_memProperties;
	vmaGetAllocationMemoryProperties(allocator, buffer.allocation, &buffer_memProperties);
	
	if (buffer_memProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		unsigned char* srcPointer = static_cast<unsigned char*>(srcData);
		unsigned char* dstPointer = static_cast<unsigned char*>(buffer.info.pMappedData);
		srcPointer = srcPointer + copyInfo.srcOffset;
		dstPointer = dstPointer + copyInfo.dstOffset;
		memcpy(dstPointer, srcPointer, copyInfo.size);
	}
	else if (buffer_memProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
		AllocatedBuffer stagingBuffer = create_buffer("Buffer Update Stager", srcDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		memcpy(stagingBuffer.info.pMappedData, srcData, srcDataSize);

		VkCommandBuffer cmd = start_immediate_recording();
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, buffer.buffer, 1, &copyInfo);
		submit_immediate_commands();

		destroy_buffer(stagingBuffer);
	}
	else {
		throw std::runtime_error("Buffer Memory Property not a MyDevice Local or Host Visible");
	}
}

void VulkanContext::update_buffer(const AllocatedBuffer& buffer, void* srcData, size_t srcDataSize, std::vector<VkBufferCopy>& copyInfos) {
	VkMemoryPropertyFlags buffer_memProperties;
	vmaGetAllocationMemoryProperties(allocator, buffer.allocation, &buffer_memProperties);

	if (buffer_memProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		for (VkBufferCopy& copyInfo : copyInfos) {
			unsigned char* srcPointer = static_cast<unsigned char*>(srcData);
			unsigned char* dstPointer = static_cast<unsigned char*>(buffer.info.pMappedData);
			srcPointer = srcPointer + copyInfo.srcOffset;
			dstPointer = dstPointer + copyInfo.dstOffset;
			memcpy(dstPointer, srcPointer, copyInfo.size);
		}
	}
	else if (buffer_memProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
		AllocatedBuffer stagingBuffer = create_buffer("Buffer Update Stager", srcDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		memcpy(stagingBuffer.info.pMappedData, srcData, srcDataSize);

		VkCommandBuffer cmd = start_immediate_recording();
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, buffer.buffer, copyInfos.size(), copyInfos.data());
		submit_immediate_commands();

		destroy_buffer(stagingBuffer);
	}
	else {
		throw std::runtime_error("Buffer Memory Property not a Device Local or Host Visible");
	}
}

AllocatedImage VulkanContext::create_image(const char* name, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
	AllocatedImage newImage;
	newImage.format = format;
	newImage.extent = size;

	VkImageCreateInfo img_info = vkutil::image_create_info(format, usage, size);

	if (mipmapped)
		img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;

	VmaAllocationCreateInfo alloc_info{};
	alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	alloc_info.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//Allocate and Create Image
	VK_CHECK(vmaCreateImage(allocator, &img_info, &alloc_info, &newImage.image, &newImage.allocation, nullptr));
	vmaSetAllocationName(allocator, newImage.allocation, name);

	VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
	if (format == VK_FORMAT_D32_SFLOAT)
		aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;

	VkImageViewCreateInfo view_info = vkutil::imageview_create_info(format, newImage.image, aspectFlag);
	view_info.subresourceRange.levelCount = img_info.mipLevels;

	VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &newImage.imageView));

	return newImage;
}

AllocatedImage VulkanContext::create_image(const char* name, VkImageCreateInfo imageInfo, VmaAllocationCreateInfo allocInfo, VkImageViewCreateInfo imageViewInfo) {
	AllocatedImage newImage;
	newImage.format = imageInfo.format;
	newImage.extent = imageInfo.extent;

	VK_CHECK(vmaCreateImage(allocator, &imageInfo, &allocInfo, &newImage.image, &newImage.allocation, nullptr));
	vmaSetAllocationName(allocator, newImage.allocation, name);

	VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
	if (newImage.format == VK_FORMAT_D32_SFLOAT)
		aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;

	imageViewInfo.image = newImage.image;

	VK_CHECK(vkCreateImageView(device, &imageViewInfo, nullptr, &newImage.imageView));

	return newImage;
}

void VulkanContext::destroy_image(const AllocatedImage& image) {
	vkDestroyImageView(device, image.imageView, nullptr);
	vmaDestroyImage(allocator, image.image, image.allocation);
}

void VulkanContext::update_image(const AllocatedImage& image, void* srcData, VkExtent3D imageSize) {
	size_t dataSize;
	//Specifies Datasize based on Image Format using 8 bytes or 16 bytes. Tbh really only supports two formats. Might change it so that AllocatedImage specifies the size of its channels instead of checking the format since so many
	if (image.format == VK_FORMAT_R16G16B16A16_SFLOAT)
		dataSize = imageSize.width * imageSize.height * imageSize.depth * 4 * 2;
	else
		dataSize = imageSize.width * imageSize.height * imageSize.depth * 4;

	VkMemoryPropertyFlags image_memProperties;
	vmaGetAllocationMemoryProperties(allocator, image.allocation, &image_memProperties);

	if (image_memProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		memcpy(image.info.pMappedData, srcData, dataSize);
	}
	else if (image_memProperties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
		AllocatedBuffer stagingBuffer = create_buffer("Image Update Stager", dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
		memcpy(stagingBuffer.info.pMappedData, srcData, dataSize);

		VkCommandBuffer cmd = start_immediate_recording();
		vkutil::transition_image(cmd, image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy copyRegion{};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = imageSize;

		vkCmdCopyBufferToImage(cmd, stagingBuffer.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		vkutil::transition_image(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); //!!!Might Delete. Dont think Im supposed to transition layout to this, especially if using this function on a variety types of images
		submit_immediate_commands();

		destroy_buffer(stagingBuffer);
	}
	else {
		throw std::runtime_error("Buffer Memory Property not a MyDevice Local or Host Visible");
	}
}

void VulkanContext::update_image(const AllocatedImage& dstImage, const AllocatedImage& srcImage, uint32_t copyCount, const VkImageCopy* copyInfo) {
	VkCommandBuffer cmd = start_immediate_recording();

	vkutil::transition_image(cmd, srcImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	vkutil::transition_image(cmd, dstImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	vkCmdCopyImage(cmd, srcImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, copyCount, copyInfo);

	submit_immediate_commands();
}

VkSampler VulkanContext::create_sampler(VkSamplerCreateInfo& samplerCreateInfo) {
	VkSampler sampler;
	vkCreateSampler(device, &samplerCreateInfo, nullptr, &sampler);
	return sampler;
}

void VulkanContext::destroy_sampler(const VkSampler& sampler) {
	vkDestroySampler(device, sampler, nullptr);
}

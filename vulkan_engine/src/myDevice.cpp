#include "myDevice.h"
#include <iostream>
#include <format>
#include <stack>

void MyDevice::init(SDL_Window* window, VkExtent2D windowExtent) {
	init_vulkan(window);
	init_swapchain(windowExtent);
	init_commands();
	init_syncStructurrs();
	init_vertexInput();
	init_descriptorSet();
	init_graphicsPipeline();
	init_imgui(window);
}

void MyDevice::shutdown() {
	//Cleanup Imgui
	ImGui_ImplVulkan_Shutdown();
	vkDestroyDescriptorPool(_device, _imguiDescriptorPool, nullptr);

	//Cleanup Pipeline
	vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
	vkDestroyPipeline(_device, _pipeline, nullptr);

	//Cleanup Descriptor Stuff
	vkDestroyDescriptorSetLayout(_device, _descriptorSetLayout, nullptr);
	vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);

	//Cleanup Commands and Sync Structures
	vkDestroyCommandPool(_device, _immCommandPool, nullptr);
	vkDestroyFence(_device, _immFence, nullptr);

	for (Frame& frame : _frames) {
		vkDestroyCommandPool(_device, frame.commandPool, nullptr);
		vkDestroySemaphore(_device, frame.renderSemaphore, nullptr);
		vkDestroySemaphore(_device, frame.swapchainSemaphore, nullptr);
		vkDestroyFence(_device, frame.renderFence, nullptr);

		destroy_buffer(frame.drawContext.indirectDrawCommandsBuffer);
		destroy_buffer(frame.drawContext.vertexPosBuffer);
		destroy_buffer(frame.drawContext.vertexOtherAttribBuffer);
		destroy_buffer(frame.drawContext.indexBuffer);
		destroy_buffer(frame.drawContext.viewprojMatrixBuffer);
		destroy_buffer(frame.drawContext.modelMatricesBuffer);
		destroy_buffer(frame.drawContext.primitiveIdsBuffer);
		destroy_buffer(frame.drawContext.primitiveInfosBuffer);
		destroy_buffer(frame.drawContext.materialsBuffer);
		destroy_buffer(frame.drawContext.texturesBuffer);
	}

	//Cleanup Swapchain
	destroy_swapchain();

	//Cleanup Vulkan & VMA
	vmaDestroyAllocator(_allocator);
	vkDestroySurfaceKHR(_instance, _surface, nullptr);
	vkDestroyDevice(_device, nullptr);

	vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
	vkDestroyInstance(_instance, nullptr);
}

/*
VMA ALlocation Flags Tips:
	GPU Only - Usage: Auto/Auto prefer Device, Flags: None or Dedicated_Memory if buffer is large like attachment
	Staging Buffer - Usage: Auto/Auto prefer Host, Flags: Create_Mapped & Host_Access_Sequential_Write to ensure both host visbile and wirte-combined
	Readback - Usage: Auto/Auto prefer Host, Flags: Create_Mapped & Host_Access_Random to allow host visible and caching.
	Dynamic Buffers (Frequently updated by CPU and read by GPU - Usage: Auto, Flags: Create_Mapped & Host_Access_Sequential_Write & Host_Access_Allow_Transfer_Instead to have vma decided if host visible or device local based on performance.
*/
AllocatedBuffer MyDevice::create_buffer(const char* name, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags) {
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

	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));
	vmaSetAllocationName(_allocator, newBuffer.allocation, name);

	return newBuffer;
}

AllocatedBuffer MyDevice::create_buffer(const char* name, VkBufferCreateInfo bufferInfo, VmaAllocationCreateInfo allocInfo) {
	AllocatedBuffer newBuffer;
	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &allocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));
	vmaSetAllocationName(_allocator, newBuffer.allocation, name);

	return newBuffer;
}

void MyDevice::destroy_buffer(const AllocatedBuffer& buffer) {
	vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

void MyDevice::update_buffer(const AllocatedBuffer& buffer, void* srcData, size_t srcDataSize, VkBufferCopy& copyInfo) {
	VkMemoryPropertyFlags buffer_memProperties;
	vmaGetAllocationMemoryProperties(_allocator, buffer.allocation, &buffer_memProperties);

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

void MyDevice::update_buffer(const AllocatedBuffer& buffer, void* srcData, size_t srcDataSize, std::vector<VkBufferCopy>& copyInfos) {
	VkMemoryPropertyFlags buffer_memProperties;
	vmaGetAllocationMemoryProperties(_allocator, buffer.allocation, &buffer_memProperties);

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

//Only allocates Image on the GPU
AllocatedImage MyDevice::create_image(const char* name, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
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
	VK_CHECK(vmaCreateImage(_allocator, &img_info, &alloc_info, &newImage.image, &newImage.allocation, nullptr));
	vmaSetAllocationName(_allocator, newImage.allocation, name);

	VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
	if (format == VK_FORMAT_D32_SFLOAT)
		aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;

	VkImageViewCreateInfo view_info = vkutil::imageview_create_info(format, newImage.image, aspectFlag);
	view_info.subresourceRange.levelCount = img_info.mipLevels;

	VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &newImage.imageView));

	return newImage;
}

AllocatedImage MyDevice::create_image(const char* name, VkImageCreateInfo imageInfo, VmaAllocationCreateInfo allocInfo) {
	AllocatedImage newImage;
	newImage.format = imageInfo.format;
	newImage.extent = imageInfo.extent;
	
	VK_CHECK(vmaCreateImage(_allocator, &imageInfo, &allocInfo, &newImage.image, &newImage.allocation, nullptr));
	vmaSetAllocationName(_allocator, newImage.allocation, name);

	VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
	if (newImage.format == VK_FORMAT_D32_SFLOAT)
		aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;

	VkImageViewCreateInfo view_info = vkutil::imageview_create_info(newImage.format, newImage.image, aspectFlag);
	view_info.subresourceRange.levelCount = imageInfo.mipLevels;

	VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &newImage.imageView));

	return newImage;
}

void MyDevice::destroy_image(const AllocatedImage& image) {
	vkDestroyImageView(_device, image.imageView, nullptr);
	vmaDestroyImage(_allocator, image.image, image.allocation);
}

void MyDevice::update_image(const AllocatedImage& image, void* srcData, VkExtent3D imageSize) {
	size_t dataSize = imageSize.width * imageSize.height * imageSize.depth * 4;
	
	VkMemoryPropertyFlags image_memProperties;
	vmaGetAllocationMemoryProperties(_allocator, image.allocation, &image_memProperties);

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
		
		vkutil::transition_image(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		submit_immediate_commands();

		destroy_buffer(stagingBuffer);
	}
	else {
		throw std::runtime_error("Buffer Memory Property not a MyDevice Local or Host Visible");
	}
}

VkSampler MyDevice::create_sampler(VkSamplerCreateInfo& samplerCreateInfo) {
	VkSampler sampler;
	vkCreateSampler(_device, &samplerCreateInfo, nullptr, &sampler);
	return sampler;
}

void MyDevice::destroy_sampler(const VkSampler& sampler) {
	vkDestroySampler(_device, sampler, nullptr);
}

VkCommandBuffer MyDevice::start_immediate_recording() {
	VK_CHECK(vkResetFences(_device, 1, &_immFence));
	VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(_immCommandBuffer, &cmdBeginInfo));
	return _immCommandBuffer;
}

void MyDevice::submit_immediate_commands() {
	VK_CHECK(vkEndCommandBuffer(_immCommandBuffer));

	VkCommandBufferSubmitInfo cmdInfo = vkutil::command_buffer_submit_info(_immCommandBuffer);
	VkSubmitInfo2 submit = vkutil::submit_info(&cmdInfo, nullptr, nullptr);

	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));

	VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

VkCommandBuffer MyDevice::startFrame(VkResult& result) {
	VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame().renderFence, true, 1000000000));

	//Acquire the next swapchain image
	result = vkAcquireNextImageKHR(_device, _swapchain.vkSwapchain, 1000000000, get_current_frame().swapchainSemaphore, nullptr, &_swapchainImageIndex);

	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		return nullptr;
	}

	VK_CHECK(vkResetFences(_device, 1, &get_current_frame().renderFence));

	VkCommandBuffer cmd = get_current_frame().commandBuffer;

	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
	
	return cmd;
}

void MyDevice::endFrame(VkResult& result) {
	VkCommandBuffer cmd = get_current_frame().commandBuffer;

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = vkutil::command_buffer_submit_info(cmd);
	VkSemaphoreSubmitInfo waitInfo = vkutil::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame().swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkutil::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame().renderSemaphore);

	VkSubmitInfo2 submit = vkutil::submit_info(&cmdInfo, &signalInfo, &waitInfo);

	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, get_current_frame().renderFence));

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &_swapchain.vkSwapchain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &get_current_frame().renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pImageIndices = &_swapchainImageIndex;

	result = vkQueuePresentKHR(_graphicsQueue, &presentInfo);

	go_next_frame();
}

Image MyDevice::get_currentSwapchainImage() {
	return _swapchain.images[_swapchainImageIndex];
}

VkExtent2D MyDevice::get_swapChainExtent() {
	return _swapchain.extent;
}

void MyDevice::init_vulkan(SDL_Window* window) {
	//Create Instance
	vkb::InstanceBuilder builder;
	
	builder.set_app_name("Vulkan Engine");
	builder.request_validation_layers(true);
	builder.use_default_debug_messenger();
	builder.require_api_version(1, 3, 0);

	auto inst_result = builder.build();
	vkb::Instance vkb_inst = inst_result.value();

	_instance = vkb_inst.instance;
	_debug_messenger = vkb_inst.debug_messenger;

	//Create Surface
	SDL_Vulkan_CreateSurface(window, _instance, &_surface);

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
	selector.set_surface(_surface);

	auto physical_device_selector_return = selector.select();

	if (!physical_device_selector_return) {
		std::cout << std::format("Physical Device Selector Error: {}", physical_device_selector_return.error().message()) << std::endl;
		throw std::runtime_error("Failed to select Physical Device");
	}

	vkb::PhysicalDevice physicalDevice = physical_device_selector_return.value();

	//Create Logical Device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	vkb::Device vkbDevice = deviceBuilder.build().value();

	_device = vkbDevice.device;
	_physicalDevice = physicalDevice.physical_device;
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	//Create VMA
	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.physicalDevice = _physicalDevice;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &_allocator);
}

void MyDevice::init_swapchain(VkExtent2D windowExtent) {
	vkb::SwapchainBuilder builder{ _physicalDevice, _device, _surface };

	_swapchain.format = VK_FORMAT_B8G8R8A8_UNORM;

	builder.set_desired_format(VkSurfaceFormatKHR{ .format = _swapchain.format, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR });
	builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
	builder.set_desired_extent(windowExtent.width, windowExtent.height);
	builder.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT);

	vkb::Swapchain vkbSwapchain = builder.build().value();

	_swapchain.extent = vkbSwapchain.extent;
	_swapchain.vkSwapchain = vkbSwapchain.swapchain;

	std::vector<VkImage> imgs = vkbSwapchain.get_images().value();
	std::vector<VkImageView> imgViews = vkbSwapchain.get_image_views().value();

	for (int i = 0; i < imgs.size(); i++) {
		_swapchain.images.push_back({ .image = imgs[i], .imageView = imgViews[i], .extent = { .width = 0, .height = 0, .depth = 0 } }); //Since using swapchain struct's extent, no need for indivudual image extents.
	}
}

void MyDevice::init_commands() {
	VkCommandPoolCreateInfo cmdPoolInfo{};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.pNext = nullptr;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cmdPoolInfo.queueFamilyIndex = _graphicsQueueFamily;

	for (int i = 0; i < FRAMES_TOTAL; i++) {
		VK_CHECK(vkCreateCommandPool(_device, &cmdPoolInfo, nullptr, &_frames[i].commandPool));

		VkCommandBufferAllocateInfo cmdAllocInfo{};
		cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdAllocInfo.pNext = nullptr;
		cmdAllocInfo.commandPool = _frames[i].commandPool;
		cmdAllocInfo.commandBufferCount = 1;
		cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i].commandBuffer));
	}

	//Init Immediate Command Resources
	VK_CHECK(vkCreateCommandPool(_device, &cmdPoolInfo, nullptr, &_immCommandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo{};
	cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAllocInfo.pNext = nullptr;
	cmdAllocInfo.commandPool = _immCommandPool;
	cmdAllocInfo.commandBufferCount = 1;
	cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));
}

void MyDevice::init_syncStructurrs() {
	//Init Frame's Syns Structures
	VkFenceCreateInfo fenceCreateInfo{};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkSemaphoreCreateInfo semaphoreCreateInfo{};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;

	for (int i = 0; i < FRAMES_TOTAL; i++) {
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i].renderFence));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i].swapchainSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i].renderSemaphore));
	}

	//Init Immediate Command Sync Structure
	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
}

void MyDevice::init_vertexInput() {
	//Vertex Buffer Format
	//VB1: [pos1, pos2, ...]
	//VB2: [(norm1, tan1, ...), (norm2, tan2, ...), ...] where each () corresponds to a vertex

	//Set up Vertex Input Descriptions
	_bindingDescriptions = { {},{} };

	//Vertex Position Buffer
	_bindingDescriptions[0].binding = 0;
	_bindingDescriptions[0].stride = sizeof(glm::vec3);
	_bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	//Vertex Other Attributes Buffer
	_bindingDescriptions[1].binding = 1;
	_bindingDescriptions[1].stride = sizeof(RenderShader::VertexAttributes);
	_bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	_attribueDescriptions = { {}, {}, {}, {}, {} };

	//Position
	_attribueDescriptions[0].binding = 0;
	_attribueDescriptions[0].location = 0;
	_attribueDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	_attribueDescriptions[0].offset = 0;

	//Normal
	_attribueDescriptions[1].binding = 1;
	_attribueDescriptions[1].location = 1;
	_attribueDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	_attribueDescriptions[1].offset = 0;

	//Tangent
	_attribueDescriptions[2].binding = 1;
	_attribueDescriptions[2].location = 2;
	_attribueDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	_attribueDescriptions[2].offset = offsetof(RenderShader::VertexAttributes, RenderShader::VertexAttributes::tangent);

	//Color_0
	_attribueDescriptions[3].binding = 1;
	_attribueDescriptions[3].location = 3;
	_attribueDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
	_attribueDescriptions[3].offset = offsetof(RenderShader::VertexAttributes, RenderShader::VertexAttributes::color);

	//UV_0
	_attribueDescriptions[4].binding = 1;
	_attribueDescriptions[4].location = 4;
	_attribueDescriptions[4].format = VK_FORMAT_R32G32_SFLOAT;
	_attribueDescriptions[4].offset = offsetof(RenderShader::VertexAttributes, RenderShader::VertexAttributes::uv);
}

void MyDevice::init_descriptorSet() {
	//Create Descriptor Pool
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = MAX_SAMPLED_IMAGE_COUNT },
		{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = MAX_SAMPLER_COUNT }
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 1;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

	if (vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create Descriptor Pool");

	//Create Descriptor Set Layout for Descriptors
	//-Set Layout Bindings
	std::vector<VkDescriptorSetLayoutBinding> layout_bindings = {
		{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = MAX_SAMPLED_IMAGE_COUNT,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr },
		{.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = MAX_SAMPLER_COUNT,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr }
	};

	//-Set Binding Flags
	std::vector<VkDescriptorBindingFlags> binding_flags = {
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
	};

	VkDescriptorSetLayoutBindingFlagsCreateInfo set_binding_flags{};
	set_binding_flags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	set_binding_flags.bindingCount = static_cast<uint32_t>(binding_flags.size());
	set_binding_flags.pBindingFlags = binding_flags.data();

	//-Now Create Descriptor Set Layout
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext = &set_binding_flags;
	layoutInfo.bindingCount = static_cast<uint32_t>(layout_bindings.size());
	layoutInfo.pBindings = layout_bindings.data();
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

	if (vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_descriptorSetLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to Create Descriptor Set Layout");

	//Create Descriptor Set
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &_descriptorSetLayout;

	if (vkAllocateDescriptorSets(_device, &allocInfo, &_descriptorSet) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate Descriptor Set");
}

void MyDevice::setup_drawContexts(const GraphicsDataPayload& payload) { 
	//Holds all the data for one drawContext
	std::vector<VkDrawIndexedIndirectCommand> indirect_commands;
	uint32_t drawCount = 0;
	std::vector<glm::vec3> positions;
	std::vector<RenderShader::VertexAttributes> attributes;
	std::vector<uint32_t> indices;
	RenderShader::ViewProj viewproj;
	viewproj.view = payload.camera_transform;
	viewproj.proj = glm::perspective(glm::radians(45.0f), _swapchain.extent.width / (float)_swapchain.extent.height, 50.0f, 0.01f);
	viewproj.proj[1][1] *= -1;
	std::vector<glm::mat4> model_matrices; //Also contains view and proj at start
	std::vector<int32_t> primitiveIds;
	std::vector<RenderShader::PrimitiveInfo> primitiveInfos;
	std::vector<RenderShader::Material> materials;
	std::vector<RenderShader::Texture> textures;

	//VkBufferCopies to correctly copy data to respective regions in buffer in accordance to their ids
	VkBufferCopy viewprojMatrix_copy_info;
	viewprojMatrix_copy_info = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(glm::mat4) * 2 };
	VkBufferCopy pos_copy_info;
	VkBufferCopy attrib_copy_info;
	VkBufferCopy index_copy_info;
	VkBufferCopy indirect_copy_info;
	VkBufferCopy primId_copy_info;
	std::vector<VkBufferCopy> modelMatrices_copy_infos;
	std::vector<VkBufferCopy> primInfo_copy_infos;
	std::vector<VkBufferCopy> material_copy_infos;
	std::vector<VkBufferCopy> texture_copy_infos;

	//Get Our Data from Host
	//-Navigate Node Tree for Mesh Nodes and Get Primitive Data
	//--Add all root nodes in current scene to Sack
	std::stack<std::shared_ptr<Node>> dfs_node_stack;
	for (auto root_node : payload.current_scene->root_nodes) {
		dfs_node_stack.push(root_node);
	}
	//--Perform DFS, traverse all Nodes
	while (!dfs_node_stack.empty()) {
		std::shared_ptr<Node> currentNode = dfs_node_stack.top();
		dfs_node_stack.pop();

		//Add currentNode's children to Stack
		for (std::shared_ptr<Node> child_node : currentNode->child_nodes) {
			dfs_node_stack.push(child_node);
		}

		modelMatrices_copy_infos.push_back({ .srcOffset = model_matrices.size() * sizeof(glm::mat4), .dstOffset = currentNode->getID() * sizeof(glm::mat4), .size = sizeof(glm::mat4) });
		model_matrices.push_back(currentNode->get_WorldTransform());

		//Check if Node represents Mesh
		if (currentNode->mesh != nullptr) {
			std::shared_ptr<Mesh>& currentMesh = currentNode->mesh;

			//Iterate through it's primitives and add their data to 
			for (Mesh::Primitive primitive : currentMesh->primitives) {
				//Indirect Draw Command
				VkDrawIndexedIndirectCommand indirect_command{};
				indirect_command.firstIndex = indices.size();
				indirect_command.indexCount = primitive.indices.size();
				indirect_command.vertexOffset = positions.size();
				indirect_command.firstInstance = 0;
				indirect_command.instanceCount = 1;
				indirect_commands.push_back(indirect_command);

				drawCount++;

				//Primitive Ids
				primitiveIds.push_back(primitive.getID());

				/*
				//-Construct The Primitive's DataRegions and push them back onto our array of regions.
				DataRegion last_vertex_region;
				if (_deviceDataMapper.primitiveVerticeRegions.size() > 0) { //Check if there is a region to designate as the last. If not just set a 0 offset 0 size one.
					uint32_t last_vertex_region_ID = _deviceDataMapper.primitiveVerticeRegions.back();
					last_vertex_region = _deviceDataMapper.PrimitiveID_to_PrimitivePositionsRegions[last_vertex_region_ID];
				}
				else
					last_vertex_region = { .offset = 0, .size = 0 };

				_deviceDataMapper.PrimitiveID_to_PrimitivePositionsRegions[primitive.getID()] = { .offset = last_vertex_region.offset + last_vertex_region.size, .size = sizeof(glm::vec3) * primitive.vertices.size() };
				_deviceDataMapper.PrimitiveID_to_PrimitiveAttributesRegions[primitive.getID()] = { .offset = last_vertex_region.offset + last_vertex_region.size, .size = sizeof(RenderShader::VertexAttributes) * primitive.vertices.size() };
				_deviceDataMapper.primitiveVerticeRegions.push_back(primitive.getID());

				DataRegion last_index_region;
				if (_deviceDataMapper.primitiveIndicesRegions.size() > 0) { //Check if there is a region to designate as the last. If not just set a 0 offset 0 size one.
					uint32_t last_index_region_ID = _deviceDataMapper.primitiveIndicesRegions.back();
					last_index_region = _deviceDataMapper.PrimitiveID_to_PrimitiveIndicesRegions[last_index_region_ID];
				}
				else
					last_index_region = { .offset = 0, .size = 0 };

				_deviceDataMapper.PrimitiveID_to_PrimitiveIndicesRegions[primitive.getID()] = { .offset = last_index_region.offset + last_index_region.size, .size = sizeof(uint32_t) * primitive.indices.size() };
				_deviceDataMapper.primitiveIndicesRegions.push_back(primitive.getID());
				*/
				//Vertex's Position and other Vertex Attributes
				for (Mesh::Primitive::Vertex vertex : primitive.vertices) {
					RenderShader::VertexAttributes attribute;
					attribute.normal = vertex.normal;
					attribute.tangent = vertex.tangent;
					if (vertex.colors.empty())
						attribute.color = glm::vec3(1.0f, 1.0f, 1.0f);
					else
						attribute.color = vertex.colors[0]; //Get Color_0
					if (vertex.uvs.empty())
						attribute.uv = glm::vec2(-1.0f, -1.0f);
					else
						attribute.uv = vertex.uvs[0]; //Get TexCoord_0

					positions.push_back(vertex.position);
					attributes.push_back(attribute);
				}

				//Indices
				indices.insert(indices.end(), primitive.indices.begin(), primitive.indices.end());

				//PrimitiveInfo
				primInfo_copy_infos.push_back({ .srcOffset = primitiveInfos.size() * sizeof(RenderShader::PrimitiveInfo), .dstOffset = primitive.getID() * sizeof(RenderShader::PrimitiveInfo), .size = sizeof(RenderShader::PrimitiveInfo) });
				RenderShader::PrimitiveInfo prmInfo{};
				if (primitive.material.expired() != true)
					prmInfo.mat_id = primitive.material.lock()->getID();
				else
					prmInfo.mat_id = 0;
				prmInfo.model_matrix_id = currentNode->getID();
				primitiveInfos.push_back(prmInfo);
			}
		}
	}
	pos_copy_info = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(glm::vec3) * positions.size() };
	attrib_copy_info = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(RenderShader::VertexAttributes) * attributes.size() };
	index_copy_info = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(uint32_t) * indices.size() };
	indirect_copy_info = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(VkDrawIndexedIndirectCommand) * indirect_commands.size() };
	primId_copy_info = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(int32_t) * primitiveIds.size() };

	//-Get Materials
	for (std::shared_ptr<Material> material : payload.materials) {
		material_copy_infos.push_back({ .srcOffset = materials.size() * sizeof(RenderShader::Material), .dstOffset = material->getID() * sizeof(RenderShader::Material), .size = sizeof(RenderShader::Material) });
		RenderShader::Material mat{};
		if (material->baseColor_texture.expired() != true)
			mat.baseColor_texture_id = material->baseColor_texture.lock()->getID();
		else
			mat.baseColor_texture_id = 0;
		mat.baseColor_texCoord_id = material->baseColor_coord_index;
		mat.baseColor_factor = material->baseColor_Factor;
		materials.push_back(mat);
	}

	//-Get Textures
	for (auto& texture : payload.textures) {
		texture_copy_infos.push_back({ .srcOffset = textures.size() * sizeof(RenderShader::Texture), .dstOffset = texture->getID() * sizeof(RenderShader::Texture), .size = sizeof(RenderShader::Texture) });
		RenderShader::Texture tex{};
		tex.textureImage_id = texture->image_index;
		tex.sampler_id = texture->sampler_index;
		textures.push_back(tex);
	}

	//Add Data to DrawContexts
	size_t alloc_vertPos_size = sizeof(glm::vec3) * positions.size();
	size_t alloc_vertAttrib_size = sizeof(RenderShader::VertexAttributes) * attributes.size();
	size_t alloc_index_size = sizeof(uint32_t) * indices.size();
	size_t alloc_indirect_size = sizeof(VkDrawIndexedIndirectCommand) * indirect_commands.size();
	size_t alloc_viewprojMatrix_size = sizeof(glm::mat4) * 2;
	size_t alloc_modelMatrices_size = sizeof(glm::mat4) * model_matrices.size();
	size_t alloc_primIds_size = sizeof(int32_t) * primitiveIds.size();
	size_t alloc_primInfo_size = sizeof(RenderShader::PrimitiveInfo) * primitiveInfos.size();
	size_t alloc_materials_size = sizeof(RenderShader::Material) * materials.size();
	size_t alloc_textures_size = sizeof(RenderShader::Texture) * textures.size();

	int i = 1;
	for (Frame& frame : _frames) {
		DrawContext& currentDrawContext = frame.drawContext;
		currentDrawContext.drawCount = drawCount;

		//Draw Coommand and Vertex Input Buffers	
		currentDrawContext.indirectDrawCommandsBuffer = create_buffer(std::format("Indirect Draw Commands Buffer {}", i).c_str(), alloc_indirect_size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT);
		currentDrawContext.vertexPosBuffer = create_buffer(std::format("Vertex Position Buffer {}", i).c_str(), alloc_vertPos_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT);
		currentDrawContext.vertexOtherAttribBuffer = create_buffer(std::format("Vertex Other Attributes Buffer {}", i).c_str(), alloc_vertAttrib_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT);
		currentDrawContext.indexBuffer = create_buffer(std::format("Index Buffer {}", i).c_str(), alloc_index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT);
		//BDA Buffers
		currentDrawContext.viewprojMatrixBuffer = create_buffer(std::format("View and Projection Matrix Buffer {}", i).c_str(), alloc_viewprojMatrix_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT); //Careful as if the alloc size is 0. Will cause errors
		currentDrawContext.modelMatricesBuffer = create_buffer(std::format("Model Matrices Buffer {}", i).c_str(), alloc_modelMatrices_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT);
		currentDrawContext.primitiveIdsBuffer = create_buffer(std::format("Primitive IDs Buffer {}", i).c_str(), alloc_primIds_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT);
		currentDrawContext.primitiveInfosBuffer = create_buffer(std::format("Primitive Infos Buffer {}", i).c_str(), alloc_primInfo_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT);
		currentDrawContext.materialsBuffer = create_buffer(std::format("Materials Buffer {}", i).c_str(), alloc_materials_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT);
		currentDrawContext.texturesBuffer = create_buffer(std::format("Textures Buffer {}", i).c_str(), alloc_textures_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT);

		VkBufferDeviceAddressInfo address_info{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		address_info.buffer = currentDrawContext.viewprojMatrixBuffer.buffer;
		currentDrawContext.viewprojMatrixBufferAddress = vkGetBufferDeviceAddress(_device, &address_info);
		address_info.buffer = currentDrawContext.modelMatricesBuffer.buffer;
		currentDrawContext.modelMatricesBufferAddress = vkGetBufferDeviceAddress(_device, &address_info);
		address_info.buffer = currentDrawContext.primitiveIdsBuffer.buffer;
		currentDrawContext.primitiveIdsBufferAddress = vkGetBufferDeviceAddress(_device, &address_info);
		address_info.buffer = currentDrawContext.primitiveInfosBuffer.buffer;
		currentDrawContext.primitiveInfosBufferAddress = vkGetBufferDeviceAddress(_device, &address_info);
		address_info.buffer = currentDrawContext.materialsBuffer.buffer;
		currentDrawContext.materialsBufferAddress = vkGetBufferDeviceAddress(_device, &address_info);
		address_info.buffer = currentDrawContext.texturesBuffer.buffer;
		currentDrawContext.texturesBufferAddress = vkGetBufferDeviceAddress(_device, &address_info);

		//Copy Data to the Buffers
		update_buffer(currentDrawContext.indirectDrawCommandsBuffer, indirect_commands.data(), alloc_indirect_size, indirect_copy_info);
		update_buffer(currentDrawContext.vertexPosBuffer, positions.data(), alloc_vertPos_size, pos_copy_info);
		update_buffer(currentDrawContext.vertexOtherAttribBuffer, attributes.data(), alloc_vertAttrib_size, attrib_copy_info);
		update_buffer(currentDrawContext.indexBuffer, indices.data(), alloc_index_size, index_copy_info);
		update_buffer(currentDrawContext.viewprojMatrixBuffer, &viewproj, alloc_viewprojMatrix_size, viewprojMatrix_copy_info);
		update_buffer(currentDrawContext.modelMatricesBuffer, model_matrices.data(), alloc_modelMatrices_size, modelMatrices_copy_infos);
		update_buffer(currentDrawContext.primitiveIdsBuffer, primitiveIds.data(), alloc_primIds_size, primId_copy_info);
		update_buffer(currentDrawContext.primitiveInfosBuffer, primitiveInfos.data(), alloc_primInfo_size, primInfo_copy_infos);
		update_buffer(currentDrawContext.materialsBuffer, materials.data(), alloc_materials_size, material_copy_infos);
		update_buffer(currentDrawContext.texturesBuffer, textures.data(), alloc_textures_size, texture_copy_infos);
		i++;
	}
}

std::vector<VkBuffer> MyDevice::get_vertexBuffers() {
	DrawContext& currentDrawContext = get_current_frame().drawContext;
	return std::vector<VkBuffer>{currentDrawContext.vertexPosBuffer.buffer, currentDrawContext.vertexOtherAttribBuffer.buffer};
}

VkBuffer MyDevice::get_indexBuffer() {
	DrawContext& currentDrawContext = get_current_frame().drawContext;
	return currentDrawContext.indexBuffer.buffer;
}

RenderShader::PushConstants MyDevice::get_pushConstants() {
	DrawContext& currentDrawContext = get_current_frame().drawContext;
	RenderShader::PushConstants pushconstants{};
	pushconstants.primitiveIdsBufferAddress = currentDrawContext.primitiveIdsBufferAddress;
	pushconstants.primitiveInfosBufferAddress = currentDrawContext.primitiveInfosBufferAddress;
	pushconstants.viewProjMatrixBufferAddress = currentDrawContext.viewprojMatrixBufferAddress;
	pushconstants.modelMatricesBufferAddress = currentDrawContext.modelMatricesBufferAddress;
	pushconstants.materialsBufferAddress = currentDrawContext.materialsBufferAddress;
	pushconstants.texturesBufferAddress = currentDrawContext.texturesBufferAddress;
	return pushconstants;
}

VkBuffer MyDevice::get_indirectDrawBuffer() {
	DrawContext& currentDrawContext = get_current_frame().drawContext;
	return currentDrawContext.indirectDrawCommandsBuffer.buffer;
}

uint32_t MyDevice::get_drawCount() {
	DrawContext& currentDrawContext = get_current_frame().drawContext;
	return currentDrawContext.drawCount;
}

void MyDevice::signal_to_updateDeviceBuffer(DeviceBufferType bufferType) {
	_deviceBufferTypesCounter[bufferType] = FRAMES_TOTAL;
}

void MyDevice::updateSignaledDeviceBuffers(GraphicsDataPayload& payload) {
	if (_deviceBufferTypesCounter[DeviceBufferType::ViewProjMatrix] > 0) {
		size_t viewSize = sizeof(glm::mat4);
		VkBufferCopy copyInfo{ .srcOffset = 0, .dstOffset = 0, .size = viewSize };
		update_buffer(get_current_frame().drawContext.viewprojMatrixBuffer, &payload.camera_transform, viewSize, copyInfo);
		_deviceBufferTypesCounter[DeviceBufferType::ViewProjMatrix]--;
	}
	//Other Buffers...
}

void MyDevice::init_graphicsPipeline() {
	//Load SHaders
	VkShaderModule vertexShader;
	if (!vkutil::load_shader_module("shaders/default_vert.spv", _device, &vertexShader))
		throw std::runtime_error("Error trying to create Vertex Shader Module");
	else
		std::cout << "Vertex Shader successfully loaded" << std::endl;

	VkShaderModule fragShader;
	if (!vkutil::load_shader_module("shaders/default_frag.spv", _device, &fragShader))
		throw std::runtime_error("error trying to create Frag Shader Module");
	else
		std::cout << "Fragment Shader successfully loaded" << std::endl;

	//Set Pipeline Layout - Descriptor Sets and Push Constants Layout
	VkPushConstantRange range{};
	range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
	range.offset = 0;
	range.size = 48;

	VkPipelineLayoutCreateInfo pipeline_layout_info = vkutil::pipeline_layout_create_info();
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &_descriptorSetLayout;
	pipeline_layout_info.pushConstantRangeCount = 1;
	pipeline_layout_info.pPushConstantRanges = &range;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_pipelineLayout));

	//Build Pipeline
	PipelineBuilder pipelineBuilder;
	pipelineBuilder._pipelineLayout = _pipelineLayout;
	pipelineBuilder.set_shaders(vertexShader, fragShader);
	pipelineBuilder.set_vertex_input(_bindingDescriptions, _attribueDescriptions);
	pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	pipelineBuilder.set_multisampling_none();
	pipelineBuilder.disable_blending();
	pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
	pipelineBuilder.set_color_attachment_format(_swapchain.format);
	pipelineBuilder.set_depth_format(VK_FORMAT_D32_SFLOAT);

	_pipeline = pipelineBuilder.build_pipeline(_device);

	vkDestroyShaderModule(_device, vertexShader, nullptr);
	vkDestroyShaderModule(_device, fragShader, nullptr);
}

void MyDevice::init_imgui(SDL_Window* window) {
	VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
	{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
	{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
	{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
	{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
	{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
	{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
	{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
	{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
	{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
	{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

	VkDescriptorPoolCreateInfo pool_info{};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000;
	pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
	pool_info.pPoolSizes = pool_sizes;

	if (vkCreateDescriptorPool(_device, &pool_info, nullptr, &_imguiDescriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create Descriptor Pool for Imgui");

	ImGui::CreateContext();
	ImGui_ImplSDL2_InitForVulkan(window);

	ImGui_ImplVulkan_InitInfo init_info{};
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _physicalDevice;
	init_info.Device = _device;
	init_info.Queue = _graphicsQueue;
	init_info.DescriptorPool = _imguiDescriptorPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;
	init_info.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchain.format;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);
	ImGui_ImplVulkan_CreateFontsTexture();
}

void MyDevice::resize_swapchain(VkExtent2D windowExtent) {
	vkDeviceWaitIdle(_device);
	destroy_swapchain();
	init_swapchain(windowExtent);
}

void MyDevice::bind_descriptors(GraphicsDataPayload& payload) {
	std::vector<VkWriteDescriptorSet> descriptorWrites(2);

	std::vector<VkDescriptorImageInfo> sampledImages_imgInfos;
	for (auto& image : payload.images) {
		VkDescriptorImageInfo imgInfo{};
		imgInfo.imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
		imgInfo.imageView = image.imageView;
		sampledImages_imgInfos.push_back(imgInfo);
	}

	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = _descriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	descriptorWrites[0].descriptorCount = sampledImages_imgInfos.size();
	descriptorWrites[0].pImageInfo = sampledImages_imgInfos.data();

	std::vector<VkDescriptorImageInfo> sampler_imgInfos;
	for (auto& sampler : payload.samplers) {
		VkDescriptorImageInfo imgInfo{};
		imgInfo.sampler = sampler;
		sampler_imgInfos.push_back(imgInfo);
	}

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = _descriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	descriptorWrites[1].descriptorCount = sampler_imgInfos.size();
	descriptorWrites[1].pImageInfo = sampler_imgInfos.data();

	vkUpdateDescriptorSets(_device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
}

void MyDevice::destroy_swapchain() {
	vkDestroySwapchainKHR(_device, _swapchain.vkSwapchain, nullptr);

	for (int i = 0; i < _swapchain.images.size(); i++)
		vkDestroyImageView(_device, _swapchain.images[i].imageView, nullptr);
}

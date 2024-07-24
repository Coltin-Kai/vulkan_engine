#include "engine.h"

#include "VkBootstrap.h"
#include "vulkan/vk_enum_string_helper.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include "checkVkResult.h"
#include "vulkanUtility.h"
#include "vulkanFunctionPointers.h"

#include <thread>
#include <iostream>
#include <format>
#include <pipeline.h>

void Engine::init() {
	//Initialize SDL Window
	SDL_SetMainReady();
	SDL_Init(SDL_INIT_VIDEO);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	_window = SDL_CreateWindow("Engine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, _windowExtent.width, _windowExtent.height, window_flags);
	
	init_vulkan();
	init_swapchain();
	init_commands();
	init_sync_structures();
	setup_vertex_input();
	setup_descriptors();
	init_graphics_pipeline();
}

void Engine::run() {
	SDL_Event e;
	bool quit = false;

	while (!quit) {
		while (SDL_PollEvent(&e) != 0) {
			if (e.type == SDL_QUIT)
				quit = true;

			if (e.window.event == SDL_WINDOWEVENT_MINIMIZED)
				stop_rendering = true;
			if (e.window.event == SDL_WINDOWEVENT_RESTORED)
				stop_rendering = false;
			if (e.window.event == SDL_WINDOWEVENT_RESIZED)
				windowResized = true;
		}

		if (stop_rendering) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		if (windowResized) {
			resize_swapchain();
		}

		draw();
	}
}

void Engine::cleanup() {
	vkDeviceWaitIdle(_device);

	for (int i = 0; i < FRAMES_TOTAL; i++) {
		vkDestroyCommandPool(_device, _frames[i].commandPool, nullptr);

		vkDestroyFence(_device, _frames[i].renderFence, nullptr);
		vkDestroySemaphore(_device, _frames[i].renderSemaphore, nullptr);
		vkDestroySemaphore(_device, _frames[i].swapchainSemaphore, nullptr);
		
		_frames[i].deletionQueue.flush();
	}

	_mainDeletionQueue.flush();

	destroy_swapchain();
	vkDestroySurfaceKHR(_instance, _surface, nullptr);
	vkDestroyDevice(_device, nullptr);

	vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
	vkDestroyInstance(_instance, nullptr);
	SDL_DestroyWindow(_window);
}

void Engine::draw() {
	VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame().renderFence, true, 1000000000));
	
	get_current_frame().deletionQueue.flush();
	
	uint32_t swapchainImageIndex;
	VkResult result = vkAcquireNextImageKHR(_device, _swapchain.vkSwapchain, 1000000000, get_current_frame().swapchainSemaphore, nullptr, &swapchainImageIndex);

	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		windowResized = true;
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		throw std::runtime_error("Failed to acquire Swap Chain Images!");

	VK_CHECK(vkResetFences(_device, 1, &get_current_frame().renderFence));

	VkCommandBuffer cmd = get_current_frame().commandBuffer;

	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	vkutil::transition_image(cmd, _swapchain.images[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	VkClearColorValue clearValue = { {0.0f, 1.0f, 0.0f, 1.0f} };

	VkImageSubresourceRange clearRange = vkutil::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	vkCmdClearColorImage(cmd, _swapchain.images[swapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

	draw_geometry(cmd, swapchainImageIndex);

	vkutil::transition_image(cmd, _swapchain.images[swapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

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
	presentInfo.pImageIndices = &swapchainImageIndex;

	result = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
		windowResized = true;
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		throw std::runtime_error("Failed to Present!");

	_frameNumber++;
}

void Engine::draw_geometry(VkCommandBuffer cmd, uint32_t swapchainImageIndex) {
	VkRenderingAttachmentInfo colorAttachment = vkutil::attachment_info(_swapchain.imageViews[swapchainImageIndex], nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo = vkutil::rendering_info(_swapchain.extent, &colorAttachment, nullptr);
	vkCmdBeginRendering(cmd, &renderInfo);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);

	//Set Dynamic States
	VkViewport viewport{};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = _swapchain.extent.width;
	viewport.height = _swapchain.extent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = _swapchain.extent.width;
	scissor.extent.height = _swapchain.extent.height;

	vkCmdSetScissor(cmd, 0, 1, &scissor);

	//Set Vertex Input
	VkDeviceSize offset[] = { 0 };
	vkCmdBindVertexBuffers(cmd, 0, 1, &_vertex_data_buffer.buffer, offset);

	//Set Descriptors

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr);

	//Draw
	vkCmdDraw(cmd, 3, 1, 0, 0);

	vkCmdEndRendering(cmd);
}

void Engine::init_vulkan() {
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
	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

	//Get Physical Device
	VkPhysicalDeviceVulkan13Features features{};
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features.dynamicRendering = true;
	features.synchronization2 = true;
	
	VkPhysicalDeviceVulkan12Features features2{};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features2.bufferDeviceAddress = true;
	//-Descriptor Indexing Features:
	features2.descriptorIndexing = true;
	features2.descriptorBindingPartiallyBound = true;
	features2.shaderUniformBufferArrayNonUniformIndexing = true;
	features2.descriptorBindingUniformBufferUpdateAfterBind = true;
		
	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	
	selector.set_minimum_version(1, 3);
	selector.set_required_features_13(features);
	selector.set_required_features_12(features2);
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

	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.physicalDevice = _physicalDevice;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;
	allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
	vmaCreateAllocator(&allocatorInfo, &_allocator);

	_mainDeletionQueue.push_function([&]() {
		vmaDestroyAllocator(_allocator);
		});
}

void Engine::init_swapchain() {
	vkb::SwapchainBuilder builder{ _physicalDevice, _device, _surface };

	_swapchain.format = VK_FORMAT_B8G8R8A8_UNORM;

	builder.set_desired_format(VkSurfaceFormatKHR{ .format = _swapchain.format, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR });
	builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
	builder.set_desired_extent(_windowExtent.width, _windowExtent.height);
	builder.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT);

	vkb::Swapchain vkbSwapchain = builder.build().value();

	_swapchain.extent = vkbSwapchain.extent;
	_swapchain.vkSwapchain = vkbSwapchain.swapchain;
	_swapchain.images = vkbSwapchain.get_images().value();
	_swapchain.imageViews = vkbSwapchain.get_image_views().value();
}

void Engine::init_commands() {
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
}

void Engine::init_sync_structures() {
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
}

void Engine::init_graphics_pipeline() {
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

	VkPipelineLayoutCreateInfo pipeline_layout_info = vkutil::pipeline_layout_create_info();
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &_uniform_descriptor.layout;

	VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_pipelineLayout));

	PipelineBuilder pipelineBuilder;
	pipelineBuilder._pipelineLayout = _pipelineLayout;
	pipelineBuilder.set_shaders(vertexShader, fragShader);
	pipelineBuilder.set_vertex_input(_bindingDescription, _attribueDescriptions);
	pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	pipelineBuilder.set_multisampling_none();
	pipelineBuilder.disable_blending();
	pipelineBuilder.disable_depthtest();
	pipelineBuilder.set_color_attachment_format(_swapchain.format);
	pipelineBuilder.set_depth_format(VK_FORMAT_UNDEFINED);

	_pipeline = pipelineBuilder.build_pipeline(_device);

	vkDestroyShaderModule(_device, vertexShader, nullptr);
	vkDestroyShaderModule(_device, fragShader, nullptr);

	_mainDeletionQueue.push_function([&]() {
		vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
		vkDestroyPipeline(_device, _pipeline, nullptr);
		});
}

void Engine::setup_vertex_input() {
	//Set up Vertex Input Descriptions
	_bindingDescription = {};
	_bindingDescription.binding = 0;
	_bindingDescription.stride = sizeof(Vertex);
	_bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	_attribueDescriptions = { {}, {} };
	_attribueDescriptions[0].binding = 0;
	_attribueDescriptions[0].location = 0;
	_attribueDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
	_attribueDescriptions[0].offset = offsetof(Vertex, Vertex::pos);

	_attribueDescriptions[1].binding = 0;
	_attribueDescriptions[1].location = 1;
	_attribueDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	_attribueDescriptions[1].offset = offsetof(Vertex, Vertex::color);

	//Set up Vertex Input Data Buffer
	_vertex_data_buffer = create_buffer(sizeof(Vertex) * vertices.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	
	_mainDeletionQueue.push_function([=, this]() {
		destroy_buffer(_vertex_data_buffer);
		});

	void* mappedData;
	vmaMapMemory(_allocator, _vertex_data_buffer.allocation, &mappedData);
	memcpy(mappedData, vertices.data(), sizeof(Vertex) * vertices.size());
	vmaUnmapMemory(_allocator, _vertex_data_buffer.allocation);
}

void Engine::setup_descriptors() {
	//Setup Uniform Data and a Buffer that holds it
	_uniformData_buffer = create_buffer(sizeof(UnifrormData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	
	_mainDeletionQueue.push_function([=, this]() {
		destroy_buffer(_uniformData_buffer);
		});
	
	UnifrormData* data = (UnifrormData*)_uniformData_buffer.allocation->GetMappedData();
	data->color = glm::vec3(0.0, 0.5, 0.5);

	//Create Descriptor Set Layout for Descriptors

	VkDescriptorSetLayoutBinding uboLayoutBinding{};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	uboLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &uboLayoutBinding;

	VK_CHECK(vkCreateDescriptorSetLayout(_device, &layoutInfo, nullptr, &_uniform_descriptor.layout));

	_mainDeletionQueue.push_function([=, this]() {
		vkDestroyDescriptorSetLayout(_device, _uniform_descriptor.layout, nullptr);
		});

	//Create Descriptor Pool

	VkDescriptorPoolSize poolSize{};
	poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSize.descriptorCount = 1;

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = 1;

	if (vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create Descriptor Pool");

	_mainDeletionQueue.push_function([=, this]() {
		vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
		});

	//Create Descriptor Set

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &_uniform_descriptor.layout;
	
	if (vkAllocateDescriptorSets(_device, &allocInfo, &_descriptorSet) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate Descriptor Set");
	
	//Configure Descriptor in Descriptor Set with our data

	VkDescriptorBufferInfo bufferInfo{};
	bufferInfo.buffer = _uniformData_buffer.buffer;
	bufferInfo.offset = 0;
	bufferInfo.range = sizeof(UnifrormData);

	VkWriteDescriptorSet descriptorWrites{};
	descriptorWrites.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites.dstSet = _descriptorSet;
	descriptorWrites.dstBinding = 0;
	descriptorWrites.dstArrayElement = 0;
	descriptorWrites.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites.descriptorCount = 1;
	descriptorWrites.pBufferInfo = &bufferInfo;
	
	vkUpdateDescriptorSets(_device, 1, &descriptorWrites, 0, nullptr);
}

void Engine::resize_swapchain() {
	vkDeviceWaitIdle(_device);
	destroy_swapchain();
	
	int w, h;
	SDL_GetWindowSize(_window, &w, &h);
	_windowExtent.width = w;
	_windowExtent.height = h;
	init_swapchain();
	
	windowResized = false;
}

void Engine::destroy_swapchain() {
	vkDestroySwapchainKHR(_device, _swapchain.vkSwapchain, nullptr);

	for (int i = 0; i < _swapchain.imageViews.size(); i++)
		vkDestroyImageView(_device, _swapchain.imageViews[i], nullptr);
}

Engine::AllocatedBuffer Engine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = memoryUsage;
	vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	AllocatedBuffer newBuffer;

	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));
	return newBuffer;
}

void Engine::destroy_buffer(const AllocatedBuffer& buffer) {
	vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}


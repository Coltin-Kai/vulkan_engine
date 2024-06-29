#include "engine.h"

#include "VkBootstrap.h"
#include "vulkan/vk_enum_string_helper.h"

#include "checkVkResult.h"
#include "vulkanUtility.h"

#include <thread>
#include <iostream>
#include <format>

void Engine::init() {
	//Initialize SDL Window
	SDL_SetMainReady();
	SDL_Init(SDL_INIT_VIDEO);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);
	_window = SDL_CreateWindow("Engine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, _windowExtent.width, _windowExtent.height, window_flags);

	init_vulkan();
	init_swapchain();
	init_commands();
	init_sync_structures();
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
		}

		if (stop_rendering) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
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
		
	}

	destroy_swapchain();
	vkDestroySurfaceKHR(_instance, _surface, nullptr);
	vkDestroyDevice(_device, nullptr);

	vkb::destroy_debug_utils_messenger(_instance, _debug_messenger);
	vkDestroyInstance(_instance, nullptr);
	SDL_DestroyWindow(_window);
}

void Engine::draw() {
	VK_CHECK(vkWaitForFences(_device, 1, &get_current_frame().renderFence, true, 1000000000));
	VK_CHECK(vkResetFences(_device, 1, &get_current_frame().renderFence));

	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain.vkSwapchain, 1000000000, get_current_frame().swapchainSemaphore, nullptr, &swapchainImageIndex));

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

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	_frameNumber++;
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

	vkb::PhysicalDeviceSelector selector{ vkb_inst };
	
	selector.set_minimum_version(1, 3);
	selector.set_required_features_13(features);
	selector.set_surface(_surface);
	
	vkb::PhysicalDevice physicalDevice = selector.select().value();

	//Create Logical Device
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };

	vkb::Device vkbDevice = deviceBuilder.build().value();

	_device = vkbDevice.device;
	_physicalDevice = physicalDevice.physical_device;
	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
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

void Engine::destroy_swapchain() {
	vkDestroySwapchainKHR(_device, _swapchain.vkSwapchain, nullptr);

	for (int i = 0; i < _swapchain.imageViews.size(); i++)
		vkDestroyImageView(_device, _swapchain.imageViews[i], nullptr);
}


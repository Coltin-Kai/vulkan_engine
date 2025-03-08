#include "engine.h"

#include "VkBootstrap.h"
#include "vulkan/vk_enum_string_helper.h"

#include "vk_mem_alloc.h"

#include "gtc/matrix_transform.hpp"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include "checkVkResult.h"
#include "vulkan_helper_functions.h"
#include "loader.h"
#include "pipeline.h"
#include "shader_types.h"

#include <thread>
#include <iostream>
#include <format>
#include <stack>

void Engine::init() {
	//Initialize SDL Window
	SDL_SetMainReady();
	SDL_Init(SDL_INIT_VIDEO);
	SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	_window = SDL_CreateWindow("Engine", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, _windowExtent.width, _windowExtent.height, window_flags);
	SDL_SetRelativeMouseMode(SDL_TRUE); //Traps Mouse and records relative mouse movement
	
	init_vulkan();
	init_swapchain();
	init_commands();
	init_sync_structures();
	setup_depthImage();

	//LAZY CODE STUFF
	//-Load Default Data
	uint32_t default_data = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	VkExtent3D extent{};
	extent.width = 1;
	extent.height = 1;
	extent.depth = 1;
	AllocatedImage default_image = create_image((void*)&default_data, extent, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
	_payload.images.push_back(default_image);

	VkSampler default_sampler;
	VkSamplerCreateInfo sampler_info{};
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.maxLod = VK_LOD_CLAMP_NONE;
	sampler_info.minLod = 0;
	sampler_info.magFilter = VK_FILTER_LINEAR;
	sampler_info.minFilter = VK_FILTER_LINEAR;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	vkCreateSampler(_device, &sampler_info, nullptr, &default_sampler);
	_payload.samplers.push_back(default_sampler);

	Texture default_texture{};
	default_texture.name = "default";
	default_texture.image_index = 0;
	default_texture.sampler_index = 0;
	_payload.textures.push_back(std::make_shared<Texture>(default_texture));

	Material default_material{};
	default_material.name = "default";
	_payload.materials.push_back(std::make_shared<Material>(default_material));

	//-Load File Data
	loadGLTFFile(_payload, *this, "C:\\Github\\vulkan_engine\\vulkan_engine\\assets\\Sample_Models\\BoomBox\\BoomBox.gltf"); //Exception expected to be thrown since allocated data in payload is not released
	_mainDeletionQueue.push_function([&]() {
		_payload.cleanup(_device, _allocator);
		});
	
	_camera = Camera({ 0.0f, 0.0f, 2.0f });
	_camera.update_view_matrix();

	//-Initiliaze Device Data Update Tracker
	_deviceDataUpdateTracker.add_deviceBufferType(DeviceBufferType::IndirectDraw);
	_deviceDataUpdateTracker.add_deviceBufferType(DeviceBufferType::Vertex);
	_deviceDataUpdateTracker.add_deviceBufferType(DeviceBufferType::Attributes);
	_deviceDataUpdateTracker.add_deviceBufferType(DeviceBufferType::Index);
	_deviceDataUpdateTracker.add_deviceBufferType(DeviceBufferType::PrimitiveInfo);
	_deviceDataUpdateTracker.add_deviceBufferType(DeviceBufferType::ViewProjMatrix);
	_deviceDataUpdateTracker.add_deviceBufferType(DeviceBufferType::ModelMatrix);
	_deviceDataUpdateTracker.add_deviceBufferType(DeviceBufferType::Material);
	_deviceDataUpdateTracker.add_deviceBufferType(DeviceBufferType::Texture);
	//----

	setup_drawContexts();
	setup_vertex_input();
	setup_descriptor_set();
	init_graphics_pipeline();
	init_imgui();
}

void Engine::run() {
	SDL_Event e;
	bool quit = false;

	while (!quit) {
		while (SDL_PollEvent(&e) != 0) {
			if (e.type == SDL_QUIT)
				quit = true;

			//Window Events
			if (e.window.event == SDL_WINDOWEVENT_MINIMIZED)
				stop_rendering = true;
			if (e.window.event == SDL_WINDOWEVENT_RESTORED)
				stop_rendering = false;
			if (e.window.event == SDL_WINDOWEVENT_RESIZED)
				windowResized = true;
			
			//Input - Event Type
			if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
				quit = true;

			if (e.key.keysym.scancode == SDL_SCANCODE_Z)
				if (e.key.type == SDL_KEYDOWN)
					SDL_SetRelativeMouseMode(SDL_GetRelativeMouseMode() == SDL_FALSE ? SDL_TRUE : SDL_FALSE);

			//GUI Events
			ImGui_ImplSDL2_ProcessEvent(&e);
		}

		if (stop_rendering) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		if (windowResized) {
			resize_swapchain();
		}
		
		//Input - State Type
		const uint8_t* keys = SDL_GetKeyboardState(NULL); //Updates cause SDL_PollEvents implicitly called SDL_PumpEvents

		//-Camera Input
		int rel_mouse_x, rel_mouse_y;
		SDL_GetRelativeMouseState(&rel_mouse_x, &rel_mouse_y);
		if (_camera.processInput(static_cast<uint32_t>(rel_mouse_x), static_cast<uint32_t>(rel_mouse_y), keys)) { //If camera received input/change in input
			_camera.update_view_matrix();
			_deviceDataUpdateTracker.signal_to_update(DeviceBufferType::ViewProjMatrix);
		}

		//Device Data Updates
		if (_deviceDataUpdateTracker.is_updatable(DeviceBufferType::ViewProjMatrix)) {
			_deviceDataUpdateTracker.acknowledge_update(DeviceBufferType::ViewProjMatrix);
			glm::mat4 view = _camera.get_view_matrix();
			size_t viewProjSize = sizeof(glm::mat4);
			VkBufferCopy copyInfo{ .srcOffset = 0, .dstOffset = 0, .size = viewProjSize };
			copy_to_device_buffer(get_current_frame().drawContext.viewprojMatrixBuffer, &view, viewProjSize, &copyInfo, 1);
		}

		//IMGUI Rendering
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();
		ImGui::ShowDemoWindow();
		ImGui::Render();

		//DRAW
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

	//Transition Images for Drawing
	vkutil::transition_image(cmd, _swapchain.images[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transition_image(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	//Clear Color
	VkClearColorValue clearValue = { {0.5f, 0.5f, 0.5f, 0.5f} };

	VkImageSubresourceRange clearRange = vkutil::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	vkCmdClearColorImage(cmd, _swapchain.images[swapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

	//Draw
	draw_geometry(cmd, swapchainImageIndex);

	//Draw GUI
	draw_imgui(cmd, _swapchain.imageViews[swapchainImageIndex]);

	//Transition for Presentation
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
	VkRenderingAttachmentInfo depthAttachment = vkutil::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo = vkutil::rendering_info(_swapchain.extent, &colorAttachment, &depthAttachment);
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

	//Bind Descriptor Set
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr);

	//Bind Vertex Input Buffers
	DrawContext& current_drawContext = get_current_frame().drawContext;
	std::vector<VkBuffer> vertexBuffers = { current_drawContext.vertexPosBuffer.buffer, current_drawContext.vertexOtherAttribBuffer.buffer }; //Jank Debug code will move vector to drawContext structure itself
	std::vector<VkDeviceSize> vertexOffsets = { 0, 0 };
	vkCmdBindVertexBuffers(cmd, 0, vertexBuffers.size(), vertexBuffers.data(), vertexOffsets.data());
	vkCmdBindIndexBuffer(cmd,current_drawContext.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	//Push Constants
	RenderShader::PushConstants pushconstants{};
	pushconstants.primitiveInfosBufferAddress = current_drawContext.primitiveInfosBufferAddress;
	pushconstants.viewProjMatrixBufferAddress = current_drawContext.viewprojMatrixBufferAddress;
	pushconstants.modelMatricesBufferAddress = current_drawContext.modelMatricesBufferAddress;
	pushconstants.materialsBufferAddress = current_drawContext.materialsBufferAddress;
	pushconstants.texturesBufferAddress = current_drawContext.texturesBufferAddress;

	vkCmdPushConstants(cmd, _pipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(RenderShader::PushConstants), &pushconstants);

	//Draw
	vkCmdDrawIndexedIndirect(cmd, current_drawContext.indirectDrawCommandsBuffer.buffer, 0, current_drawContext.drawCount, sizeof(VkDrawIndexedIndirectCommand));

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

	//Display Limits - DEBUG
	VkPhysicalDeviceLimits& limits = physicalDevice.properties.limits;
	std::cout << "Limits:\n" << std::format("maxVertexInputBindings: {}\nmaxVertexInputAttribues: {}\n"
				 "maxDescriptorSetUniformBuffers: {}\nmaxPerStageDescriptorUnifomrBuffers: {}\n"
				 "maxDescriptorSetStorageBuffers: {}\nmaxPerStageDescriptorStorageBuffers: {}", 
		limits.maxVertexInputBindings, limits.maxVertexInputAttributes,
		limits.maxDescriptorSetUniformBuffers, limits.maxPerStageDescriptorUniformBuffers,
		limits.maxDescriptorSetStorageBuffers, limits.maxPerStageDescriptorStorageBuffers) << std::endl;
	
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

	//Init Immediate Command Resources
	VK_CHECK(vkCreateCommandPool(_device, &cmdPoolInfo, nullptr, &_immCommandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo{};
	cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAllocInfo.pNext = nullptr;
	cmdAllocInfo.commandPool = _immCommandPool;
	cmdAllocInfo.commandBufferCount = 1;
	cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));

	_mainDeletionQueue.push_function([=]() {
		vkDestroyCommandPool(_device, _immCommandPool, nullptr);
		});
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

	//Init Immediate Command Sync Structure
	VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
	_mainDeletionQueue.push_function([=]() {
		vkDestroyFence(_device, _immFence, nullptr);
		});
}

void Engine::init_graphics_pipeline() {
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
	range.size = 40;

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

	_mainDeletionQueue.push_function([&]() {
		vkDestroyPipelineLayout(_device, _pipelineLayout, nullptr);
		vkDestroyPipeline(_device, _pipeline, nullptr);
		});
}

void Engine::draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView) {
	VkRenderingAttachmentInfo colorAttachment = vkutil::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = vkutil::rendering_info(_swapchain.extent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

void Engine::setup_vertex_input() {
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

void Engine::setup_descriptor_set() {
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

	_mainDeletionQueue.push_function([=, this]() {
		vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
		});

	//Create Descriptor Set Layout for Descriptors
	//-Set Layout Bindings
	std::vector<VkDescriptorSetLayoutBinding> layout_bindings = {
		{ .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = MAX_SAMPLED_IMAGE_COUNT,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr },
		{ .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = MAX_SAMPLER_COUNT, 
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

	_mainDeletionQueue.push_function([=, this]() {
		vkDestroyDescriptorSetLayout(_device, _descriptorSetLayout, nullptr);
		});

	//Create Descriptor Set
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &_descriptorSetLayout;
	
	if (vkAllocateDescriptorSets(_device, &allocInfo, &_descriptorSet) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate Descriptor Set");

	//Configure Descriptor in Descriptor Set with our data - Point to Buffers and Images
	std::vector<VkWriteDescriptorSet> descriptorWrites(2);

	std::vector<VkDescriptorImageInfo> sampledImages_imgInfos; 
	for (auto& image : _payload.images) {
		VkDescriptorImageInfo imgInfo{};
		imgInfo.imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
		imgInfo.imageView = image.imageView;
		sampledImages_imgInfos.push_back(imgInfo);
	}
;
	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = _descriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	descriptorWrites[0].descriptorCount = sampledImages_imgInfos.size();
	descriptorWrites[0].pImageInfo = sampledImages_imgInfos.data();

	std::vector<VkDescriptorImageInfo> sampler_imgInfos;
	for (auto& sampler : _payload.samplers) {
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

void Engine::setup_depthImage() {
	_depthImage.format = VK_FORMAT_D32_SFLOAT;
	VkExtent3D extent{};
	extent.width = _swapchain.extent.width;
	extent.height = _swapchain.extent.height;
	extent.depth = 1; 
	_depthImage.extent = extent;

	VkImageCreateInfo depth_image_info = vkutil::image_create_info(_depthImage.format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, _depthImage.extent);
	
	VmaAllocationCreateInfo depth_image_alloc_info{};
	depth_image_alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	depth_image_alloc_info.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(_allocator, &depth_image_info, &depth_image_alloc_info, &_depthImage.image, &_depthImage.allocation, nullptr);

	VkImageViewCreateInfo depth_image_view_info = vkutil::imageview_create_info(_depthImage.format, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(_device, &depth_image_view_info, nullptr, &_depthImage.imageView));

	_mainDeletionQueue.push_function([=, this]() {
		destroy_image(_depthImage);
		});
}

void Engine::setup_drawContexts() {
	//Holds all the data for one drawContext
	std::vector<VkDrawIndexedIndirectCommand> indirect_commands;
	uint32_t drawCount = 0;
	std::vector<glm::vec3> positions;
	std::vector<RenderShader::VertexAttributes> attributes;
	std::vector<uint32_t> indices;
	RenderShader::ViewProj viewproj;
	viewproj.view = _camera.get_view_matrix();
	viewproj.proj = glm::perspective(glm::radians(45.0f), _swapchain.extent.width / (float)_swapchain.extent.height, 50.0f, 0.1f);
	viewproj.proj[1][1] *= -1;
	std::vector<glm::mat4> model_matrices; //Also contains view and proj at start
	std::vector<RenderShader::PrimitiveInfo> primitiveInfos;
	std::vector<RenderShader::Material> materials;
	std::vector<RenderShader::Texture> textures;

	//VkBufferCopies to correctly copy data to respective regions in buffer in accordance to their ids
	VkBufferCopy viewprojMatrix_copy_info{ .srcOffset = 0, .dstOffset = 0, .size = sizeof(glm::mat4) * 2 };
	VkBufferCopy pos_copy_info;
	VkBufferCopy attrib_copy_info;
	VkBufferCopy index_copy_info;
	std::vector<VkBufferCopy> modelMatrices_copy_infos;
	std::vector<VkBufferCopy> indirect_copy_infos;
	std::vector<VkBufferCopy> primInfo_copy_infos;
	std::vector<VkBufferCopy> material_copy_infos;
	std::vector<VkBufferCopy> texture_copy_infos;

	//Get Our Data from Host
	//-Navigate Node Tree for Mesh Nodes and Get Primitive Data
	//--Add all root nodes in current scene to Sack
	std::stack<std::shared_ptr<Node>> dfs_node_stack;
	for (auto root_node : _payload.current_scene->root_nodes) {
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
				indirect_copy_infos.push_back({ .srcOffset = indirect_commands.size() * sizeof(VkDrawIndexedIndirectCommand), .dstOffset = primitive.getID() * sizeof(VkDrawIndexedIndirectCommand), .size = sizeof(VkDrawIndexedIndirectCommand) });
				VkDrawIndexedIndirectCommand indirect_command{};
				indirect_command.firstIndex = indices.size();
				indirect_command.indexCount = primitive.indices.size();
				indirect_command.vertexOffset = positions.size();
				indirect_command.firstInstance = 0;
				indirect_command.instanceCount = 1;;
				indirect_commands.push_back(indirect_command);

				drawCount++;

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

	//-Get Materials
	for (std::shared_ptr<Material>& material : _payload.materials) {
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
	for (auto& texture : _payload.textures) {
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
	size_t alloc_primInfo_size = sizeof(RenderShader::PrimitiveInfo) * primitiveInfos.size();
	size_t alloc_materials_size = sizeof(RenderShader::Material) * materials.size();
	size_t alloc_textures_size = sizeof(RenderShader::Texture) * textures.size();

	for (Frame& frame : _frames) {
		DrawContext& currentDrawContext = frame.drawContext;
		currentDrawContext.drawCount = drawCount;

		//Draw Coommand and Vertex Input Buffers
		currentDrawContext.indirectDrawCommandsBuffer = create_buffer(alloc_indirect_size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		currentDrawContext.vertexPosBuffer = create_buffer(alloc_vertPos_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		currentDrawContext.vertexOtherAttribBuffer = create_buffer(alloc_vertAttrib_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		currentDrawContext.indexBuffer = create_buffer(alloc_index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		//BDA Buffers
		currentDrawContext.viewprojMatrixBuffer = create_buffer(alloc_viewprojMatrix_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE); //Careful as if the alloc size is 0. Will cause errors
		currentDrawContext.modelMatricesBuffer = create_buffer(alloc_modelMatrices_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		currentDrawContext.primitiveInfosBuffer = create_buffer(alloc_primInfo_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		currentDrawContext.materialsBuffer = create_buffer(alloc_materials_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		currentDrawContext.texturesBuffer = create_buffer(alloc_textures_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
		VkBufferDeviceAddressInfo address_info{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		address_info.buffer = currentDrawContext.viewprojMatrixBuffer.buffer;
		currentDrawContext.viewprojMatrixBufferAddress = vkGetBufferDeviceAddress(_device, &address_info);
		address_info.buffer = currentDrawContext.modelMatricesBuffer.buffer;
		currentDrawContext.modelMatricesBufferAddress = vkGetBufferDeviceAddress(_device, &address_info);
		address_info.buffer = currentDrawContext.primitiveInfosBuffer.buffer;
		currentDrawContext.primitiveInfosBufferAddress = vkGetBufferDeviceAddress(_device, &address_info);
		address_info.buffer = currentDrawContext.materialsBuffer.buffer;
		currentDrawContext.materialsBufferAddress = vkGetBufferDeviceAddress(_device, &address_info);
		address_info.buffer = currentDrawContext.texturesBuffer.buffer;
		currentDrawContext.texturesBufferAddress = vkGetBufferDeviceAddress(_device, &address_info);

		//Copy Data to the Buffers
		copy_to_device_buffer(currentDrawContext.indirectDrawCommandsBuffer, indirect_commands.data(), alloc_indirect_size, indirect_copy_infos.data(), indirect_copy_infos.size());
		copy_to_device_buffer(currentDrawContext.vertexPosBuffer, positions.data(), alloc_vertPos_size, &pos_copy_info, 1);
		copy_to_device_buffer(currentDrawContext.vertexOtherAttribBuffer, attributes.data(), alloc_vertAttrib_size, &attrib_copy_info, 1);
		copy_to_device_buffer(currentDrawContext.indexBuffer, indices.data(), alloc_index_size, &index_copy_info, 1);
		copy_to_device_buffer(currentDrawContext.viewprojMatrixBuffer, &viewproj, alloc_viewprojMatrix_size, &viewprojMatrix_copy_info, 1);
		copy_to_device_buffer(currentDrawContext.modelMatricesBuffer, model_matrices.data(), alloc_modelMatrices_size, modelMatrices_copy_infos.data(), modelMatrices_copy_infos.size());
		copy_to_device_buffer(currentDrawContext.primitiveInfosBuffer, primitiveInfos.data(), alloc_primInfo_size, primInfo_copy_infos.data(), primInfo_copy_infos.size());
		copy_to_device_buffer(currentDrawContext.materialsBuffer, materials.data(), alloc_materials_size, material_copy_infos.data(), material_copy_infos.size());
		copy_to_device_buffer(currentDrawContext.texturesBuffer, textures.data(), alloc_textures_size, texture_copy_infos.data(), texture_copy_infos.size());

		//Put Buffers in Deletion Queueu
		_mainDeletionQueue.push_function([=, this]() {
			destroy_buffer(currentDrawContext.indirectDrawCommandsBuffer);
			destroy_buffer(currentDrawContext.vertexPosBuffer);
			destroy_buffer(currentDrawContext.vertexOtherAttribBuffer);
			destroy_buffer(currentDrawContext.indexBuffer);
			destroy_buffer(currentDrawContext.viewprojMatrixBuffer);
			destroy_buffer(currentDrawContext.modelMatricesBuffer);
			destroy_buffer(currentDrawContext.primitiveInfosBuffer);
			destroy_buffer(currentDrawContext.materialsBuffer);
			destroy_buffer(currentDrawContext.texturesBuffer);
			});
	}
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

void Engine::init_imgui() {
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

	VkDescriptorPool imguiPool;
	if (vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create Descriptor Pool for Imgui");

	ImGui::CreateContext();
	ImGui_ImplSDL2_InitForVulkan(_window);

	ImGui_ImplVulkan_InitInfo init_info{};
	init_info.Instance = _instance;
	init_info.PhysicalDevice = _physicalDevice;
	init_info.Device = _device;
	init_info.Queue = _graphicsQueue;
	init_info.DescriptorPool = imguiPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;
	init_info.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchain.format;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	
	ImGui_ImplVulkan_Init(&init_info);
	ImGui_ImplVulkan_CreateFontsTexture();
	
	_mainDeletionQueue.push_function([=]() {
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
		});
}

	AllocatedBuffer Engine::create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = memoryUsage;
	vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT; 
	AllocatedBuffer newBuffer;

	VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));
	return newBuffer;
}

void Engine::destroy_buffer(const AllocatedBuffer& buffer) {
	vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

AllocatedImage Engine::create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
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

	VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
	if (format == VK_FORMAT_D32_SFLOAT)
		aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;

	VkImageViewCreateInfo view_info = vkutil::imageview_create_info(format, newImage.image, aspectFlag);
	view_info.subresourceRange.levelCount = img_info.mipLevels;

	VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &newImage.imageView));

	return newImage;
}

AllocatedImage Engine::create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped) {
	//Staging Buffer Setup
	size_t data_size = size.depth * size.width * size.height * 4;

	AllocatedBuffer uploadBuffer = create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
	memcpy(uploadBuffer.info.pMappedData, data, data_size);

	AllocatedImage newImage = create_image(size, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);

	//Copy Data to New Image
	immediate_command_submit([&](VkCommandBuffer cmd) {
		vkutil::transition_image(cmd, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy copyRegion{};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = size;

		vkCmdCopyBufferToImage(cmd, uploadBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		vkutil::transition_image(cmd, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		});

	destroy_buffer(uploadBuffer);

	return newImage;
}

void Engine::immediate_command_submit(std::function<void(VkCommandBuffer cmd)>&& function) {
	VK_CHECK(vkResetFences(_device, 1, &_immFence));
	VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

	VkCommandBuffer cmd = _immCommandBuffer;
	
	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	function(cmd);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = vkutil::command_buffer_submit_info(cmd);
	VkSubmitInfo2 submit = vkutil::submit_info(&cmdInfo, nullptr, nullptr);

	VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));

	VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

void Engine::destroy_image(const AllocatedImage& img) {
	vkDestroyImageView(_device, img.imageView, nullptr);
	vmaDestroyImage(_allocator, img.image, img.allocation);
}

void Engine::copy_to_device_buffer(const AllocatedBuffer& dstBuffer, void* data, size_t dataSize, const VkBufferCopy* vkBufferCopy, uint32_t bufferCopiesCount) {
	AllocatedBuffer stagingBuffer = create_buffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST);
	memcpy(stagingBuffer.info.pMappedData, data, dataSize);

	immediate_command_submit([&](VkCommandBuffer cmd) {
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, dstBuffer.buffer, bufferCopiesCount, vkBufferCopy);
		});

	destroy_buffer(stagingBuffer);
}




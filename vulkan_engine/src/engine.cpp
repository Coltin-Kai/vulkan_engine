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
	
	//Initialize Vulkan Context
	_vkContext.init(_window);

	//Initalize Systems
	_renderSys.init(_windowExtent);

	//adfads
	setup_depthImage();
	setup_default_data();
	//LAZY CODE STUFF
	//-Load File Data
	loadGLTFFile(_vkContext, _payload, "C:\\Github\\vulkan_engine\\vulkan_engine\\assets\\Sample_Models\\BoomBox\\BoomBox.gltf"); //Exception expected to be thrown since allocated data in payload is not released
	
	_camera = Camera({ 0.0f, 0.0f, 0.15f });
	_camera.update_view_matrix();
	_payload.camera_transform = _camera.get_view_matrix();

	//Bind Images and Samplers
	_renderSys.bind_descriptors(_payload);
	//Upload Draw Data
	_renderSys.setup_drawContexts(_payload);
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
			int w;
			int h;
			SDL_GetWindowSize(_window, &w, &h);
			VkExtent2D windowExtent;
			windowExtent.width = w;
			windowExtent.height = h;
			_renderSys.resize_swapchain(windowExtent);
			windowResized = false;
		}
		
		//Input - State Type
		const uint8_t* keys = SDL_GetKeyboardState(NULL); //Updates cause SDL_PollEvents implicitly called SDL_PumpEvents

		//-Camera Input
		int rel_mouse_x, rel_mouse_y;
		SDL_GetRelativeMouseState(&rel_mouse_x, &rel_mouse_y);
		if (_camera.processInput(static_cast<uint32_t>(rel_mouse_x), static_cast<uint32_t>(rel_mouse_y), keys)) { //If camera received input/change in input
			_camera.update_view_matrix();
			_payload.camera_transform = _camera.get_view_matrix();
			_renderSys.signal_to_updateDeviceBuffer(DeviceBufferType::ViewProjMatrix);
		}

		//Device Data Updates
		_renderSys.updateSignaledDeviceBuffers(_payload);

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
	vkDeviceWaitIdle(_vkContext.device);

	//Depth Image
	_vkContext.destroy_image(_depthImage);

	//Payload Cleanup
	for (VkSampler& sampler : _payload.samplers) {
		_vkContext.destroy_sampler(sampler);
	}

	for (AllocatedImage& image : _payload.images) {
		_vkContext.destroy_image(image);
	}
	
	//Deletion Queue
	_mainDeletionQueue.flush();

	//RenderingSystem Cleanup
	_renderSys.shutdown();

	//Vulkan Cleanup
	_vkContext.shutdown();

	//SDL Cleanup
	SDL_DestroyWindow(_window);
}

void Engine::draw() {
	VkResult result;
	VkCommandBuffer cmd = _renderSys.startFrame(result);
	Image swapchainImage = _renderSys.get_currentSwapchainImage();

	if (result == VK_ERROR_OUT_OF_DATE_KHR) { //Failed to acquire swapchain image
		windowResized = true;
		return;
	}
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		throw std::runtime_error("Failed to acquire Swap Chain Images");

	//Transition Images for Drawing
	vkutil::transition_image(cmd, swapchainImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transition_image(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	//Clear Color
	VkClearColorValue clearValue = { {0.5f, 0.5f, 0.5f, 0.5f} };

	VkImageSubresourceRange clearRange = vkutil::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	vkCmdClearColorImage(cmd, swapchainImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

	//Draw
	draw_geometry(cmd, swapchainImage);

	//Draw GUI
	draw_imgui(cmd, swapchainImage);

	//Transition for Presentation
	vkutil::transition_image(cmd, swapchainImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	_renderSys.endFrame(result);

	if (result == VK_ERROR_OUT_OF_DATE_KHR)
		windowResized = true;
	else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		throw std::runtime_error("Failed to Present!");
}

void Engine::draw_geometry(VkCommandBuffer cmd, const Image& swapchainImage) {
	VkRenderingAttachmentInfo colorAttachment = vkutil::attachment_info(swapchainImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo depthAttachment = vkutil::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkExtent2D swapchainExtent = _renderSys.get_swapChainExtent();

	VkRenderingInfo renderInfo = vkutil::rendering_info(swapchainExtent, &colorAttachment, &depthAttachment);
	vkCmdBeginRendering(cmd, &renderInfo);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _renderSys._pipeline);

	//Set Dynamic States
	VkViewport viewport{};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = swapchainExtent.width;
	viewport.height = swapchainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = swapchainExtent.width;
	scissor.extent.height = swapchainExtent.height;

	vkCmdSetScissor(cmd, 0, 1, &scissor);

	//Bind Descriptor Set
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _renderSys._pipelineLayout, 0, 1, &_renderSys._descriptorSet, 0, nullptr);

	//Bind Vertex Input Buffers
	std::vector<VkBuffer> vertexBuffers = _renderSys.get_vertexBuffers(); //Jank Debug code will move vector to drawContext structure itself
	std::vector<VkDeviceSize> vertexOffsets = { 0, 0 };
	vkCmdBindVertexBuffers(cmd, 0, vertexBuffers.size(), vertexBuffers.data(), vertexOffsets.data());
	vkCmdBindIndexBuffer(cmd, _renderSys.get_indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

	//Push Constants
	RenderShader::PushConstants pushconstants = _renderSys.get_pushConstants();
	vkCmdPushConstants(cmd, _renderSys._pipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(RenderShader::PushConstants), &pushconstants);

	//Draw
	vkCmdDrawIndexedIndirect(cmd, _renderSys.get_indirectDrawBuffer(), 0, _renderSys.get_drawCount(), sizeof(VkDrawIndexedIndirectCommand));

	vkCmdEndRendering(cmd);
}

void Engine::draw_imgui(VkCommandBuffer cmd, const Image& swapchainImage) {
	VkRenderingAttachmentInfo colorAttachment = vkutil::attachment_info(swapchainImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = vkutil::rendering_info(_renderSys.get_swapChainExtent(), &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

void Engine::setup_depthImage() {
	/*
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
	*/
	VkExtent2D swapchainExtent = _renderSys.get_swapChainExtent();
	VkExtent3D depthExtent;
	depthExtent.width = swapchainExtent.width;
	depthExtent.height = swapchainExtent.height;
	depthExtent.depth = 1;
	_depthImage = _vkContext.create_image("Depth Image", depthExtent, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, false);
}

void Engine::setup_default_data() {
	uint32_t default_data = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	VkExtent3D extent{};
	extent.width = 1;
	extent.height = 1;
	extent.depth = 1;
	AllocatedImage default_image = _vkContext.create_image("Default Image", extent, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, true);
	_vkContext.update_image(default_image,(void*)&default_data, extent);
	_payload.images.push_back(default_image);

	VkSampler default_sampler;
	VkSamplerCreateInfo sampler_info{};
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.maxLod = VK_LOD_CLAMP_NONE;
	sampler_info.minLod = 0;
	sampler_info.magFilter = VK_FILTER_LINEAR;
	sampler_info.minFilter = VK_FILTER_LINEAR;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	default_sampler = _vkContext.create_sampler(sampler_info);
	_payload.samplers.push_back(default_sampler);

	Texture default_texture{};
	default_texture.name = "default";
	default_texture.image_index = 0;
	default_texture.sampler_index = 0;
	_payload.textures.push_back(std::make_shared<Texture>(default_texture));

	Material default_material{};
	default_material.name = "default";
	_payload.materials.push_back(std::make_shared<Material>(default_material));
}

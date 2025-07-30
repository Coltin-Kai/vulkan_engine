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
	_guiSys.init(_window, _renderSys.get_swapChainFormat());

	setup_default_data();
	//LAZY CODE STUFF
	//-Load File Data
	loadGLTFFile(_vkContext, _payload, "C:\\Github\\vulkan_engine\\vulkan_engine\\assets\\Sample_Models\\BoomBox\\BoomBox.gltf"); //Exception expected to be thrown since allocated data in payload is not released
	
	_camera = Camera({ 0.0f, 0.0f, 0.15f });
	_camera.update_view_matrix();
	_payload.camera_transform = _camera.get_view_matrix();
	_payload.proj_transform = glm::perspective(glm::radians(45.0f), _windowExtent.width / (float)_windowExtent.height, 50.0f, 0.01f);
	_payload.proj_transform[1][1] *= -1;

	//Light
	_payload.pointLights.push_back({ .pos = glm::vec3(0.0f, 2.0f, -2.0f), .color = glm::vec3(1.0f, 1.0f, 1.0f), .power = 100.0f });
	_payload.pointLights.push_back({ .pos = glm::vec3(-2.0f, 2.0f, 2.0f) , .color = glm::vec3(1.0f, 0.0f, 0.0f), .power = 100.0f });

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
			_windowExtent.width = w;
			_windowExtent.height = h;
			_renderSys.resize_swapchain(_windowExtent);
			_payload.proj_transform = glm::perspective(glm::radians(45.0f), _windowExtent.width / (float)_windowExtent.height, 50.0f, 0.01f);
			_payload.proj_transform[1][1] *= -1;
			DeviceBufferTypeFlags dataType;
			dataType.viewProjMatrix = true;
			_renderSys.signal_to_updateDeviceBuffers(dataType);
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
			_payload.cam_pos = _camera.pos;
			DeviceBufferTypeFlags dataType;
			dataType.viewProjMatrix = true;
			_renderSys.signal_to_updateDeviceBuffers(dataType);
		}

		VkResult result;
		_guiSys.run(_guiParam, _payload);

		if (_guiParam.fileOpened) {
			_guiParam.fileOpened = false;
			loadGLTFFile(_vkContext, _payload, _guiParam.OpenedFilePath); //Testing this
			DeviceBufferTypeFlags dataType;
			dataType.setAll();
			_renderSys.signal_to_updateDeviceBuffers(dataType);
			_renderSys.bind_descriptors(_payload);
		}

		if (_guiParam.sceneChanged) {
			_guiParam.sceneChanged = false;
			//_renderSys.signal_to_updateDeviceBuffer(DeviceBufferType::ModelMatrix | DeviceBufferType::IndirectDraw | DeviceBufferType::PrimitiveID | DeviceBufferType::Vertex | DeviceBufferType::Index | DeviceBufferType::PrimitiveInfo);
			DeviceBufferTypeFlags dataType;
			dataType.modelMatrix = true;
			dataType.indirectDraw = true;
			dataType.primID = true;
			dataType.primInfo = true;
			dataType.vertex = true;
			dataType.index = true;
			_renderSys.signal_to_updateDeviceBuffers(dataType); //Need to fix stuff to ensure that it only updates what is neccesary instead of All
		}

		//Render System Device Data/Render Data Updates
		_renderSys.updateSignaledDeviceBuffers(_payload);

		result = _renderSys.run();
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			windowResized = true;
			continue;
		}
	}
}

void Engine::cleanup() {
	vkDeviceWaitIdle(_vkContext.device);

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
	_guiSys.shutdown();
	_renderSys.shutdown();

	//Vulkan Cleanup
	_vkContext.shutdown();

	//SDL Cleanup
	SDL_DestroyWindow(_window);
}

void Engine::setup_default_data() {
	uint32_t default_data = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	VkExtent3D extent{};
	extent.width = 1;
	extent.height = 1;
	extent.depth = 1;
	AllocatedImage default_image = _vkContext.create_image("Default Image", extent, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, true);
	_vkContext.update_image(default_image,(void*)&default_data, extent.width * extent.height * extent.depth * 4);
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

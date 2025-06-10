#pragma once

#include "thirdparty_defines.h"

#include "vulkan/vulkan.h"

#include "SDL.h"
#include "SDL_vulkan.h"

#include "vk_mem_alloc.h"

#include "glm.hpp"
#include "gtc/matrix_transform.hpp"
#include "vec3.hpp"

#include "fastgltf/core.hpp"

#include "vulkan_helper_types.h"
#include "graphic_data_types.h"

#include "vulkanContext.h"
#include "Camera.h"
#include "renderSystem.h"	
#include "guiSystem.h"

#include <vector>
#include <deque>
#include <functional>
#include <filesystem>
#include <map>

class Engine {
public:
	//Initalize the Engine
	void init();
	
	//Main Loop
	void run();

	//Shut down Engine
	void cleanup();

private:
	struct DeletionQueue {
		std::deque<std::function<void()>> deletors;

		void push_function(std::function<void()>&& function) {
			deletors.push_back(function);
		}

		void flush() {
			// reverse iterate the deletion queue to execute all the functions
			for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
				(*it)();
			}

			deletors.clear();
		}
	};

	bool stop_rendering = false;

	//SDL Window
	struct SDL_Window* _window{ nullptr };
	VkExtent2D _windowExtent{ 1700, 900 };
	
	//VulkanContext
	VulkanContext _vkContext;

	//Systems
	RenderSystem _renderSys{ _vkContext };
	GUISystem _guiSys{ _vkContext };

	//Swapchain
	bool windowResized = false;

	//Deletion Queue
	DeletionQueue _mainDeletionQueue;

	//Camera
	Camera _camera;

	//Payload and stuff
	GraphicsDataPayload _payload;
	GUIParameters _guiParam;

	void setup_default_data();
};
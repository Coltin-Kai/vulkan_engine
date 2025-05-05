#pragma once

#include "thirdparty_defines.h"

#include "vulkan/vulkan.h"

#include "SDL.h"
#include "SDL_vulkan.h"

#include "vk_mem_alloc.h"

#include "glm.hpp"
#include "vec3.hpp"

#include "fastgltf/core.hpp"

#include "vulkan_helper_types.h"
#include "graphic_data_types.h"

#include "vulkanContext.h"
#include "Camera.h"
#include "renderSystem.h"	

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

	//May combine the device data mapper and tracker to a whole class/struct that manages the relationship between host and device data
	//DEBUG - Maps host data to respective buffer regions via their id.
	struct DeviceDataMapper {
		std::unordered_map<uint32_t,DataRegion> PrimitiveID_to_PrimitivePositionsRegions; //Each region of position data in buffer for each primtive. Mapped to Primitive IDs.
		std::unordered_map<uint32_t,DataRegion> PrimitiveID_to_PrimitiveAttributesRegions; //Each region of attrib data in buffer for each primitive. Mapped to Primitive IDs.
		std::unordered_map<uint32_t,DataRegion> PrimitiveID_to_PrimitiveIndicesRegions; //Each region of index data in buffer for each primitive. Mapped to Primitive IDs.
		//Vectors to keep track of Region order in buffer. Use the containing IDs to access Regions in Maps.
		std::vector<uint32_t> primitiveVerticeRegions; //Ordered array of the regions of vertex data in the buffer by containing their PrimitiveID
		std::vector<uint32_t> primitiveIndicesRegions; //Ordered array of the regions of index data in the buffer by containing their PrimitiveiD.
	};

	bool stop_rendering = false;

	//SDL Window
	struct SDL_Window* _window{ nullptr };
	VkExtent2D _windowExtent{ 1700, 900 };
	
	//VulkanContext
	VulkanContext _vkContext;

	//MyDevice
	RenderSystem _renderSys{ _vkContext };

	//Swapchain
	bool windowResized = false;

	//Depth Image
	AllocatedImage _depthImage;

	//Deletion Queue
	DeletionQueue _mainDeletionQueue;

	//Camera
	Camera _camera;

	//Payload
	GraphicsDataPayload _payload;

	//Data Update Stuff
	DeviceDataMapper _deviceDataMapper;

	void draw();

	void draw_geometry(VkCommandBuffer cmd, const Image& swapchainImage);

	void draw_imgui(VkCommandBuffer cmd, const Image& swapchainImage);

	void setup_depthImage();

	void setup_default_data();
};
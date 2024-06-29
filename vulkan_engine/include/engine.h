#pragma once
#include <vector>

#include "vulkan/vulkan.h"

#define SDL_MAIN_HANDLED //Needed as SDL defines main macro that can conflict with app main
#include "SDL.h"
#include "SDL_vulkan.h"

//Config
constexpr unsigned int FRAMES_TOTAL = 2;

class Engine {
public:
	//Initalize the Engine
	void init();
	
	//Main Loop
	void run();

	//Shut down Engine
	void cleanup();

private:
	struct Swapchain {
		VkSwapchainKHR vkSwapchain;
		VkFormat format;
		std::vector<VkImage> images;
		std::vector<VkImageView> imageViews;
		VkExtent2D extent;
	};

	struct Frame {
		VkCommandPool commandPool;
		VkCommandBuffer commandBuffer;

		VkSemaphore swapchainSemaphore, renderSemaphore;
		VkFence renderFence;
	};

	bool stop_rendering = false;

	//SDL Window
	struct SDL_Window* _window{ nullptr };
	VkExtent2D _windowExtent{ 1700, 900 };

	//Vulkan Components
	VkInstance _instance; //Vulkan Instance
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkPhysicalDevice _physicalDevice;
	VkDevice _device;
	VkSurfaceKHR _surface;

	//Swapchain
	Swapchain _swapchain;

	Frame _frames[FRAMES_TOTAL];
	int _frameNumber = 0;
	Frame& get_current_frame() { return _frames[_frameNumber % FRAMES_TOTAL]; };

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	//Draw and Render
	void draw();

	//Initialize Vulkan Components
	void init_vulkan();

	//Initialize Swapchain
	void init_swapchain();

	void init_commands();

	void init_sync_structures();

	void destroy_swapchain();
};
#pragma once

#include "vulkan/vulkan.h"
#include "SDL.h"

class Engine {
public:
	bool stop_rendering = false;
	struct SDL_Window* _window{ nullptr };
	VkExtent2D _windowExtent{ 1700, 900 };

	//Initalize the Engine
	void init();
	
	//Main Loop
	void run();

	//Shut down Engine
	void cleanup();

private:
	//Draw and Render
	void draw();
};
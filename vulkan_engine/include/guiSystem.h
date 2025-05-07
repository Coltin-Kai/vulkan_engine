#pragma once

#include "thirdparty_defines.h"

#include "SDL.h"
#include "SDL_vulkan.h"
#include "vulkan/vulkan.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include "vulkanContext.h"

class GUISystem {
public:
	//IMGUI Pool

	GUISystem(VulkanContext& vkContext) : _vkContext(vkContext) {}

	VkDescriptorPool _imguiDescriptorPool;

	void init(SDL_Window* window, const VkFormat& swapChainFormat);
	void run();
	void shutdown();

private:
	VulkanContext& _vkContext;

	void init_imgui(SDL_Window* window, const VkFormat& swapChainFormat);
};
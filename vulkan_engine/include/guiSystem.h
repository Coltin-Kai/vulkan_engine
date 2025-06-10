#pragma once

#include "thirdparty_defines.h"

#include "SDL.h"
#include "SDL_vulkan.h"
#include "vulkan/vulkan.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"
#include "imfilebrowser.h"

#include "vulkanContext.h"
#include "graphic_data_types.h"

struct GUIParameters { //Used to pass GUI Parameters and values outside of GUISystem
	bool fileOpened;
	std::string OpenedFilePath;

	bool sceneChanged;
};

class GUISystem {
public:
	//IMGUI Pool

	GUISystem(VulkanContext& vkContext) : _vkContext(vkContext) {}

	VkDescriptorPool _imguiDescriptorPool;

	void init(SDL_Window* window, const VkFormat& swapChainFormat);
	void run(GUIParameters& param, GraphicsDataPayload& graphics_payload);
	void shutdown();

private:
	VulkanContext& _vkContext;

	ImGui::FileBrowser fileExplorer{};

	void init_imgui(SDL_Window* window, const VkFormat& swapChainFormat);
};
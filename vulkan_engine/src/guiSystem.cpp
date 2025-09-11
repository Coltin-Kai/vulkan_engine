#include "guiSystem.h"

void GUISystem::init(SDL_Window* window, const VkFormat& swapChainFormat) {
	init_imgui(window, swapChainFormat);

	//Init File Explorer
	fileExplorer.SetTitle("Load 3D File");
	fileExplorer.SetTypeFilters({ ".gltf" });
}

void GUISystem::run(GUIParameters& param, GraphicsDataPayload& graphics_payload) {
	//GUI
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();

	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Open")) {
				fileExplorer.Open();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	if (ImGui::Begin("Menu")) {
		ImGui::SeparatorText("Scene");
		//Select Scene Combo
		std::string combo_preview_value = graphics_payload.scenes[graphics_payload.current_scene_idx].name;
		if (ImGui::BeginCombo("Scene", combo_preview_value.c_str())) {
			for (int i = 0; i < graphics_payload.scenes.size(); i++) {
				const bool is_selected = (graphics_payload.current_scene_idx == i);
				std::string label = graphics_payload.scenes[i].name + std::format("##{}", i); //Assign unique label ID to selectable
				if (ImGui::Selectable(label.c_str(), is_selected)) { //If a Combo option was selected
					if (graphics_payload.current_scene_idx != i) { //If the Combo option selected isn't just the current scene, change scene
						graphics_payload.current_scene_idx = i;
						param.sceneChanged = true;
					}
				}

				if (is_selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		//ImGui::SeparatorText("Node Tree");
		ImGui::End();
	}

	//File Explorer
	fileExplorer.Display();
	if (fileExplorer.HasSelected()) {
		param.fileOpened = true;
		param.OpenedFilePath = fileExplorer.GetSelected().string();
		fileExplorer.ClearSelected();
	}

	ImGui::Render();
}

void GUISystem::shutdown() {
	//Cleanup Imgui
	ImGui_ImplVulkan_Shutdown();
	vkDestroyDescriptorPool(_vkContext.device, _imguiDescriptorPool, nullptr);
}

void GUISystem::init_imgui(SDL_Window* window, const VkFormat& swapChainFormat) {
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

	if (vkCreateDescriptorPool(_vkContext.device, &pool_info, nullptr, &_imguiDescriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create Descriptor Pool for Imgui");

	ImGui::CreateContext();
	ImGui_ImplSDL2_InitForVulkan(window);

	ImGui_ImplVulkan_InitInfo init_info{}; 
	init_info.Instance = _vkContext.instance;
	init_info.PhysicalDevice = _vkContext.physicalDevice;
	init_info.Device = _vkContext.device;
	init_info.Queue = _vkContext.primaryQueue;
	init_info.DescriptorPool = _imguiDescriptorPool;
	init_info.MinImageCount = 3;
	init_info.ImageCount = 3;
	init_info.UseDynamicRendering = true;
	init_info.PipelineRenderingCreateInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	init_info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
	init_info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapChainFormat;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);
	ImGui_ImplVulkan_CreateFontsTexture();
}

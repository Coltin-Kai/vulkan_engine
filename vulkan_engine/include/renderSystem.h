#pragma once

#include "thirdparty_defines.h"

#include "SDL.h"
#include "SDL_vulkan.h"
#include "vulkan/vulkan.h"
#include "vk_mem_alloc.h"
#include "VkBootstrap.h"
#include "gtc/matrix_transform.hpp"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include "vulkanContext.h"
#include "vulkan_helper_types.h"
#include "vulkan_helper_functions.h"
#include "graphic_data_types.h"
#include "checkVkResult.h"
#include "shader_types.h"
#include "pipeline.h"
#include <unordered_map>

constexpr unsigned int FRAMES_TOTAL = 2;

//-Descriptor Settings
constexpr unsigned int UNIFORM_DESCRIPTOR_COUNT = 500;
constexpr uint32_t MAX_SAMPLED_IMAGE_COUNT = 100;
constexpr uint32_t MAX_SAMPLER_COUNT = 100;

enum class DeviceBufferType {
	IndirectDraw,
	Vertex,
	Attributes,
	Index,
	PrimitiveID,
	PrimitiveInfo,
	ViewProjMatrix,
	ModelMatrix,
	Material,
	Texture
};

class RenderSystem { 
public:
	//Need to figure out how to handle these public variables. Getters? Or performs commands involving these variables only inside the device class. Or just keep them public.
	VkSurfaceKHR _surface;

	//Descriptors
	VkDescriptorPool _descriptorPool;
	VkDescriptorSetLayout _descriptorSetLayout;
	VkDescriptorSet _descriptorSet;

	//Graphics Pipeline
	VkPipelineLayout _pipelineLayout;
	VkPipeline _pipeline;

	RenderSystem(VulkanContext& vkContext) : _vkContext(vkContext){}

	void init(VkExtent2D windowExtent);
	VkResult run();
	void shutdown();

	//Swapchain
	Image get_currentSwapchainImage();
	VkExtent2D get_swapChainExtent();
	VkFormat get_swapChainFormat();

	void resize_swapchain(VkExtent2D windowExtent);
	void bind_descriptors(GraphicsDataPayload& payload);
	void setup_drawContexts(const GraphicsDataPayload& payload);
	void signal_to_updateDeviceBuffer(DeviceBufferType bufferType);
	void updateSignaledDeviceBuffers(GraphicsDataPayload& payload);
private:
	struct Swapchain {
		VkSwapchainKHR vkSwapchain;
		VkFormat format;
		std::vector<Image> images;
		VkExtent2D extent;
	};

	//Big Structure that enxapsulates all data neccesary for one draw. Vertex and Index, UBO, SBO, etc. Maybe Represents the Current Scene
	struct DrawContext {
		//Draw Resources
		AllocatedBuffer indirectDrawCommandsBuffer; //Global Buffer that holds the draw data for each and every primitive/batched data
		uint32_t drawCount; //How many draws total in the commands buffer
		AllocatedBuffer vertexPosBuffer; //Global Buffer containing every vertex's position for the draw
		AllocatedBuffer vertexOtherAttribBuffer; //Global Buffer containing every vertex's other attributes besides position, uvs, vertex_colors.
		AllocatedBuffer indexBuffer;

		//Buffer Ressources
		AllocatedBuffer primitiveIdsBuffer;
		VkDeviceAddress primitiveIdsBufferAddress;
		AllocatedBuffer primitiveInfosBuffer;
		VkDeviceAddress primitiveInfosBufferAddress;
		AllocatedBuffer viewprojMatrixBuffer;
		VkDeviceAddress viewprojMatrixBufferAddress;
		AllocatedBuffer modelMatricesBuffer;
		VkDeviceAddress modelMatricesBufferAddress;
		AllocatedBuffer materialsBuffer;
		VkDeviceAddress materialsBufferAddress;
		AllocatedBuffer texturesBuffer;
		VkDeviceAddress texturesBufferAddress;
	};

	struct Frame {
		VkCommandPool commandPool;
		VkCommandBuffer commandBuffer;

		VkSemaphore swapchainSemaphore, renderSemaphore;
		VkFence renderFence;

		DrawContext drawContext;
	};

	//Vulkan Context
	VulkanContext& _vkContext;

	//Swapchain
	Swapchain _swapchain;
	uint32_t _swapchainImageIndex; //Represents the current image that will be presented

	//Frames
	Frame _frames[FRAMES_TOTAL];
	int _frameNumber = 0;
	Frame& get_current_frame() { return _frames[_frameNumber % FRAMES_TOTAL]; }
	void go_next_frame() { _frameNumber++;  }

	//Vertex Input
	std::vector<VkVertexInputBindingDescription> _bindingDescriptions;
	std::vector<VkVertexInputAttributeDescription>_attribueDescriptions;

	//Device Data Updates
	std::unordered_map<DeviceBufferType, int> _deviceBufferTypesCounter;

	//Depth Image
	AllocatedImage _depthImage;

	void init_swapchain(VkExtent2D windowExtent);
	void init_frames();
	void init_vertexInput();
	void init_descriptorSet();
	void init_graphicsPipeline();
	
	//Draw
	VkResult draw(); //Maybe move draw commands to rendersystem object.
	void draw_geometry(VkCommandBuffer cmd, const Image& swapchainImage);
	void draw_gui(VkCommandBuffer cmd, const Image& swapchainImage);
	
	//DrawContext
	std::vector<VkBuffer> get_vertexBuffers();
	VkBuffer get_indexBuffer();
	RenderShader::PushConstants get_pushConstants();
	VkBuffer get_indirectDrawBuffer();
	uint32_t get_drawCount();

	//Depth Image
	void setup_depthImage();

	void destroy_swapchain();
};
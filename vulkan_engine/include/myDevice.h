#pragma once

#define SDL_MAIN_HANDLED //Needed as SDL defines main macro that can conflict with app main
#include "SDL.h"
#include "SDL_vulkan.h"
#include "vulkan/vulkan.h"

#include "vk_mem_alloc.h"
#include "VkBootstrap.h"
#include "gtc/matrix_transform.hpp"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

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

class MyDevice { //Representation of Abstracted GPU MyDevice and a lot of related objects and Vulkan operations
public:
	//Need to figure out how to handle these public variables. Getters? Or performs commands involving these variables only inside the device class. Or just keep them public.
	VkDevice _device;
	VkSurfaceKHR _surface;
	VkQueue _graphicsQueue;

	//Descriptors
	VkDescriptorPool _descriptorPool;
	VkDescriptorSetLayout _descriptorSetLayout;
	VkDescriptorSet _descriptorSet;

	//Graphics Pipeline
	VkPipelineLayout _pipelineLayout;
	VkPipeline _pipeline;

	void init(SDL_Window* window, VkExtent2D windowExtent);
	void shutdown();

	//Buffer and Image Operations
	AllocatedBuffer create_buffer(const char* name, size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocationCreateFlags allocFlags);
	AllocatedBuffer create_buffer(const char* name, VkBufferCreateInfo bufferInfo, VmaAllocationCreateInfo allocInfo); //WHen Buffer Creation requires more specific details
	void destroy_buffer(const AllocatedBuffer& buffer);
	void update_buffer(const AllocatedBuffer& buffer, void* srcData, size_t srcDataSize, VkBufferCopy& copyInfo);
	void update_buffer(const AllocatedBuffer& buffer, void* srcData, size_t srcDataSize, std::vector<VkBufferCopy>& copyInfos);

	AllocatedImage create_image(const char* name, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped);
	AllocatedImage create_image(const char* name, VkImageCreateInfo imageInfo, VmaAllocationCreateInfo allocInfo); //When Image Create requires more specific details
	void destroy_image(const AllocatedImage& image);
	void update_image(const AllocatedImage& image, void* srcData, VkExtent3D imageSize); //Uploads raw data to an image

	VkSampler create_sampler(VkSamplerCreateInfo& samplerCreateInfo);
	void destroy_sampler(const VkSampler& sampler);

	//Commands
	VkCommandBuffer start_immediate_recording(); //Begins Immediate Command Recording and returns the cmd buffer to record commands.
	void submit_immediate_commands();

	//Frame
	VkCommandBuffer startFrame(VkResult& result); //Returns the current frame's command buffer and starts recording
	void endFrame(VkResult& result); //Submits the current frame's commands to the queue

	//Swapchain
	Image get_currentSwapchainImage();
	VkExtent2D get_swapChainExtent();
	void resize_swapchain(VkExtent2D windowExtent);

	//Descriptors
	void bind_descriptors(GraphicsDataPayload& payload);

	//Draw Context
	void setup_drawContexts(const GraphicsDataPayload& payload);
	std::vector<VkBuffer> get_vertexBuffers();
	VkBuffer get_indexBuffer();
	RenderShader::PushConstants get_pushConstants();
	VkBuffer get_indirectDrawBuffer();
	uint32_t get_drawCount();

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

	//Vulkan Components
	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkPhysicalDevice _physicalDevice;
	uint32_t _graphicsQueueFamily;

	//VMA Allocator
	VmaAllocator _allocator;

	//Swapchain
	Swapchain _swapchain;
	uint32_t _swapchainImageIndex; //Represents the current image that will be presented

	//Frames
	Frame _frames[FRAMES_TOTAL];
	int _frameNumber = 0;
	Frame& get_current_frame() { return _frames[_frameNumber % FRAMES_TOTAL]; }
	void go_next_frame() { _frameNumber++;  }

	//Immediate Commands
	VkCommandPool _immCommandPool;
	VkCommandBuffer _immCommandBuffer;
	VkFence _immFence;

	//Vertex Input
	std::vector<VkVertexInputBindingDescription> _bindingDescriptions;
	std::vector<VkVertexInputAttributeDescription>_attribueDescriptions;

	//IMGUI Pool
	VkDescriptorPool _imguiDescriptorPool;

	//Device Data Updates
	std::unordered_map<DeviceBufferType, int> _deviceBufferTypesCounter;

	void init_vulkan(SDL_Window* window);
	void init_swapchain(VkExtent2D windowExtent);
	void init_commands();
	void init_syncStructurrs();
	void init_vertexInput();
	void init_descriptorSet();
	void init_graphicsPipeline();
	void init_imgui(SDL_Window* window);

	void destroy_swapchain();
};
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
#include "stb_image.h" //Temp Include until move HDR loading code to loader

#include "vulkanContext.h"
#include "vulkan_helper_types.h"
#include "vulkan_helper_functions.h"
#include "graphic_data_types.h"
#include "checkVkResult.h"
#include "shader_types.h"
#include "pipeline.h"
#include <unordered_map>
#include <array>

constexpr unsigned int FRAMES_TOTAL = 2;

//-Descriptor Settings
constexpr uint32_t MAX_SAMPLED_IMAGE_COUNT = 100;
constexpr uint32_t MAX_SAMPLER_COUNT = 100;

enum class DeviceBufferType {
	ViewProj,
	Indirect,
	PrimID,
	PrimInfo,
	Model,
	Index,
	Vertex,
	Material,
	Texture,
	Light
};

struct DeviceBufferTypeFlags {
	bool viewProjMatrix = false;
	bool indirectDraw = false;
	bool primID = false;
	bool primInfo = false;
	bool modelMatrix = false;
	bool index = false;
	bool vertex = false;
	bool material = false;
	bool texture = false;
	bool light = false;

	void setAll() {
		viewProjMatrix = true;
		indirectDraw = true;
		primID = true;
		primInfo = true;
		modelMatrix = true;
		index = true;
		vertex = true;
		material = true;
		texture = true;
		light = true;
	}
};

class RenderSystem { 
public:
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
	void signal_to_updateDeviceBuffers(DeviceBufferTypeFlags deviceBufferTypes);
	void updateSignaledDeviceBuffers(const GraphicsDataPayload& payload);
	void setup_hdrMap2();
	void setup_skybox();
private:
	struct Swapchain {
		VkSwapchainKHR vkSwapchain;
		VkFormat format;
		std::vector<Image> images;
		VkExtent2D extent;
	};

	//Big Structure that encapsulates all resources neccesary for one draw. Vertex and Index, UBO, SBO, etc. Maybe Represents the Current Scene
	struct DrawContext {
		//Draw Resources
		
		uint32_t drawCount; //How many draws total in the commands buffer (for the current scene)
		AllocatedBuffer indirectDrawCommandsBuffer; //Global Buffer that holds the draw data for each and every primitive/batched data
		AllocatedBuffer vertexPosBuffer; //Global Buffer containing every vertex's position for the draw
		AllocatedBuffer vertexOtherAttribBuffer; //Global Buffer containing every vertex's other attributes besides position, uvs, vertex_colors.
		AllocatedBuffer indexBuffer;

		//Buffer Resources - Geometry Rendering
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
		AllocatedBuffer lightsBuffer;
		VkDeviceAddress lightsBufferAddress;

		//Buffer Resources - Skybox
		AllocatedBuffer skybox_viewprojMatrixBuffer; 
	};

	//Represents all the types of data needed to populate Draw Context's Buffers used in Render Shader and Drawing. Need this in order to format most of the data as continous memory for memcpying + Easy to pass all the data from the extract function using a struct
	struct RenderShaderData {
		std::vector<VkDrawIndexedIndirectCommand> indirect_commands;
		std::vector<glm::vec3> positions;
		std::vector<RenderShader::VertexAttributes> attributes;
		std::vector<uint32_t> indices;
		RenderShader::ViewProj viewproj;
		std::vector<int32_t> primitiveIds;
		std::vector<glm::mat4> model_matrices;
		std::vector<RenderShader::PrimitiveInfo> primitiveInfos;
		std::vector<RenderShader::Material> materials;
		std::vector<RenderShader::Texture> textures;
		uint32_t pointLightsCount = 0; //Actual number of data in pointLights (Since std::array doesnt actually track its number of valid elements)
		std::array<RenderShader::PointLight, MAX_POINTLIGHT_COUNT> pointLights;

		//Copy Infos, dictates how the extracted data should be copied into the buffers
		VkBufferCopy indirect_copy_info;
		VkBufferCopy pos_copy_info;
		VkBufferCopy attrib_copy_info;
		VkBufferCopy index_copy_info;
		VkBufferCopy viewprojMatrix_copy_info;
		VkBufferCopy primId_copy_info;
		VkBufferCopy light_copy_info;
		//-Use multiple copy infos in order to place each data using ID-offsets
		std::vector<VkBufferCopy> modelMatrices_copy_infos; 
		std::vector<VkBufferCopy> primInfo_copy_infos;
		std::vector<VkBufferCopy> material_copy_infos;
		std::vector<VkBufferCopy> texture_copy_infos;
	};

	struct Frame {
		VkCommandPool commandPool; //Might move ownership of commandbools and buffers and related resources to vkcontext
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

	//DEBUG - Device Data Updates
	std::unordered_map<DeviceBufferType, int> _deviceBufferTypesCounter; //Used for keeping track of how many buffers need to updated for each type (across the frames). Plan to change since using string as key is pretty bad
	RenderShaderData _stagingUpdateData; //Use to stage render data for updates

	//DEBUG - Primitive Vertex Input Data Tracker. Might delete
	std::unordered_map<uint32_t, VkDrawIndexedIndirectCommand> _primID_to_drawCmd;	//Stores and Maps a Primitive's ID to a DrawCommand, which contains the info pertaining to offset and sizes of its indices and vertex info in the GPU buffers. Aka allows us to keep track of vertex and index info using IDs

	//Depth Image
	AllocatedImage _depthImage;

	//DEBUG - HDR Cubemap
	AllocatedImage _hdrCubeMap;
	AllocatedImage _hdrIrradianceCubeMap;
	AllocatedImage _hdrSpecularCubeMap;
	AllocatedImage _hdrSpecularLUT;
	VkSampler _cubemapSampler; //For both hdr and convoluted cubemap
	VkDescriptorPool _skyboxDescriptorPool;
	VkDescriptorSetLayout _skyboxDescriptorSetLayout;
	VkDescriptorSet _skyboxDescriptorSet;
	VkPipelineLayout _skyboxPipelineLayout;
	VkPipeline _skyboxPipeline;
	AllocatedBuffer _skyboxVertexBuffer;
	AllocatedBuffer _skyboxIndexBuffer;

	void init_swapchain(VkExtent2D windowExtent);
	void init_frames();
	void init_vertexInput();
	void init_descriptorSet();
	void init_graphicsPipeline();
	
	//Draw
	VkResult draw(); //Maybe move draw commands to rendersystem object.
	void draw_geometry(VkCommandBuffer cmd, const Image& swapchainImage);
	void draw_skybox(VkCommandBuffer cmd, const Image& swapchainImage);
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

	//Graphics Payload
	void extract_render_data(const GraphicsDataPayload& payload, DeviceBufferTypeFlags dataType, RenderShaderData& data); //Extracts The specified type of data from payload and output to RenderShaderData param

	//Skybox/Environment Map Functions
	void setup_hdrMap();
};
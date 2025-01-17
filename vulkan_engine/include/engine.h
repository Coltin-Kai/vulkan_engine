#pragma once
#include <vector>

#include "vulkan/vulkan.h"

#define SDL_MAIN_HANDLED //Needed as SDL defines main macro that can conflict with app main
#include "SDL.h"
#include "SDL_vulkan.h"

#include "vk_mem_alloc.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include "glm.hpp"
#include "vec3.hpp"

#include "fastgltf/core.hpp"

#include "vulkan_helper_types.h"
#include "graphic_data_types.h"

#include "Camera.h"

#include <deque>
#include <functional>
#include <filesystem>

//Config
constexpr unsigned int FRAMES_TOTAL = 2;

//-Descriptor Settings
constexpr unsigned int UNIFORM_DESCRIPTOR_COUNT = 500;
constexpr unsigned int COMBINED_IMAGE_SAMPLER_COUNT = 500;

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

	struct Swapchain {
		VkSwapchainKHR vkSwapchain;
		VkFormat format;
		std::vector<VkImage> images;
		std::vector<VkImageView> imageViews;
		VkExtent2D extent;
	};
	
	//Big Structure that enxapsulates all data neccesary for one draw. Vertex and Index, UBO, SBO, etc. 
	struct DrawContext {
		//Draw Resources
		AllocatedBuffer indirectDrawCommandsBuffer; //Global Buffer that holds the draw data for each and every primitive/batched data
		uint32_t drawCount; //How many draws total in the commands buffer
		AllocatedBuffer vertex_buffer; //Global Buffer containing of every vertex for the draw
		AllocatedBuffer index_buffer;

		//Buffer Ressources
		AllocatedBuffer primitiveInfosBuffer;
		VkDeviceAddress primitiveInfosBufferAddress;
		AllocatedBuffer viewprojMatrixBuffer;
		VkDeviceAddress viewprojMatrixBufferAddress;
		AllocatedBuffer modelMatricesBuffer;
		VkDeviceAddress modelMatricesBufferAddress;
		AllocatedBuffer materialsBuffer;
		VkDeviceAddress materialsBufferAddress;
	};

	struct Frame {
		VkCommandPool commandPool;
		VkCommandBuffer commandBuffer;

		VkSemaphore swapchainSemaphore, renderSemaphore;
		VkFence renderFence;

		DrawContext drawContext;

		DeletionQueue deletionQueue; //Currently no resources to delete yet...
	};

	//UBO Structures------------
	struct UniformData { //To delete
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 proj;
	};

	struct MeshData {  //To delete
		glm::mat4 model;
	};

	struct CameraData { //To delete
		glm::mat4 view;
		glm::mat4 proj;
	};
	//---------------------------
	struct Vertex { //To delete
		glm::vec3 pos;
		glm::vec3 color;
	};

	//Interfaces with Graphics Payload to setup data (representing a MesH) used for drawing.
	struct MeshNodeDrawData { //Not really sure this is really neccesary now, as superseded by the global draw context structure in every way. 
		//Draw Data that share the same data: Materal, are batched together for one draw call.
		struct BatchedData {
			AllocatedBuffer pos_buffer; //Need to move to drawcontext as global pos buffer to allow multi drawing
			AllocatedBuffer index_buffer; //Need to move to drawcontext as global index buffer to allow multidrawing
			//and Uniform, Material resources...
			std::shared_ptr<Material> material;

			//Remove this v. Make it global buffer instead that holds multiple VkDrawIndexedCommands (array) to allow multidraw
			AllocatedBuffer indirect_draw_buffer; //Data and Info pertaining to Drawing for these Batched Data. Not Needed
		};

		//Batched Datas that is Batched according ti Topology (for pipeline switching for respective topology)
		std::unordered_map<VkPrimitiveTopology, std::vector<BatchedData>> topologyToBatchedDatas;

		std::shared_ptr<Node> mesh_node; //The associated Node containing the Mesh. If use_count() returns 1, then this MeshNodeDrawData should no longer exist as Node is no longer part of structore

		void cleanup(const VmaAllocator allocator) {
		}
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
	bool windowResized = false;

	//Depth Image
	AllocatedImage _depthImage;

	//Deletion Queue
	DeletionQueue _mainDeletionQueue;

	//VMA Allocator
	VmaAllocator _allocator;

	//Frame Resources
	Frame _frames[FRAMES_TOTAL];
	int _frameNumber = 0;
	Frame& get_current_frame() { return _frames[_frameNumber % FRAMES_TOTAL]; };

	//Immediate Commands Resources
	VkCommandPool _immCommandPool;
	VkCommandBuffer _immCommandBuffer;
	VkFence _immFence;

	//Queues
	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	VkPipelineLayout _pipelineLayout;
	VkPipeline _pipeline;

	//Vertex Input
	std::vector<VkVertexInputBindingDescription> _bindingDescriptions;
	std::vector<VkVertexInputAttributeDescription>_attribueDescriptions;
	std::vector<Vertex> vertices = { //Vertices
		{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
		{{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
		{{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
		{{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}},

		{{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
		{{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
		{{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},
		{{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}}
	};
	std::vector<uint16_t> indices = {
		0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4
	};
	AllocatedBuffer _vertex_data_buffer; //to replace with vector version
	AllocatedBuffer _index_data_buffer; //to replace with vector version

	std::vector<MeshNodeDrawData> _mesh_node_draw_datas; //List of current Meshs to be used in drawing. Move to Draw Context

	//Descriptors
	AllocatedBuffer _uniformData_buffer; //Not Needed
	UniformData _uniform_data; //Not Needed
	VkDescriptorPool _descriptorPool;
	VkDescriptorSetLayout _descriptorSetLayout;
	VkDescriptorSet _descriptorSet;

	//Indrect Resources
	AllocatedBuffer _indirectDrawBuffer; //Not needed

	//Camera
	Camera _camera;

	//Payload
	GraphicsDataPayload _payload;

	void draw();

	void draw_geometry(VkCommandBuffer cmd, uint32_t swapchainImageIndex);

	void draw_imgui(VkCommandBuffer cmd, VkImageView targetImageView);

	void init_vulkan();

	void init_swapchain();

	void init_commands();

	void init_sync_structures();

	void init_graphics_pipeline();

	void init_imgui();

	void setup_depthImage();

	void setup_drawContexts();

	void setup_vertex_input(); //Set up layout of vertex input

	void setup_descriptor_set(); //Set up Descriptor Set and point to the appropriate buffers.

	void resize_swapchain();

	void destroy_swapchain();

public:
	//Helper Functions
	void immediate_command_submit(std::function<void(VkCommandBuffer cmd)>&& function);

	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

	void destroy_buffer(const AllocatedBuffer& buffer);

	AllocatedImage create_image(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);

	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);

	void destroy_image(const AllocatedImage& img);

	//Friends
	friend void loadGLTFFile(GraphicsDataPayload&, Engine&, std::filesystem::path);
	friend std::optional<AllocatedImage> load_image(Engine&, fastgltf::Asset&, fastgltf::Image&);
};
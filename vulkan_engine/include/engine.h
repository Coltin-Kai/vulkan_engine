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
constexpr uint32_t MAX_SAMPLED_IMAGE_COUNT = 100;
constexpr uint32_t MAX_SAMPLER_COUNT = 100;

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
	
	//Big Structure that enxapsulates all data neccesary for one draw. Vertex and Index, UBO, SBO, etc. Maybe Represents the Current Scene
	struct DrawContext {
		//Draw Resources
		AllocatedBuffer indirectDrawCommandsBuffer; //Global Buffer that holds the draw data for each and every primitive/batched data
		uint32_t drawCount; //How many draws total in the commands buffer
		AllocatedBuffer vertexPosBuffer; //Global Buffer containing every vertex's position for the draw
		AllocatedBuffer vertexOtherAttribBuffer; //Global Buffer containing every vertex's other attributes besides position, uvs, vertex_colors.
		AllocatedBuffer indexBuffer;

		//Buffer Ressources
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

		DeletionQueue deletionQueue; //Currently no resources to delete yet...
	};

	//Used by DeviceDataUpdater primarily
	enum class DeviceBufferType {
		IndirectDraw,
		Vertex,
		Attributes,
		Index,
		PrimitiveInfo,
		ViewProjMatrix,
		ModelMatrix,
		Material,
		Texture
	};

	//DEBUG - Tracks what device buffers need and when to be updated (Will figure out a better structure to handle this task)
	struct DeviceDataUpdateTracker { 
	private:
		std::unordered_map<DeviceBufferType, int> deviceBufferTypeToFrameCount; //Tracks the device buffer type and their frameCount (how many times these types of buffers should be updated with respect to frames in flight
	public:
		void add_deviceBufferType(DeviceBufferType bufferType) {
			deviceBufferTypeToFrameCount[bufferType] = 0;
		}

		void signal_to_update(DeviceBufferType bufferType) {
			deviceBufferTypeToFrameCount[bufferType] = FRAMES_TOTAL;
		}

		bool is_updatable(DeviceBufferType bufferType) {
			if (deviceBufferTypeToFrameCount[bufferType] > 0)
				return true;
			else
				return false;
		}

		void acknowledge_update(DeviceBufferType bufferType) { //Decrements the frameCount of the specific buffer type to indicate that update has consumed it
			if (deviceBufferTypeToFrameCount[bufferType] > 0)
				deviceBufferTypeToFrameCount[bufferType]--;
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

	//Command Queues
	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	//Pipeline
	VkPipelineLayout _pipelineLayout;
	VkPipeline _pipeline;

	//Vertex Input
	std::vector<VkVertexInputBindingDescription> _bindingDescriptions;
	std::vector<VkVertexInputAttributeDescription>_attribueDescriptions;

	//Descriptors - Primarily used for SampledImages and Samplers
	VkDescriptorPool _descriptorPool;
	VkDescriptorSetLayout _descriptorSetLayout;
	VkDescriptorSet _descriptorSet;

	//Camera
	Camera _camera;

	//Payload
	GraphicsDataPayload _payload;

	//Data Update Stuff
	DeviceDataUpdateTracker _deviceDataUpdateTracker;

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

	void copy_to_device_buffer(const AllocatedBuffer& dstBuffer, void* data, size_t dataSize, const VkBufferCopy& vkBufferCopy, uint32_t bufferCopiesCount);

	void destroy_image(const AllocatedImage& img);

	//Friends
	friend void loadGLTFFile(GraphicsDataPayload&, Engine&, std::filesystem::path);
	friend std::optional<AllocatedImage> load_image(Engine&, fastgltf::Asset&, fastgltf::Image&);
};
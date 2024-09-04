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

#include <deque>
#include <functional>
#include <filesystem>

//Config
constexpr unsigned int FRAMES_TOTAL = 2;

//May MOve ALlocated Object Structs to own Header
struct AllocatedImage {
	VkImage image;
	VkImageView imageView;
	VmaAllocation allocation;
	VkExtent3D extent;
	VkFormat format;
};

struct AllocatedBuffer {
	VkBuffer buffer;
	VmaAllocation allocation;
	VmaAllocationInfo info;
};

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

	struct Frame {
		VkCommandPool commandPool;
		VkCommandBuffer commandBuffer;

		VkSemaphore swapchainSemaphore, renderSemaphore;
		VkFence renderFence;

		DeletionQueue deletionQueue; //Currently no resources to delete yet...
	};

	struct UnifrormData { 
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 proj;
	};

	struct Vertex {
		glm::vec3 pos;
		glm::vec3 color;
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
	VkVertexInputBindingDescription _bindingDescription;
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
	AllocatedBuffer _vertex_data_buffer;
	AllocatedBuffer _index_data_buffer;

	//Descriptors
	VkDescriptorSetLayout _uniform_descriptor_set_layout;
	AllocatedBuffer _uniformData_buffer;
	VkDescriptorPool _descriptorPool;
	VkDescriptorSet _descriptorSet;

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

	void setup_vertex_input();

	void setup_descriptors();

	void resize_swapchain();

	void destroy_swapchain();

public:
	//Helper Functions
	void immediate_command_submit(std::function<void(VkCommandBuffer cmd)>&& function);

	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);

	void destroy_buffer(const AllocatedBuffer& buffer);

	AllocatedImage create_image(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false);

	void destroy_image(const AllocatedImage& img);

	//Friends
	friend void loadGLTFFile(Engine&, std::filesystem::path);
	friend std::optional<AllocatedImage> load_image(Engine&, fastgltf::Asset&, fastgltf::Image&);
};
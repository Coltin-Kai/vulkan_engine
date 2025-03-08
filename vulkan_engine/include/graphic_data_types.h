/*
	Types related to graphical data that will be managed on the Host/Application. 
*/

#pragma once

#include "glm.hpp"
#include "vulkan/vulkan.h"
#include "vk_mem_alloc.h"

#include "vulkan_helper_types.h"

#include <vector>
#include <string>
#include <memory>

struct Scene;
struct Node;
struct Mesh;
struct Material;
struct Texture;

struct GraphicsDataPayload{
	Scene* current_scene = nullptr; //The scene to load
	std::vector<Scene> scenes{};
	//Default Data resides in index 0 for all these resources.
	//Note order and location should be preserved for images and samplers as maps directly to GPU memory and textures use location index to point to respective one.
	std::vector<VkSampler> samplers{}; //Global Samplers. 
	std::vector<AllocatedImage> images{}; //Global Images accessible by Textures
	std::vector<std::shared_ptr<Texture>> textures{}; //Global Textures
	std::vector<std::shared_ptr<Material>> materials{}; //Global Materials

	//Clean up any vulkan resources that require manual deltion/cleanup
	void cleanup(const VkDevice& device, const VmaAllocator& allocator) {
		for (VkSampler& sampler : samplers) {
			vkDestroySampler(device, sampler, nullptr);
		}
		samplers.clear();

		for (AllocatedImage& image : images) {
			vkDestroyImageView(device, image.imageView, nullptr);
			vmaDestroyImage(allocator, image.image, image.allocation);
		}
		images.clear();

		//STill in Development
	}
};

struct Scene {
	std::string name;
	std::vector<std::shared_ptr<Node>> root_nodes{};
};

/*
	Follows GLTF Node Hierarchy: A root node has no parent. A node hierarchy must not
	contain cycles. And each node must have zero or one parent node.
*/
struct Node {
	std::string name;

	std::weak_ptr<Node> parent_node;
	std::vector<std::shared_ptr<Node>> child_nodes{};

	std::shared_ptr<Mesh> mesh;

	Node() : id(available_id++) {

	}

	uint32_t getID() {
		return id;
	}

	//Updates the Local Transform as well as its World Transform based on parent
	void updateLocalTransform(const glm::mat4& newTransform) {
		local_transform = newTransform;
		auto locked_parent_node = parent_node.lock();
		if (locked_parent_node != nullptr) {
			updateWorldTransform(locked_parent_node->get_WorldTransform());
		}
		else { //Has no parent. SO assume Root Node
			updateWorldTransform(glm::mat4(1.0f));
		}
	}

	const glm::mat4& get_LocalTransform() {
		return local_transform;
	}

	const glm::mat4& get_WorldTransform() {
		return world_transform;
	}
private:
	inline static uint32_t available_id = 0;
	uint32_t id;
	glm::mat4 local_transform; //Local relative to its parent Node
	glm::mat4 world_transform; //Local_Transform * Parent's World Transform. If Root Node, Local_Transform * Identity_Matrix. Should be updated when local_transform us updated.

	void updateWorldTransform(const glm::mat4& parentTransform) {
		world_transform = local_transform * parentTransform;
		for (std::shared_ptr<Node> child : child_nodes) {
			child->updateWorldTransform(world_transform);
		}
	}
};

struct Mesh { 
	struct Primitive {
		struct Vertex { //May move these types of structs outside as nesting structs may be bad practice idk
			glm::vec3 position;
			glm::vec3 normal;
			glm::vec4 tangent;
			//v Have to change how Color_n and Texture_n data is stored to work as passable data.
			std::vector<glm::vec3> colors{}; //Represents Color0, Color1, ...
			std::vector<glm::vec2> uvs{}; //Represents Texcoord0, Texcoord1, ...
		};

		VkPrimitiveTopology topology;
		std::vector<Vertex> vertices{};
		std::vector<uint32_t> indices{};
		std::weak_ptr<Material> material;

		Primitive() : id(available_id++) {

		}

		uint32_t getID() {
			return id;
		}

	private:
		inline static uint32_t available_id = 0;
		uint32_t id;
	};

	std::string name;
	std::vector<Primitive> primitives{};
};

struct Material {
	std::string name;

	Material() : id(available_id++) {

	}

	uint32_t getID() {
		return id;
	}

	std::weak_ptr<Texture> normal_texture;
	int normal_coord_index = -1; //Index of the specific Texture Coord in the Mesh
	float normal_scale = 0.0f;

	std::weak_ptr<Texture> occlusion_texture;
	int occlusion_coord_index = -1;
	float occlusion_strength = 0.0f;

	std::weak_ptr<Texture> emission_texture;
	int emission_coord_index = -1;
	glm::vec3 emission_Factor = glm::vec3(0.0f, 0.0f, 0.0f);

	//PBR Variables
	std::weak_ptr<Texture> baseColor_texture;
	int baseColor_coord_index = -1;
	glm::vec4 baseColor_Factor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

	std::weak_ptr<Texture> metal_rough_texture;
	int metal_rough_coord_index = -1;
	float metallic_Factor = 0.0f;
	float roughness_Factor = 0.0f;

private:
	inline static uint32_t available_id = 0;
	uint32_t id;
};

struct Texture {
	Texture() : id(available_id++) {

	}

	uint32_t getID() {
		return id;
	}

	std::string name;
	int image_index = 0;
	int sampler_index = 0;

private:
	inline static uint32_t available_id = 0;
	uint32_t id;
};
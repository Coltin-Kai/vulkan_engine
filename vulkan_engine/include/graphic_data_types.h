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
	//Default Data resides in index 0 for all these resources
	std::vector<std::unique_ptr<VkSampler>> samplers{}; //Global Samplers. 
	std::vector<std::unique_ptr<AllocatedImage>> images{}; //Global Images accessible by Textures
	std::vector<std::unique_ptr<Texture>> textures{}; //Global Textures
	std::vector<std::unique_ptr<Material>> materials{}; //Global Materials

	//Clean up any vulkan resources that require manual deltion/cleanup
	void cleanup(const VkDevice& device, const VmaAllocator& allocator) {
		for (std::unique_ptr<VkSampler>& sampler : samplers) {
			vkDestroySampler(device, *sampler, nullptr);
		}
		samplers.clear();

		for (std::unique_ptr<AllocatedImage>& image : images) {
			vkDestroyImageView(device, image->imageView, nullptr);
			vmaDestroyImage(allocator, image->image, image->allocation);
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
	glm::mat4 local_transform; //Local to its paren Node
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
		int material_id = 0;
	};

	std::string name;
	std::vector<Primitive> primitives{};
};

struct Material {
	std::string name;

	//std::shared_ptr<Texture> normal_Texture;
	int normal_texture_id = 0;
	int normal_coord_index = -1; //Index of the specific Texture Coord in the Mesh
	float normal_scale = 0.0f;

	//std::shared_ptr<Texture> occlusion_Texture;
	int occlusiong_texture_id = 0;
	int occlusion_coord_index = -1;
	float occlusion_strength = 0.0f;

	//std::shared_ptr<Texture> emission_Texture;
	int emission_texture_id = 0;
	int emission_coord_index = -1;
	glm::vec3 emission_Factor = glm::vec3(0.0f, 0.0f, 0.0f);

	//PBR Variables
	//std::shared_ptr<Texture> baseColor_Texture;
	int baseColor_texture_id = 0;
	int baseColor_coord_index = -1;
	glm::vec4 baseColor_Factor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

	//std::shared_ptr<Texture> metal_rough_Texture;
	int metal_rough_texture_id = 0;
	int metal_rough_coord_index = -1;
	float metallic_Factor = 0.0f;
	float roughness_Factor = 0.0f;
};

struct Texture {
	std::string name;
	//std::shared_ptr<AllocatedImage> image; remove
	int image_index = 0;
	//std::shared_ptr<VkSampler> sampler;
	int sampler_index = 0;
};
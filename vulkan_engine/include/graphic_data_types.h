#pragma once

#include "glm.hpp"
#include "vulkan/vulkan.h"

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
	std::vector<std::shared_ptr<VkSampler>> samplers{};
	std::vector<std::shared_ptr<AllocatedImage>> images{};
	std::vector<std::shared_ptr<Texture>> textures{};
	std::vector<std::shared_ptr<Material>> materials{};
};

struct Scene {
	std::string name;
	std::vector<std::shared_ptr<Node>> root_nodes{};
};

/*
	Follows GLTF Node Hierarchy: A root node has no parent. A node hierarchy must not
	conta ncycles. And each node must have zero or one parent node.
*/
struct Node {
	std::string name;
	std::weak_ptr<Node> parent_node;
	std::vector<std::shared_ptr<Node>> child_nodes{};

	//Updates the Local Transform as well as its World Transform
	void updateLocalTransform(const glm::mat4& newTransform) {
		local_transform = newTransform;
		auto locked_parent_node = parent_node.lock();
		if (locked_parent_node != nullptr) {
			updateWorldTransform(locked_parent_node->get_WorldTransform());
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
	glm::mat4 world_transform; //Parent's World Transform * Local Transform. Should be updated when local_transform us updated

	void updateWorldTransform(const glm::mat4& parentTransform) {
		world_transform = parentTransform * local_transform;
		for (std::shared_ptr<Node> child : child_nodes) {
			child->updateWorldTransform(world_transform);
		}
	}
};

struct MeshNode : Node {
	std::shared_ptr<Mesh> mesh;
};

struct Mesh { 
	struct Primitive {
		struct Vertex {
			glm::vec3 position;
			glm::vec3 normal;
			glm::vec3 tangent;
			std::vector<glm::vec3> colors{};
			std::vector<glm::vec2> uvs{}; //Represents Texcoord0, Texcoord1, ...
		};

		VkPrimitiveTopology topology;
		std::vector<Vertex> vertices{};
		std::vector<uint32_t> indices{};
		std::shared_ptr<Material> material;
	};

	std::string name;
	std::vector<Primitive> primitives{};
};

struct Material {
	std::string name;

	std::shared_ptr<Texture> normal_Texture;
	int normal_coord_index; //Index of the specific Texture Coord in the Mesh
	float normal_scale;

	std::shared_ptr<Texture> occlusion_Texture;
	int occlusion_coord_index;
	float occlusion_strength;

	std::shared_ptr<Texture> emission_Texture;
	int emission_coord_index;
	glm::vec3 emission_Factor;

	//PBR Variables
	std::shared_ptr<Texture> baseColor_Texture;
	int baseColor_coord_index;
	glm::vec4 basrColor_Factor;

	std::shared_ptr<Texture> metal_rough_Texture;
	int metal_rough_coord_index;
	float metallic_Factor;
	float roughness_Factor;
};

struct Texture {
	std::string name;
	std::shared_ptr<AllocatedImage> image;
	std::shared_ptr<VkSampler> sampler;
};
#pragma once

#include "glm.hpp"
#include "vulkan/vulkan.h"
#include "engine.h"

#include <vector>
#include <string>
#include <memory>

struct GraphicsDataPayload{
	std::unique_ptr<Scene> current_scene;
	std::vector<Scene> scenes;
	std::vector<std::shared_ptr<Material>> materials;
	std::vector<VkSampler> samplers;
};

struct Scene {
	std::string name;
	std::vector<std::shared_ptr<Node>> root_nodes;
};

struct Node {
	std::string name;
	std::vector<std::shared_ptr<Node>> child_nodes;
	glm::mat4 local_transform;

	//Camera pointer variable
	std::shared_ptr<Mesh> mesh;
};

struct Mesh {
	struct Primitive {
		struct Vertex {
			glm::vec3 position;
			glm::vec3 normal;
			glm::vec3 tangent;
			std::vector<glm::vec3> colors;
			std::vector<glm::vec2> uvs;
		};

		VkPrimitiveTopology topology;
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		std::shared_ptr<Material> material;
	};

	std::string name;
	std::vector<Primitive> primitives;
};

struct Material {
	std::string name;

	//normalTexture
	//occlusion Texture
	//emissiveTexture
	//emissionFactor

	//PBR Variables
	//base color texture
	//metallic+roughness texture
	float metallic_Factor;
	float roughness_Factor;
};

struct Texture {
	std::shared_ptr<AllocatedImage> image;
	std::shared_ptr<VkSampler> sampler;
};
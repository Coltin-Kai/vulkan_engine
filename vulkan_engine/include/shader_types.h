/*
	Types that reflect the types used by Shaders in order to properly populate
	device buffers.
*/
#pragma once
#include <stdint.h>

constexpr uint32_t MAX_POINTLIGHT_COUNT = 100;

namespace CubeMapShader {
	struct TransformMatrices {
		glm::mat4 view;
		glm::mat4 proj;
	};
}

namespace RenderShader { //Default Shader
	struct VertexAttributes { //Other than Position, Vertex Color, and UV
		glm::vec3 normal;
		glm::vec4 tangent;
		glm::vec3 color;
		glm::vec2 uv;
	};

	struct PrimitiveInfo {
		uint32_t mat_id;
		uint32_t model_matrix_id;
	};

	struct Material {
		uint32_t baseColor_texture_id;
		int32_t baseColor_texCoord_id;
		glm::vec4 baseColor_factor;

		uint32_t normal_texture_id;
		int32_t normal_texcoord_id;
		float normal_scale;

		uint32_t metal_rough_texture_id;
		int32_t metal_rough_texcoord_id;
		float metallic_factor;
		float roughness_factor;

		uint32_t occlusion_texture_id;
		int32_t occlusion_texcoord_id;
		float occlusion_strength;

		uint32_t emission_texture_id;
		int32_t emission_texcoord_id;
		glm::vec3 emission_factor;
	};

	struct Texture {
		int32_t textureImage_id;
		int32_t sampler_id;
	};

	struct ViewProj {
		glm::mat4 view;
		glm::mat4 proj;
		glm::vec3 pos;
	};

	struct PointLight {
		glm::vec3 pos;
		glm::vec3 color;
		float power;
	};

	struct Lights {
		uint32_t pointLightCount;
		PointLight pointLights[MAX_POINTLIGHT_COUNT];
	};

	struct PushConstants {
		VkDeviceAddress primitiveIdsBufferAddress;
		VkDeviceAddress primitiveInfosBufferAddress;
		VkDeviceAddress viewProjMatrixBufferAddress;
		VkDeviceAddress modelMatricesBufferAddress;
		VkDeviceAddress materialsBufferAddress;
		VkDeviceAddress texturesBufferAddress;
		VkDeviceAddress lightsBufferAddress;
	};
}
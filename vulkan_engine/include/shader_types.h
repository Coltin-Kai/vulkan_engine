/*
	Types that reflect the types used by Shaders in order to properly populate
	device buffers.
*/
#pragma once
#include <stdint.h>

namespace RenderShader {
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
	};

	struct Texture {
		int32_t textureImage_id;
		int32_t sampler_id;
	};

	struct ViewProj {
		glm::mat4 view;
		glm::mat4 proj;
	};

	struct PushConstants {
		VkDeviceAddress primitiveInfosBufferAddress;
		VkDeviceAddress viewProjMatrixBufferAddress;
		VkDeviceAddress modelMatricesBufferAddress;
		VkDeviceAddress materialsBufferAddress;
		VkDeviceAddress texturesBufferAddress;
	};
}
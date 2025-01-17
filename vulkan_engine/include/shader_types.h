/*
	Types that reflect the types used by Shaders in order to properly populate
	GPU used buffers.
*/
#pragma once
#include <stdint.h>

namespace RenderShader {
	struct PrimitiveInfo {
		uint32_t mat_id;
		uint32_t model_matrix_id;
	};

	struct Material {
		uint32_t norm_texture_id;
		uint32_t norm_texCoord_id;
		float normal_scale;
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
	};
}
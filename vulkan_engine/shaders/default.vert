#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

const uint MAX_TEXTURE2D_COUNT = 100;
const uint MAX_SAMPLER_COUNT = 100;

struct PrimitiveInfo {
	uint mat_id;
	uint model_matrix_id;
};

struct Material {
	int baseColor_texture_id;
	int baseColor_texcoord_id;
	vec4 baseColor_factor;
};

struct Texture {
	int textureImage_id;
	int sampler_id;
};

layout(scalar, buffer_reference, buffer_reference_align = 4) buffer PrimitiveInfosBuffer { //Unsure what buffer_reference_align should be
	PrimitiveInfo primitiveInfos[]; //Index with gl_DrawID
};

layout(scalar, buffer_reference, buffer_reference_align = 4) buffer ViewProjMatrixBuffer {
	mat4 view;
	mat4 proj;
};

layout(scalar, buffer_reference, buffer_reference_align = 4) buffer ModelMatricesBuffer {
	mat4 model[]; //Index with PrimitiveInfo::model_matrix_id
};

layout(scalar, buffer_reference, buffer_reference_align = 4) buffer MaterialsBuffer {
	Material materials[]; //Index with PrimitiveInfo::mat_id
};

layout(scalar, buffer_reference, buffer_reference_align = 4) buffer TexturesBuffer {
	Texture textures[];
};

layout(push_constant) uniform PushConstants {
	PrimitiveInfosBuffer primInfoBuffer;
	ViewProjMatrixBuffer viewprojBuffer;
	ModelMatricesBuffer modelsBuffer;
	MaterialsBuffer matBuffer;
	TexturesBuffer texBuffer;
};

layout(set = 0, binding = 0) uniform texture2D texture_images[MAX_TEXTURE2D_COUNT]; //Index with Texture::textureImage_id
layout(set = 0, binding = 1) uniform sampler samplers[MAX_SAMPLER_COUNT]; //Index with Texture::sampler_id

//-------------------------------------------------------------------------------------
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 tangent;
layout(location = 3) in vec3 color; //COLOR_0
layout(location = 4) in vec2 uv; //TEXCOORD_0

layout(location = 0) out int outDrawID; //Passing drawID to fragShader
layout(location = 1) out vec3 outColor;
layout(location = 2) out vec2 outUV;

void main() {
	PrimitiveInfo primitive = primInfoBuffer.primitiveInfos[gl_DrawID];
	outDrawID = gl_DrawID;
	outColor = color;
	outUV = uv;
	gl_Position = viewprojBuffer.proj * viewprojBuffer.view * modelsBuffer.model[primitive.model_matrix_id] * vec4(inPosition, 1.0f);
}
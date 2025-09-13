#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_scalar_block_layout : require

const uint MAX_TEXTURE2D_COUNT = 100;
const uint MAX_SAMPLER_COUNT = 100;

const uint MAX_POINTLIGHT_COUNT = 100;

struct PrimitiveInfo {
	uint mat_id;
	uint model_matrix_id;
};

struct Material {
	int baseColor_texture_id;
	int baseColor_texcoord_id;
	vec4 baseColor_factor;

	int normal_texture_id;
	int normal_texcoord_id;
	float normal_scale;

	int metal_rough_texture_id;
	int metal_rough_texcoord_id;
	float metallic_factor;
	float roughness_factor;

	int occlusion_texture_id;
	int occlusion_texcoord_id;
	float occlusion_strength;

	int emission_texture_id;
	int emission_texcoord_id;
	vec3 emission_factor;
};

struct Texture {
	int textureImage_id;
	int sampler_id;
};

struct PointLight {
	vec3 pos;
	vec3 color;
	float power;
};

layout(scalar, buffer_reference, buffer_reference_align = 4) buffer PrimitiveIdsBuffer { 
	int prim_ids[]; //Index with glDrawID
};

layout(scalar, buffer_reference, buffer_reference_align = 4) buffer PrimitiveInfosBuffer { //Unsure what buffer_reference_align should be
	PrimitiveInfo primitiveInfos[]; //Index with prim_id
};

layout(scalar, buffer_reference, buffer_reference_align = 4) buffer ViewProjMatrixBuffer { //Probably rename this to be like camera or something related
	mat4 view;
	mat4 proj;
	vec3 camPos;
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

layout(scalar, buffer_reference, buffer_reference_align = 4) buffer LightsBuffer {
	int pointLightCount;
	PointLight lights[MAX_POINTLIGHT_COUNT];
};

layout(push_constant) uniform PushConstants {
	PrimitiveIdsBuffer primIdBuffer;
	PrimitiveInfosBuffer primInfoBuffer;
	ViewProjMatrixBuffer viewprojBuffer;
	ModelMatricesBuffer modelsBuffer;
	MaterialsBuffer matBuffer;
	TexturesBuffer texBuffer;
	LightsBuffer lightBuffer;
};

layout(set = 0, binding = 0) uniform texture2D texture_images[MAX_TEXTURE2D_COUNT]; //Index with Texture::textureImage_id
layout(set = 0, binding = 1) uniform sampler samplers[MAX_SAMPLER_COUNT]; //Index with Texture::sampler_id
layout(set = 0, binding = 2) uniform samplerCube IBL_irradianceCubemap;
layout(set = 0, binding = 3) uniform samplerCube IBL_specPreFilteredCubemap;
layout(set = 0, binding = 4) uniform sampler2D IBL_specLUT;

//-------------------------------------------------------------------------------------
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 tangent;
layout(location = 3) in vec3 color; //COLOR_0
layout(location = 4) in vec2 uv; //TEXCOORD_0

layout(location = 0) out int outPrimID; //Passing drawID to fragShader
layout(location = 1) out vec3 outColor;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outFragPos;
layout(location = 4) out vec3 outNormal;
layout(location = 5) out mat3 TBN;

void main() {
	int primID = primIdBuffer.prim_ids[gl_DrawID];
	PrimitiveInfo primitive = primInfoBuffer.primitiveInfos[primID];
	mat4 model = modelsBuffer.model[primitive.model_matrix_id];

	outPrimID = primID;
	outColor = color;
	outUV = uv;
	outFragPos = (model * vec4(inPosition, 1.0f)).xyz;
	outNormal = inNormal;

	vec3 T = normalize(vec3(model * vec4(tangent.xyz, 0.0)));
	vec3 N = normalize(vec3(model * vec4(inNormal, 0.0)));
	vec3 B = cross(N, T) * tangent.w; //tangent.w is the bitangent sign
	TBN = mat3(T, B, N);

	gl_Position = viewprojBuffer.proj * viewprojBuffer.view * model * vec4(inPosition, 1.0f);
}
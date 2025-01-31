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
layout(location = 0) flat in int drawID;
layout(location = 1) in vec3 inColor; //Color_0
layout(location = 2) in vec2 inUV; //TexCoord_0

layout(location = 0) out vec4 outFragColor;

void main() {
	PrimitiveInfo primitive = primInfoBuffer.primitiveInfos[drawID];
	Material mat = matBuffer.materials[primitive.mat_id];

	//BaseColor
	Texture baseColor_texture = texBuffer.textures[mat.baseColor_texture_id];
	vec2 baseColor_texcoord;
	if (mat.baseColor_texcoord_id == -1) //If Texcoord doesn't exist, just return vertex color.
		outFragColor = vec4(inColor, 1.0f);
	else {
		if (mat.baseColor_texcoord_id == 0) //If it uses TexCorrd_0, grab from vertex input
			baseColor_texcoord = inUV;
		outFragColor = vec4(texture(sampler2D(texture_images[baseColor_texture.textureImage_id], samplers[baseColor_texture.sampler_id]), baseColor_texcoord).rgb * inColor, 1.0f) * mat.baseColor_factor;
	}
}
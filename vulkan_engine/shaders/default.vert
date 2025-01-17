#version 460
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_nonuniform_qualifier : require

const uint FRAMES_IN_FLIGHT = 2;

layout(location = 0) in vec3 inPosition;

layout (location = 0) out vec3 outColor;

struct PrimitiveInfo {
	uint mat_id;
	uint model_matrix_id;
};

struct Material {
	uint norm_texture_id;
	uint norm_texcoord_id;
	float normal_scale;
};

layout(buffer_reference, std430, buffer_reference_align = 4) buffer PrimitiveInfosBuffer { //Unsure what buffer_reference_align should be
	PrimitiveInfo primitiveInfos[];
};

layout(buffer_reference, std430, buffer_reference_align = 4) buffer ViewProjMatrixBuffer {
	mat4 view;
	mat4 proj;
};

layout(buffer_reference, std430, buffer_reference_align = 4) buffer ModelMatricesBuffer {
	mat4 model[];
};

layout(buffer_reference, std430, buffer_reference_align = 16) buffer MaterialsBuffer {
	Material materials[];
};

layout(push_constant) uniform PushConstants {
	PrimitiveInfosBuffer primInfoBuffer;
	ViewProjMatrixBuffer viewprojBuffer;
	ModelMatricesBuffer modelsBuffer;
	MaterialsBuffer matBuffer;
};

layout(set = 0, binding = 0) uniform sampler2D Textures2D[]; //Variable Descriptor Count

void main() {
	gl_Position = viewprojBuffer.proj * viewprojBuffer.view * modelsBuffer.model[gl_DrawID] * vec4(inPosition, 1.0f);
	outColor = vec3(0.85f, 0.85f, 0.85f);
}
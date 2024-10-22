#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout (location = 0) out vec3 outColor;

layout (set = 0, binding = 0) uniform uniformBuffer {
	mat4 model;
	mat4 view;
	mat4 proj;
} ubo[];

void main() {
	gl_Position = ubo[0].proj * ubo[0].view * ubo[0].model * vec4(inPosition, 1.0f);
	outColor = inColor;
}
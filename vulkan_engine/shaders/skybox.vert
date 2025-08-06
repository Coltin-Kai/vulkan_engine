#version 460

layout(set = 0, binding = 0) uniform TransformMatrices {
	mat4 view;
	mat4 proj;
} matrices;

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 texCoords;

void main() {
	texCoords = inPosition;
	vec4 pos = matrices.proj * matrices.view * vec4(inPosition, 1.0);
	pos.z = 0.0; 
	gl_Position = pos; //Not sure z has to be 0.0 or 1.0 for depth testing to work correctly
}
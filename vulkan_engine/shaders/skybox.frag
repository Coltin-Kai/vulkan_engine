#version 460

layout(set = 0, binding = 1) uniform samplerCube skybox;

layout(location = 0) in vec3 texCoord;

layout(location = 0) out vec4 outFragColor;

void main() {
	vec3 direction = normalize(texCoord);
	//direction.y = -direction.y;
	outFragColor = texture(skybox, direction);
}

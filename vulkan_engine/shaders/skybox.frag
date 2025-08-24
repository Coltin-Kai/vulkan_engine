#version 460

layout(set = 0, binding = 1) uniform samplerCube skybox;

layout(location = 0) in vec3 texCoord;

layout(location = 0) out vec4 outFragColor;

const float gamma = 2.2;

void main() {
	vec3 direction = normalize(texCoord);
	vec3 color = texture(skybox, direction).rgb;

	//Tone Map and Gamma Correct HDR Texture values
	color = color / (color + vec3(1.0));
	color = pow(color, vec3(1.0 / gamma));

	outFragColor = vec4(color, 1.0);
}

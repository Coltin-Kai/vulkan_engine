#version 460

layout(set = 0, binding = 1) uniform sampler2D equirectangularMap;

layout(location = 0) in vec3 localPos;

layout(location = 0) out vec4 outFragColor;

const vec2 invAtan = vec2(0.1591, 0.3183); //The first value represents the reciprocal of 2PI (aka 1/(2PI)). The second is the reciprocal of PI. (1/PI)

vec2 sampleSphericalMap(vec3 v);

void main() {
	vec2 uv = sampleSphericalMap(normalize(localPos));
	vec3 color = texture(equirectangularMap, uv).rgb;
	outFragColor = vec4(color, 1.0);
}

vec2 sampleSphericalMap(vec3 v) { //v represents the unit circle direction angle
	//Transform from Catersian Coords to Polar Angles
	vec2 uv = vec2(atan(v.z, v.x), asin(v.y)); 

	//Maps Polar Angles to [0,1] UV Range: u = (1/2PI) * phi + 0.5 and v = (1/PI) * theta + 0.5
	uv *= invAtan;
	uv += 0.5;

	//Invert UV Coords - Fixes inversion of cubemap faces
	uv.x = 1 - uv.x;
	uv.y = 1 - uv.y;
	return uv;
}
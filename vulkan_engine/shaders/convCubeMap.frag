#version 460

layout(set = 0, binding = 1) uniform samplerCube hdrCubeMap;

layout(location = 0) in vec3 localPos;

layout(location = 0) out vec4 outFragColor;

const float PI = 3.14159265359;

void main() {
	vec3 normal = normalize(localPos); //Normal Sample Direction from Fragment

	//Convolution
	vec3 irradiance = vec3(0.0);

	//-Construct Vectors to transform into a space oriented around the normal
	vec3 up = vec3(0.0, 1.0, 0.0);
	vec3 right = normalize(cross(up, normal));
	up = normalize(cross(normal, right));

	//-Sample a set of directions within the semi-hemisphere of the frag's normal to get a convoluted value
	float sampleDelta = 0.025; //Determines how many samples to do and how accurate
	float nrSamples = 0.0; //Total number of samples done

	//-Iterate through direction angles (radians) of the semi-hemisphere. 
	for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
		for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
			vec3 tangentSample = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta)); //Convert direction angles into a direction vector
			vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal; //Transform the direction vector from tangent space to world space oriented around the normal

			//Add sampled direction vector to toal irradiance
			irradiance += texture(hdrCubeMap, sampleVec).rgb * cos(theta) * sin(theta); //Scale by cos(theta) to account for weaker radiance at larger angles. And sin(theta) to account for smaller sample areas in higher hemisphere areas
			nrSamples++;
		}
	}

	//-Divide by the total number of samples we used.
	irradiance = PI * irradiance * (1.0 / float(nrSamples));

	//outFragColor = vec4(irradiance, 0.0);
	outFragColor = vec4(texture(hdrCubeMap, localPos).rgb, 1.0);
}


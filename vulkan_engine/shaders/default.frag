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

layout(push_constant) uniform PushConstants {
	PrimitiveIdsBuffer primIdBuffer;
	PrimitiveInfosBuffer primInfoBuffer;
	ViewProjMatrixBuffer viewprojBuffer;
	ModelMatricesBuffer modelsBuffer;
	MaterialsBuffer matBuffer;
	TexturesBuffer texBuffer;
};

layout(set = 0, binding = 0) uniform texture2D texture_images[MAX_TEXTURE2D_COUNT]; //Index with Texture::textureImage_id
layout(set = 0, binding = 1) uniform sampler samplers[MAX_SAMPLER_COUNT]; //Index with Texture::sampler_id

//-------------------------------------------------------------------------------------
layout(location = 0) flat in int inPrimID;
layout(location = 1) in vec3 inColor; //Color_0
layout(location = 2) in vec2 inUV; //TexCoord_0
layout(location = 3) in vec3 inFragPos;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in vec4 inTangent;

layout(location = 0) out vec4 outFragColor;

const float PI = 3.14159265359;

struct PointLight {
	vec3 pos;
	vec3 color;
};

float BRDF_NormalDistributionFunction(vec3 normal, vec3 halfwayVector, float roughness);
float BRDF_GeometryAttenuationFunction(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness);
float BRDF_GeometrySchlickGGX(float NdotV, float roughness);
vec3 BRDF_fresnelFunction(float cosTheta, vec3 base_reflectivity);

void main() {
	PrimitiveInfo primitive = primInfoBuffer.primitiveInfos[inPrimID];
	Material mat = matBuffer.materials[primitive.mat_id];

	vec3 baseColor;
	vec3 normal;
	float metallic;
	float roughness;
	float ao;

	//Sample Textures
	//-BaseColor
	Texture baseColor_texture = texBuffer.textures[mat.baseColor_texture_id];
	if (mat.baseColor_texcoord_id == -1) //If Texcoord doesn't exist, just return vertex color.
		baseColor = inColor;
	else if (mat.baseColor_texcoord_id == 0) { //If it uses TexCorrd_0, grab from vertex input
			vec2 baseColor_texcoord = inUV;
			baseColor = texture(sampler2D(texture_images[baseColor_texture.textureImage_id], samplers[baseColor_texture.sampler_id]), baseColor_texcoord).rgb * inColor * mat.baseColor_factor.rgb; //Sampled Texture Value * Associated Factor. Not sure i need to multiple with inColor
	}

	baseColor = pow(baseColor, vec3(2.2)); //Convert Texture Colors to Linear Space

	//-Normal !!!More Code needed to include tangent and stuff
	Texture normal_texture = texBuffer.textures[mat.normal_texture_id];
	if (mat.normal_texcoord_id == -1)
		normal = inNormal;
	else if (mat.normal_texcoord_id == 0) {
		vec2 normal_texcoord = inUV;
		normal = texture(sampler2D(texture_images[normal_texture.textureImage_id], samplers[normal_texture.sampler_id]), normal_texcoord).rgb * mat.normal_scale; //NOt sure normal_scale applies before or after
		vec3 bitangent = inTangent.w * cross(inNormal, inTangent.xyz);
		normal = normalize(normal.x * inTangent.xyz + normal.y * bitangent + normal.z * inNormal); //Transform the Sampled Normal Vector from Tangent Space to World Space
	}

	//-Metal_Roughness 
	Texture metal_roughness_texture = texBuffer.textures[mat.metal_rough_texture_id];
	if (mat.metal_rough_texcoord_id == -1) {
		metallic = 0.0;
		roughness = 0.5;
	}
	else if (mat.metal_rough_texcoord_id == 0) {
		vec2 metal_rough_texcoord = inUV;
		metallic = texture(sampler2D(texture_images[metal_roughness_texture.textureImage_id], samplers[metal_roughness_texture.sampler_id]), metal_rough_texcoord).b * mat.metallic_factor;
		roughness = texture(sampler2D(texture_images[metal_roughness_texture.textureImage_id], samplers[metal_roughness_texture.sampler_id]), metal_rough_texcoord).g * mat.roughness_factor;
	}

	//Occlusion
	Texture occlusion_texture = texBuffer.textures[mat.occlusion_texture_id];
	if (mat.occlusion_texcoord_id == -1) {
		ao = 0.5;
	}
	else if (mat.occlusion_texcoord_id == 0) {
		vec2 occlusion_texcoord = inUV;
		ao = texture(sampler2D(texture_images[occlusion_texture.textureImage_id], samplers[occlusion_texture.sampler_id]), occlusion_texcoord).r * mat.occlusion_strength;
	}

	//Direct Lighting Calculations
	PointLight lights[2]; //Hard-Coded Pointlights
	lights[0].pos = vec3(0.5f, 0.5f, 0.5f);
	lights[0].color = vec3(1.0f, 1.0f, 1.0f);
	lights[1].pos = vec3(-0.5f, 0.5f, 0.5f);
	lights[1].color = vec3(1.0f, 0.0f, 0.0f);

	normal = normalize(normal);
	vec3 viewDir = normalize(viewprojBuffer.camPos - inFragPos); //!!! Have to pass camera Position as uniform

	vec3 base_reflectivity = vec3(0.04);
	base_reflectivity = mix(base_reflectivity, baseColor, metallic);

	vec3 irradiance = vec3(0.0f);
	for (int i = 0; i < 2; i++) { //Calculate irradiance of Point Lights
		vec3 lightDir = normalize(lights[i].pos - inFragPos);
		vec3 halfwayVector = normalize(viewDir + lightDir);
		float lightDistance = length(lights[i].pos - inFragPos);
		float attenuation = 1.0 / (lightDistance * lightDistance);
		vec3 radiance = lights[i].color * attenuation; //Light's Radiance

		//Cook-Torrance BRDF
		float NDF = BRDF_NormalDistributionFunction(normal, halfwayVector, roughness);
		float G = BRDF_GeometryAttenuationFunction(normal, viewDir, lightDir, roughness);
		vec3 F = BRDF_fresnelFunction(max(dot(halfwayVector, viewDir), 0.0), base_reflectivity);

		vec3 kS = F; //Ratio of reflected light
		vec3 kD = vec3(1.0) - kS; //Ratio of refracted light
		kD *= 1.0 - metallic; 

		vec3 numerator = NDF * G * F;
		float denominator = 4.0 * max(dot(normal, viewDir), 0.0) * max(dot(normal, lightDir), 0.0) + 0.0001;
		vec3 specular = numerator / denominator;

		float NdotL = max(dot(normal, lightDir), 0.0);
		irradiance += (kD * baseColor / PI + specular) * radiance * NdotL;
	}
	
	//Final Color Adjustments
	vec3 ambientFactor = vec3(0.03) * ao; //Ambient Lighting
	vec3 finalColor = (ambientFactor * baseColor) + irradiance; 
	finalColor = finalColor / (finalColor + vec3(1.0)); //Reinhard Tone Mapping
	finalColor = pow(finalColor, vec3(1.0/2.2)); //Gamma Correction

	outFragColor = vec4(finalColor, 1.0);
}

//Returns value of how much of the surface's microfacets diverge from the alignment with the halfway-vector
float BRDF_NormalDistributionFunction(vec3 normal, vec3 halfwayVector, float roughness) {
	//Trowbridge-Reitz GGX
	float roughness2 = roughness * roughness;
	float NdotH = max(dot(normal, halfwayVector), 0.0);
	float NdotH2 = NdotH * NdotH;

	float denom = (NdotH2 * (roughness2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return roughness2 / denom;
}

//Returns value of how much the microfacets shadow each other.
float BRDF_GeometryAttenuationFunction(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness) {
	//Smith's Shadowing Function
	float NdotV = max(dot(normal, viewDir), 0.0);
	float NdotL = max(dot(normal, lightDir), 0.0);
	float ggx1 = BRDF_GeometrySchlickGGX(NdotV, roughness); //Geometry Obstruction from View Direction
	float ggx2 = BRDF_GeometrySchlickGGX(NdotL, roughness); //Geometry Shadowing from Light Direction

	return ggx1 * ggx2;
}

//Returns value of geometry of obstruction involving a direction vector and surface roughness
float BRDF_GeometrySchlickGGX(float NdotV, float roughness) {
//Schlick-GGX (GGX and Schlick-Beckmann approximation)
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;

	float denom = NdotV * (1.0 - k) + k;

	return NdotV / denom;
}

//Returns the value (as a vector) of how much the surface relfects based on viewing angle.
vec3 BRDF_fresnelFunction(float cosTheta, vec3 base_reflectivity) {
	return base_reflectivity + (1.0 - base_reflectivity) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
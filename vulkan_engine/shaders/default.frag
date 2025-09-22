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
layout(location = 0) flat in int inPrimID;
layout(location = 1) in vec3 inColor; //Color_0
layout(location = 2) in vec2 inUV; //TexCoord_0
layout(location = 3) in vec3 inFragPos;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in mat3 TBN;

layout(location = 0) out vec4 outFragColor;

const float PI = 3.14159265359;
const bool FLIP_ENVIRON_MAP_Y = false; //Used for IBL Enviroment Cubemaps to flip certain vector-y components in case of upside down cubemap sampling. Dont think I need this for now but keeping here just in case an Enviromap caues me trouble

float BRDF_NormalDistributionFunction(vec3 normal, vec3 halfwayVector, float roughness);
float BRDF_GeometryAttenuationFunction(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness);
float BRDF_GeometrySchlickGGX(float NdotV, float roughness);
vec3 BRDF_fresnelFunction(float cosTheta, vec3 base_reflectivity);
vec3 BRDF_fresnelFunction_roughness(float cosTheta, vec3 base_reflectivity, float roughness);

void main() {
	PrimitiveInfo primitive = primInfoBuffer.primitiveInfos[inPrimID];
	Material mat = matBuffer.materials[primitive.mat_id];

	vec3 baseColor;
	vec3 normal;
	float metallic;
	float roughness;
	float ao;
	vec3 emission;

	//Sample Textures
	//-BaseColor
	Texture baseColor_texture = texBuffer.textures[mat.baseColor_texture_id];
	if (mat.baseColor_texcoord_id == -1) //If Texcoord doesn't exist, just return vertex color.
		baseColor = mat.baseColor_factor.rgb;
	else if (mat.baseColor_texcoord_id == 0) { //If it uses TexCorrd_0, grab from vertex input
			vec2 baseColor_texcoord = inUV;
			baseColor = texture(sampler2D(texture_images[baseColor_texture.textureImage_id], samplers[baseColor_texture.sampler_id]), baseColor_texcoord).rgb * mat.baseColor_factor.rgb; //Sampled Texture Value * Associated Factor. 
	}

	baseColor = pow(baseColor, vec3(2.2)); //Convert Texture Colors to Linear Space

	//-Normal
	Texture normal_texture = texBuffer.textures[mat.normal_texture_id];
	if (mat.normal_texcoord_id == -1)
		normal = inNormal;
	else if (mat.normal_texcoord_id == 0) {
		vec2 normal_texcoord = inUV;
		normal = texture(sampler2D(texture_images[normal_texture.textureImage_id], samplers[normal_texture.sampler_id]), normal_texcoord).rgb * mat.normal_scale; //NOt sure normal_scale applies before or after
		normal = normal * 2.0 - 1.0; //Transform normal from [0,1] range to [-1,1] range.
		normal = normalize(TBN * normal); //Transform the Normal Vector from Tangent Space to World Space
	}

	//-Metal_Roughness 
	Texture metal_roughness_texture = texBuffer.textures[mat.metal_rough_texture_id];
	if (mat.metal_rough_texcoord_id == -1) {
		metallic = mat.metallic_factor;
		roughness = mat.roughness_factor;
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

	//Emission
	Texture emission_texture = texBuffer.textures[mat.emission_texture_id];
	if (mat.emission_texcoord_id == -1) {
		emission = vec3(0.0, 0.0, 0.0);
	}
	else if (mat.emission_texcoord_id == 0) {
		vec2 emission_texcoord = inUV;
		emission = texture(sampler2D(texture_images[emission_texture.textureImage_id], samplers[emission_texture.sampler_id]), emission_texcoord).rgb * mat.emission_factor;
	}

	//Direct Lighting Calculations
	normal = normalize(normal);
	vec3 viewDir = normalize(viewprojBuffer.camPos - inFragPos);
	vec3 reflectVec = reflect(-viewDir, normal);

	vec3 base_reflectivity = vec3(0.04);
	base_reflectivity = mix(base_reflectivity, baseColor, metallic);

	vec3 irradiance = vec3(0.0f);
	for (int i = 0; i < lightBuffer.pointLightCount; i++) { //Calculate irradiance of Point Lights !!!Make sure to change for dynamic size array lengths of lights!!!
		vec3 lightDir = normalize(lightBuffer.lights[i].pos - inFragPos);
		vec3 halfwayVector = normalize(viewDir + lightDir);
		float lightDistance = length(lightBuffer.lights[i].pos - inFragPos);
		float attenuation = 1.0 / (lightDistance * lightDistance);
		vec3 radiance = lightBuffer.lights[i].color * lightBuffer.lights[i].power * attenuation; //Light's Radiance

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
	
	//IBL Ambient Lighting/Irradiance
	vec3 F = BRDF_fresnelFunction_roughness(max(dot(normal,viewDir), 0.0), base_reflectivity, roughness);
	vec3 kS = F;
	vec3 kD = 1.0 - kS;
	kD *= 1.0 - metallic;

	vec3 enviro_normal = normal; //Used to sample Enviroment Irradiance Cubemap
	if (FLIP_ENVIRON_MAP_Y) 
		enviro_normal.y = -enviro_normal.y;
	vec3 enviro_irradiance = texture(IBL_irradianceCubemap, enviro_normal).rgb;

	vec3 diffuse = enviro_irradiance * baseColor;

	//IBL Specular Lighting
	const float MAX_REFLECTION_LOD = 5.0;

	vec3 enviro_reflect = reflectVec;
	if (FLIP_ENVIRON_MAP_Y)
		enviro_reflect.y = -enviro_reflect.y;
	vec3 prefilteredColor = textureLod(IBL_specPreFilteredCubemap, enviro_reflect, roughness * MAX_REFLECTION_LOD).rgb;

	vec2 brdf = texture(IBL_specLUT, vec2(max(dot(normal, viewDir), 0.0), roughness)).rg;
	vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

	vec3 ambient = (kD * diffuse + specular) * ao; //Ambient Lighting

	//Final Color Adjustments
	vec3 finalColor = ambient + irradiance; 
	finalColor = finalColor / (finalColor + vec3(1.0)); //Reinhard Tone Mapping
	finalColor = pow(finalColor, vec3(1.0/2.2)); //Gamma Correction
	finalColor += emission; //Add Emission (temp)
	outFragColor = vec4(finalColor, 1.0);
}

//Returns value of how much of the surface's microfacets diverge from the alignment with the halfway-vector
float BRDF_NormalDistributionFunction(vec3 normal, vec3 halfwayVector, float roughness) {
	//Trowbridge-Reitz GGX
	float a = roughness * roughness; //Squaring roughness produces more correct results apparently
	float a2 = a * a;
	float NdotH = max(dot(normal, halfwayVector), 0.0);
	float NdotH2 = NdotH * NdotH;

	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return a2 / denom;
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

vec3 BRDF_fresnelFunction_roughness(float cosTheta, vec3 base_reflectivity, float roughness) {
	return base_reflectivity + (max(vec3(1.0 - roughness), base_reflectivity) - base_reflectivity) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
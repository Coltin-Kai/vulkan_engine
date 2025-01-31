#include "loader.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <glm.hpp>
#include <fastgltf/glm_element_traits.hpp> //Neccesarry to allow glm types to be used in fastgltf templates
#include <stb_image.h>
#include <iostream>
#include <stack>

#include "engine.h" //Potential Circular Dependency Risk

void loadGLTFFile(GraphicsDataPayload& dataPayload, Engine& engine, std::filesystem::path filePath) { //WIP, want to make it so that it properly add to existing data payload to allow loading multiple scenes.
	//Parser and GLTF LOading Code
	fastgltf::Parser parser;

	auto data = fastgltf::GltfDataBuffer::FromPath(filePath);
	if (data.error() != fastgltf::Error::None)
		throw std::runtime_error("Failed to load GLTFDataBuffer");

	auto expected_asset = parser.loadGltf(data.get(), filePath.parent_path(), fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages);
	if (expected_asset.error() != fastgltf::Error::None) {
		throw std::runtime_error("Failed to load GLTF file");
	}

	fastgltf::Asset asset = std::move(expected_asset.get());

	//Index/ID offsets. To offset id for already existing data in the payload's data vectors (Default data and existing data)
	size_t textureImage_index_offset = dataPayload.images.size();
	size_t samplers_index_offset = dataPayload.samplers.size();
	size_t texture_index_offset = dataPayload.textures.size();
	size_t material_index_offset = dataPayload.materials.size();

	//Load Texture Samplers
	std::vector<std::unique_ptr<VkSampler>> temp_samplers; //Temp vectors used to correctly point other elements members according to index from GLTF file.
	temp_samplers.reserve(asset.samplers.size());

	for (fastgltf::Sampler& sampler : asset.samplers) {
		temp_samplers.push_back(std::make_unique<VkSampler>());

		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.pNext = nullptr;
		samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
		samplerInfo.minLod = 0;
		samplerInfo.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
		samplerInfo.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));
		samplerInfo.mipmapMode = extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));
		
		vkCreateSampler(engine._device, &samplerInfo, nullptr, temp_samplers[temp_samplers.size() - 1].get());
	}

	dataPayload.samplers.insert(dataPayload.samplers.end(), std::make_move_iterator(temp_samplers.begin()), std::make_move_iterator(temp_samplers.end())); //Add Sampelrs to Payload

	//Load Images
	std::vector<std::unique_ptr<AllocatedImage>> temp_images;
	temp_images.reserve(asset.images.size());

	for (fastgltf::Image& image : asset.images) {
		std::optional<AllocatedImage> img = load_image(engine, asset, image);

		if (img.has_value()) {
			temp_images.push_back(std::make_unique<AllocatedImage>(img.value()));
		}
		else {
			//Failed to Load Image, Store Error
			std::cout << "GLTF Failed to Load Texture: " << image.name << std::endl;
		}
	}

	dataPayload.images.insert(dataPayload.images.end(), std::make_move_iterator(temp_images.begin()), std::make_move_iterator(temp_images.end())); //Add Images to Payload

	//Load Textures
	std::vector<std::unique_ptr<Texture>> temp_textures;
	temp_textures.reserve(asset.textures.size());

	for (fastgltf::Texture& texture : asset.textures) {
		temp_textures.push_back(std::make_unique<Texture>());
		int i = temp_textures.size() - 1;

		temp_textures[i]->name = texture.name;
		if (texture.imageIndex.has_value())
			temp_textures[i]->image_index = texture.imageIndex.value() + textureImage_index_offset; 
		if (texture.samplerIndex.has_value())
			temp_textures[i]->sampler_index = texture.samplerIndex.value() + samplers_index_offset;
	}

	dataPayload.textures.insert(dataPayload.textures.end(), std::make_move_iterator(temp_textures.begin()), std::make_move_iterator(temp_textures.end())); //Add Textures to Payload

	//Load Materials
	std::vector<std::unique_ptr<Material>> temp_materials;
	temp_materials.reserve(asset.materials.size());

	for (fastgltf::Material& mat : asset.materials) {
		temp_materials.push_back(std::make_unique<Material>());
		int i = temp_materials.size() - 1;

		temp_materials[i]->name = mat.name;
		
		if (mat.normalTexture.has_value()) {
			temp_materials[i]->normal_texture_id = mat.normalTexture.value().textureIndex + texture_index_offset;
			temp_materials[i]->normal_coord_index = mat.normalTexture.value().texCoordIndex;
			temp_materials[i]->normal_scale = mat.normalTexture.value().scale;
		}

		if (mat.occlusionTexture.has_value()) {
			temp_materials[i]->occlusiong_texture_id = mat.occlusionTexture.value().textureIndex + texture_index_offset;
			temp_materials[i]->occlusion_coord_index = mat.occlusionTexture.value().texCoordIndex;
			temp_materials[i]->occlusion_strength = mat.occlusionTexture.value().strength;
		}

		if (mat.emissiveTexture.has_value()) {
			temp_materials[i]->emission_texture_id = mat.emissiveTexture.value().textureIndex + texture_index_offset;
			temp_materials[i]->emission_coord_index = mat.emissiveTexture.value().texCoordIndex;
		}
		temp_materials[i]->emission_Factor.x = mat.emissiveFactor.x();
		temp_materials[i]->emission_Factor.y = mat.emissiveFactor.y();
		temp_materials[i]->emission_Factor.z = mat.emissiveFactor.z();

		if (mat.pbrData.baseColorTexture.has_value()) {
			temp_materials[i]->baseColor_texture_id = mat.pbrData.baseColorTexture.value().textureIndex + texture_index_offset;
			temp_materials[i]->baseColor_coord_index = mat.pbrData.baseColorTexture.value().texCoordIndex;
		}
		temp_materials[i]->baseColor_Factor.x = mat.pbrData.baseColorFactor.x();
		temp_materials[i]->baseColor_Factor.y = mat.pbrData.baseColorFactor.y();
		temp_materials[i]->baseColor_Factor.z = mat.pbrData.baseColorFactor.z();
		temp_materials[i]->baseColor_Factor.w = mat.pbrData.baseColorFactor.w();

		if (mat.pbrData.metallicRoughnessTexture.has_value()) {
			temp_materials[i]->metal_rough_texture_id = mat.pbrData.metallicRoughnessTexture.value().textureIndex + texture_index_offset;
			temp_materials[i]->metal_rough_coord_index = mat.pbrData.metallicRoughnessTexture.value().texCoordIndex;
		}
		temp_materials[i]->metallic_Factor = mat.pbrData.metallicFactor;
		temp_materials[i]->roughness_Factor = mat.pbrData.roughnessFactor;
	}

	dataPayload.materials.insert(dataPayload.materials.end(), std::make_move_iterator(temp_materials.begin()), std::make_move_iterator(temp_materials.end()));

	//Load Mesh Data
	std::vector<std::shared_ptr<Mesh>> temp_meshes;
	temp_meshes.reserve(asset.meshes.size());

	for (fastgltf::Mesh& mesh : asset.meshes) {
		temp_meshes.push_back(std::make_shared<Mesh>());
		int i = temp_meshes.size() - 1;

		temp_meshes[i]->name = mesh.name;

		temp_meshes[i]->primitives.reserve(mesh.primitives.size());
		for (auto&& p : mesh.primitives) {
			temp_meshes[i]->primitives.emplace_back();
			Mesh::Primitive& current_primitive = temp_meshes[i]->primitives[temp_meshes[i]->primitives.size() - 1];

			//Topology
			current_primitive.topology = extract_topology_type(p.type);

			//Material
			if (p.materialIndex.has_value())
				current_primitive.material_id = p.materialIndex.value() + material_index_offset;

			//Indices
			if (p.indicesAccessor.has_value()) {
				fastgltf::Accessor& index_accessor = asset.accessors[p.indicesAccessor.value()];
				current_primitive.indices.reserve(index_accessor.count);

				fastgltf::iterateAccessor<std::uint32_t>(asset, index_accessor,
					[&](std::uint32_t idx) {
						current_primitive.indices.push_back(idx);
					});
			}

			//Vertex Positions
			auto position_attrib = p.findAttribute("POSITION");
			if (position_attrib != p.attributes.end()) {
				fastgltf::Accessor& pos_accessor = asset.accessors[position_attrib->accessorIndex];
				
				current_primitive.vertices.reserve(pos_accessor.count);
				fastgltf::iterateAccessor<glm::vec3>(asset, pos_accessor,
					[&](glm::vec3 pos) {
						current_primitive.vertices.emplace_back();
						current_primitive.vertices[current_primitive.vertices.size() - 1].position = pos;
					});
			}

			//Vertex Normals
			auto normals_attrib = p.findAttribute("NORMAL");
			if (normals_attrib != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, asset.accessors[normals_attrib->accessorIndex],
					[&](glm::vec3 norm, std::size_t idx) {
						current_primitive.vertices[idx].normal = norm;
					});
			}

			//Vertex Tangents
			auto tangents_attrib = p.findAttribute("TANGENT");
			if (tangents_attrib != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, asset.accessors[tangents_attrib->accessorIndex],
					[&](glm::vec4 tan, std::size_t idx) {
						current_primitive.vertices[idx].tangent = tan;
					});
			}

			//UV Coords
			int i = 0;
			while(true) {
				std::string attribName = "TEXCOORD_" + std::to_string(i);
				auto uv_attrib = p.findAttribute(attribName);
				if (uv_attrib != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec2>(asset, asset.accessors[uv_attrib->accessorIndex],
						[&](glm::vec2 uv, std::size_t idx) {
							current_primitive.vertices[idx].uvs.push_back(uv);
						});
				}
				else
					break;

				i++;
			}

			//Vertex Colors
			i = 0;
			while (true) {
				std::string attribName = "COLOR_" + std::to_string(i);
				auto color_attrib = p.findAttribute(attribName);
				if (color_attrib != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, asset.accessors[color_attrib->accessorIndex],
						[&](glm::vec3 color, size_t idx) {
							current_primitive.vertices[idx].colors.push_back(color);
						});
				}
				else
					break;

				i++;
			}
		}
	}

	//Load Scene
	size_t scenes_offset = dataPayload.scenes.size(); //Used as offset to scenes currently being added
	for (int i = 0; i < asset.scenes.size(); i++) {
		dataPayload.scenes.emplace_back();
		Scene& scene = dataPayload.scenes[scenes_offset + i];
		
		if (asset.defaultScene.has_value() && i == asset.defaultScene.value()) { //Set Current/Default Scene
			dataPayload.current_scene = &scene;
		}

		scene.name = asset.scenes[i].name;

		//Load Nodes. Perform DFS to get all the nodes in the scene
		std::stack<size_t> DFS_node_index_stack; //Maps the correct nodes to traverse for DFS
		std::stack<std::pair<std::shared_ptr<Node>, int>> DFS_node_parent_stack; //Tracks the correct parent node of the current node when traversing. Also keeps track of how many times the parent node will be used before it can be popped
		
		for (size_t root_node_index : asset.scenes[i].nodeIndices) { //Iterate through root nodes of the current scene to add to the DFS stack
			DFS_node_index_stack.push(root_node_index);
		}

		while (!DFS_node_index_stack.empty()) {
			size_t node_index = DFS_node_index_stack.top();
			DFS_node_index_stack.pop();

			//Consruct Node and Fill in member data

			std::shared_ptr<Node> node = std::make_shared<Node>();

			node->name = asset.nodes[node_index].name;

			if (asset.nodes[node_index].meshIndex.has_value()) //If Node holds a Mesh, make it a MeshNode
				node->mesh = temp_meshes[asset.nodes[node_index].meshIndex.value()];

			//Check if node has parent and if so establish relationship by pointers
			if (!DFS_node_parent_stack.empty()) { 
				DFS_node_parent_stack.top().first->child_nodes.push_back(node); 
				node->parent_node = DFS_node_parent_stack.top().first;

				DFS_node_parent_stack.top().second--;
				if (DFS_node_parent_stack.top().second == 0) { //If count reaches 0, then parent has no more children to account for and can be popped off stack
					DFS_node_parent_stack.pop();
				}
			}
			else { //Indicates node is rootnode and should be added to scene's rootnodes list.
				scene.root_nodes.push_back(node);
			}

			//Add Local Transform. Do it before adding children to prevent function from recursing
			node->updateLocalTransform(translate_to_glm_mat4(fastgltf::getTransformMatrix(asset.nodes[node_index])));

			//Check if node has children, thus adding children to DFS idnex stack and the node itself to parent stack
			if (!asset.nodes[node_index].children.empty()) {
				DFS_node_parent_stack.push(std::pair<std::shared_ptr<Node>, int>(node, asset.nodes[node_index].children.size()));
				for (size_t node_children_index : asset.nodes[node_index].children) {
					DFS_node_index_stack.push(node_children_index);
				}
			}
		}
	}
}

VkFilter extract_filter(fastgltf::Filter filter) {
	switch (filter) {
		case fastgltf::Filter::Nearest:
		case fastgltf::Filter::NearestMipMapNearest:
		case fastgltf::Filter::NearestMipMapLinear:
			return VK_FILTER_NEAREST;

		case fastgltf::Filter::Linear:
		case fastgltf::Filter::LinearMipMapNearest:
		case fastgltf::Filter::LinearMipMapLinear:
		default:
			return VK_FILTER_LINEAR;
	}
}

VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter) {
	switch (filter) {
		case fastgltf::Filter::NearestMipMapNearest:
		case fastgltf::Filter::LinearMipMapNearest:
			return VK_SAMPLER_MIPMAP_MODE_NEAREST;

		case fastgltf::Filter::NearestMipMapLinear:
		case fastgltf::Filter::LinearMipMapLinear:
		default:
			return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}

VkPrimitiveTopology extract_topology_type(fastgltf::PrimitiveType type) {
	switch (type)
	{
	case fastgltf::PrimitiveType::Points:
		return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	case fastgltf::PrimitiveType::Lines:
		return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	case fastgltf::PrimitiveType::LineLoop:
	case fastgltf::PrimitiveType::LineStrip:
		return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
	case fastgltf::PrimitiveType::Triangles:
		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	case fastgltf::PrimitiveType::TriangleStrip:
		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	case fastgltf::PrimitiveType::TriangleFan:
		return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
	}
}

glm::mat4 translate_to_glm_mat4(fastgltf::math::fmat4x4 gltf_mat4) {
	glm::mat4 result_transform;
	memcpy(&result_transform, gltf_mat4.data(), sizeof(gltf_mat4));
	return result_transform;
}

std::optional<AllocatedImage> load_image(Engine& engine, fastgltf::Asset& asset, fastgltf::Image& image) {
	AllocatedImage newImage{};
	int width, height, nrChannels;
	
	std::visit(fastgltf::visitor{
		[](auto& arg) {
			std::cout << "A load_images function encountered an unaccounted for DataSource type from its std::visit function." << std::endl;
		},
		[&](fastgltf::sources::URI& filePath) {
			assert(filePath.fileByteOffset == 0);
			assert(filePath.uri.isLocalPath());

			const std::string path(filePath.uri.path().begin(), filePath.uri.path().end());
			unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);

			if (data) {
				VkExtent3D imageSize;
				imageSize.width = width;
				imageSize.height = height;
				imageSize.depth = 1;

				newImage = engine.create_image(data, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);
				
				stbi_image_free(data);
			}
		},
		[&](fastgltf::sources::Array& array) {
			unsigned char* data = stbi_load_from_memory(reinterpret_cast<unsigned char*>(&(array.bytes[0])), static_cast<int>(array.bytes.size()), &width, &height, &nrChannels, 4);

			if (data) {
				VkExtent3D imageSize;
				imageSize.width = width;
				imageSize.height = height;
				imageSize.depth = 1;

				newImage = engine.create_image(data, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);

				stbi_image_free(data);
			}
		},
		[&](fastgltf::sources::Vector& vector) {
			unsigned char* data = stbi_load_from_memory(reinterpret_cast<unsigned char*>(&(vector.bytes[0])), static_cast<int>(vector.bytes.size()), &width, &height, &nrChannels, 4); //Potential Undefined Behavior points due to casting

			if (data) {
				VkExtent3D imageSize;
				imageSize.width = width;
				imageSize.height = height;
				imageSize.depth = 1;

				newImage = engine.create_image(data, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);
				
				stbi_image_free(data);
			}
		},
		[&](fastgltf::sources::BufferView& view) {
			fastgltf::BufferView& bufferView = asset.bufferViews[view.bufferViewIndex];
			fastgltf::Buffer& buffer = asset.buffers[bufferView.bufferIndex];

			std::visit(fastgltf::visitor{
				[](auto& arg) {},
				[&](fastgltf::sources::Vector& vector) {
					unsigned char* data = stbi_load_from_memory(reinterpret_cast<unsigned char*>(&(vector.bytes[0])) + bufferView.byteOffset, static_cast<int>(bufferView.byteLength), &width, &height, &nrChannels, 4); //Potential Undefined Behavior points due to casting

					if (data) {
						VkExtent3D imageSize;
						imageSize.width = width;
						imageSize.height = height;
						imageSize.depth = 1;

						newImage = engine.create_image(data, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);
						
						stbi_image_free(data);
					}
				}
				}, buffer.data);
		}
		}, image.data);

	if (newImage.image == VK_NULL_HANDLE)
		return {};
	else
		return newImage;
}
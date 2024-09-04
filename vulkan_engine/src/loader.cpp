#include "loader.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <glm.hpp>
#include <fastgltf/glm_element_traits.hpp> //Neccesarry to allow glm types to be used in fastgltf templates
#include <stb_image.h>
#include <iostream>

void loadGLTFFile(Engine& engine, std::filesystem::path filePath) {
	//Parser and GLTF LOading Code
	fastgltf::Parser parser;

	fastgltf::GltfDataBuffer data;
	data.FromPath(filePath);

	auto expected_asset = parser.loadGltf(data, filePath.parent_path());
	if (expected_asset.error() != fastgltf::Error::None) {
		throw std::runtime_error("Failed to load GLTF file");
	}

	fastgltf::Asset asset = std::move(expected_asset.get());

	//Load Texture Samplers
	for (fastgltf::Sampler& sampler : asset.samplers) {
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.pNext = nullptr;
		samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
		samplerInfo.minLod = 0;
		samplerInfo.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
		samplerInfo.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));
		samplerInfo.mipmapMode = extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));
		VkSampler newSampler;
		vkCreateSampler(engine._device, &samplerInfo, nullptr, &newSampler);
	}

	//Load Textures
	for (fastgltf::Image& image : asset.images) {
		std::optional<AllocatedImage> img = load_image(engine, asset, image);

		if (img.has_value()) {
			//Store Image
		}
		else {
			//Failed to Load Image, Store Error
			std::cout << "GLTF Failed to Load Texture: " << image.name << std::endl;
		}
	}

	//Load Materials
	for (fastgltf::Material& mat : asset.materials) {
		mat.pbrData.baseColorFactor;
		mat.pbrData.baseColorTexture;
		mat.pbrData.metallicFactor;
		mat.pbrData.roughnessFactor;
		mat.pbrData.metallicRoughnessTexture;

		mat.normalTexture;
		mat.occlusionTexture;
	}

	//Load Mesh Data
	for (fastgltf::Mesh& mesh : asset.meshes) {
		for (auto&& p : mesh.primitives) {
			//Indices
			fastgltf::Accessor& index_accessor = asset.accessors[p.indicesAccessor.value()];

			fastgltf::iterateAccessor<std::uint32_t>(asset, index_accessor,
				[&](std::uint32_t idx) {
					//Get Indices Data
				});

			//Vertex Positions
			fastgltf::Accessor& pos_accessor = asset.accessors[p.findAttribute("POSITION")->accessorIndex];

			fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, pos_accessor,
				[&](glm::vec3 pos, std::size_t idx) {
					//Get Vertex Pos Data
				});

			//Vertex Normals
			auto normals_attrib = p.findAttribute("NORMAL");
			if (normals_attrib != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, asset.accessors[normals_attrib->accessorIndex],
					[&](glm::vec3 norm, std::size_t idx) {
						//Get Normal Data
					});
			}

			//Figure out how many of each indexed attribute
			int texAttribCount = 0;
			int colorAttribCount = 0;
			for (fastgltf::Attribute a : p.attributes) {
				if (a.name.find("TEXCOORD") != -1)
					texAttribCount++;
				else if (a.name.find("COLOR") != -1)
					colorAttribCount++;
			}

			//UV Coords
			for (int i = 0; i < texAttribCount; i++) {
				std::string attribName = "TEXCOORD_" + std::to_string(i);
				auto uv_attrib = p.findAttribute(attribName);
				if (uv_attrib != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec2>(asset, asset.accessors[uv_attrib->accessorIndex],
						[&](glm::vec2 uv, std::size_t idx) {
							//Get UV Coord
						});
				}
			}

			//Vertex Colors
			for (int i = 0; i < colorAttribCount; i++) {
				std::string attribName = "COLOR_" + std::to_string(i);
				auto color_attrib = p.findAttribute(attribName);
				if (color_attrib != p.attributes.end()) {
					fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, asset.accessors[color_attrib->accessorIndex],
						[&](glm::vec4 color, size_t idx) {
							//Get Color Data
						});
				}
			}
		}
	}

	//Load Nodes
	fastgltf::iterateSceneNodes(asset, 0, fastgltf::math::fmat4x4(),
		[&](fastgltf::Node& node, fastgltf::math::fmat4x4 matrix) {
			//Get Node Data 

		});
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

std::optional<AllocatedImage> load_image(Engine& engine, fastgltf::Asset& asset, fastgltf::Image& image) {
	AllocatedImage newImage{};
	int width, height, nrChannels;

	std::visit(fastgltf::visitor{
		[](auto& arg) {},
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

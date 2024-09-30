#pragma once

#include <fastgltf/types.hpp>
#include <filesystem>
#include "vulkan/vulkan.h"
#include "graphic_data_types.h"
#include "engine.h" //Potential Circular Dependency Risk

//May make a struct to encapsulate to group these functions

void loadGLTFFile(GraphicsDataPayload& dataPayload, Engine& engine, std::filesystem::path filePath);

//Converts GLTF Texture Sampler Filter Types to Vulkan Types
VkFilter extract_filter(fastgltf::Filter filter);

//Converts GLTF Texture Sampler MipMap Mode Types to Vulkan Types
VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter);

//Converts GLTF Topology Type of a Primitive to Vulkan Types
VkPrimitiveTopology extract_topology_type(fastgltf::PrimitiveType type);

std::optional<AllocatedImage> load_image(Engine& engine, fastgltf::Asset& asset, fastgltf::Image& image);
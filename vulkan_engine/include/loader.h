#pragma once

#include <fastgltf/types.hpp>
#include <filesystem>
#include "vulkan/vulkan.h"
#include "engine.h" //Potential Circular Dependency Risk

//May make a struct to encapsulate to group these functions

void loadGLTFFile(Engine& engine, std::filesystem::path filePath);

//Converts GLTF Texture Sampler Filter Types to Vulkan Types
VkFilter extract_filter(fastgltf::Filter filter);

//Converts GLTF Texture Sampler MipMap Mode Types to Vulkan Types
VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter);

std::optional<AllocatedImage> load_image(Engine& engine, fastgltf::Asset& asset, fastgltf::Image& image);
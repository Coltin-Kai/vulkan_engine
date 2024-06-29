#pragma once
#include "vulkan/vulkan.h"
#include "vulkan/vk_enum_string_helper.h"

#include <iostream>
#include <format>

#define VK_CHECK(x)                                                     \
    do {                                                                \
		if (x != VK_SUCCESS) {	\
			std::cout << std::format("Detected Vulkan Error: {}", string_VkResult(x)) << std::endl; \
			abort();	\
		}	\
    } while (0)
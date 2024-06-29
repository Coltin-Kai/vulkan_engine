#pragma once

#include "vulkan/vulkan.h"

namespace vkutil {
	void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
	
	VkImageSubresourceRange image_subresource_range(VkImageAspectFlags aspectMask);

	VkSemaphoreSubmitInfo semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphre);

	VkCommandBufferSubmitInfo command_buffer_submit_info(VkCommandBuffer cmd);

	VkSubmitInfo2 submit_info(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitsemaphoreInfo);
}
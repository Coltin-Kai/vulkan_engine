#include "vulkanUtility.h"

void vkutil::transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout) {
	VkImageMemoryBarrier2 imageBarrier{};
	imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	imageBarrier.pNext = nullptr;
	
	imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

	imageBarrier.oldLayout = currentLayout;
	imageBarrier.newLayout = newLayout;

	VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	imageBarrier.subresourceRange = vkutil::image_subresource_range(aspectMask);
	imageBarrier.image = image;

	VkDependencyInfo depInfo{};
	depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	depInfo.pNext = nullptr;

	depInfo.imageMemoryBarrierCount = 1;
	depInfo.pImageMemoryBarriers = &imageBarrier;

	vkCmdPipelineBarrier2(cmd, &depInfo);
}

VkImageSubresourceRange vkutil::image_subresource_range(VkImageAspectFlags aspectMask) {
	VkImageSubresourceRange subImage{};
	subImage.aspectMask = aspectMask;
	subImage.baseMipLevel = 0;
	subImage.levelCount = VK_REMAINING_MIP_LEVELS;
	subImage.baseArrayLayer = 0;
	subImage.layerCount = VK_REMAINING_ARRAY_LAYERS;

	return subImage;
}

VkSemaphoreSubmitInfo vkutil::semaphore_submit_info(VkPipelineStageFlags2 stageMask, VkSemaphore semaphre) {
	VkSemaphoreSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	submitInfo.pNext = nullptr;
	submitInfo.semaphore = semaphre;
	submitInfo.stageMask = stageMask;
	submitInfo.deviceIndex = 0;
	submitInfo.value = 1;

	return submitInfo;
}

VkCommandBufferSubmitInfo vkutil::command_buffer_submit_info(VkCommandBuffer cmd) {
	VkCommandBufferSubmitInfo info{};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	info.pNext = nullptr;
	info.commandBuffer = cmd;
	info.deviceMask = 0;

	return info;
}

VkSubmitInfo2 vkutil::submit_info(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitsemaphoreInfo) {
	VkSubmitInfo2 info{};
	info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	info.pNext = nullptr;
	info.waitSemaphoreInfoCount = waitsemaphoreInfo == nullptr ? 0 : 1;
	info.pWaitSemaphoreInfos = waitsemaphoreInfo;
	info.signalSemaphoreInfoCount = signalSemaphoreInfo == nullptr ? 0 : 1;
	info.pSignalSemaphoreInfos = signalSemaphoreInfo;
	info.commandBufferInfoCount = 1;
	info.pCommandBufferInfos = cmd;

	return info;
}

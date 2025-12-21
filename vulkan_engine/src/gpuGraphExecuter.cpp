#include "gpuGraphExecuter.h"

void gpu_graph::GPU_GraphExecuter::compile() {
	assert(passes.size() > 0 && "Graph Executer attempted to execute on an empty list of passes");

	compileAdjacencyList();
}

void gpu_graph::GPU_GraphExecuter::compileAdjacencyList() {
	size_t passCount = passes.size();

	for (PassHandle i = 0; i < passCount; i++) {
		directedAdjacencyList.emplace_back();
		const Pass& pass1 = passes[i];

		//Find if there is a directed adjacency from pass1 to pass2
		for (PassHandle j = 0; j < passCount; j++) {
			const Pass& pass2 = passes[j];

			//Evaluates if the dependency contains a buffer subresource that is read by or interesects with a subresource read by pass2.
			auto isThereBufferSubresourceDependency = [=this, &pass2](DataResourceDependency dependency) {
				//False if pass2 does not read from the buffer resource describe by dependency
				if (!pass2.readBufferTargets.contains(dependency))
					return false;

				//Find matching or intersecting buffer subresources
				const DataResourceDependency& readDependency = *(pass2.readBufferTargets.find(dependency));
				for (vDataSubResourceHandle subResourceHandle : dependency.subResources) {
					BufferSubResource subResource = this->graphResource.getBufferSubResource(subResourceHandle);

					for (vDataSubResourceHandle readSubResourceHandle : readDependency.subResources) {
						if (subResourceHandle == readSubResourceHandle)
							return true;
						else {
							BufferSubResource readSubResource = this->graphResource.getBufferSubResource(readSubResourceHandle);

							//Find in readSubResource buffer region intersects with subResource
							if (readSubResource.offset <= (subResource.offset + subResource.range) && (readSubResource.offset + readSubResource.range) >= subResource.offset) {
								return true;
							}
						}
					}
				}

				return false;
			};

			//Evaluates if the dependency contains an image subresource that is read by or intersects with a subresource read by pass2
			auto isThereImageSubresourceDependency = [=this, &pass2](DataResourceDependency dependency) {
				//False if pass2 does not read from the image resource described by dependency
				if (!pass2.readImageTargets.contains(dependency))
					return false;

				//Find matching or intersecting image subresources
				const DataResourceDependency& readDependency = *(pass2.readImageTargets.find(dependency));
				for (vDataSubResourceHandle subResourceHandle : dependency.subResources) {
					ImageSubResource subResource = this->graphResource.getImageSubResource(subResourceHandle);
					VkImageSubresourceRange range = subResource.range;
					VkOffset3D offset = subResource.offset;
					VkExtent3D extent = subResource.extent;

					for (vDataSubResourceHandle readSubResourceHandle : readDependency.subResources) {
						if (subResourceHandle == readSubResourceHandle)
							return true;
						else {
							ImageSubResource readSubResource = this->graphResource.getImageSubResource(readSubResourceHandle);
							VkImageSubresourceRange readRange = readSubResource.range;
							VkOffset3D readOffset = subResource.offset;
							VkExtent3D readExtent = subResource.extent;

							//Check if Image Aspects intersect
							if (readRange.aspectMask & range.aspectMask) {
								//Check if Array Layers and Mip Levels intersect
								uint32_t lastLayer = range.baseArrayLayer + range.layerCount;
								uint32_t readLastLayer = readRange.baseArrayLayer + readRange.layerCount;
								uint32_t lastLevel = range.baseMipLevel + range.levelCount;
								uint32_t readLastLevel = readRange.baseMipLevel + readRange.levelCount;
								if ((readRange.baseArrayLayer <= lastLayer && readLastLayer >= range.baseArrayLayer) && (readRange.baseMipLevel <= lastLevel && readLastLevel >= range.baseMipLevel)) {
									//Check if texel regions intersect
									uint32_t lastX = offset.x + extent.width;
									uint32_t readLastX = readOffset.x + readExtent.width;
									uint32_t lastY = offset.y + extent.height;
									uint32_t readLastY = readOffset.y + readExtent.height;
									uint32_t lastZ = offset.z + extent.depth;
									uint32_t readLastZ = readOffset.z + readExtent.height;
									if ((readOffset.x <= lastX && readLastX >= offset.x) && (readOffset.y <= lastY && readLastY >= offset.y) && (readOffset.z <= lastZ && readLastZ >= offset.z))
										return true;
								}
							}
						}
					}
				}

				return false;
			};

			//Query if Pass2 is dependent on Pass1 through either buffer or image dependencies
			bool isDependent = std::any_of(pass1.writeBufferTargets.begin(), pass1.writeBufferTargets.end(), isThereBufferSubresourceDependency) || std::any_of(pass1.writeImageTargets.begin(), pass1.writeImageTargets.end(), isThereImageSubresourceDependency);

			if (isDependent) {
				directedAdjacencyList[i].push_back(j);
			}
		}
	}
}
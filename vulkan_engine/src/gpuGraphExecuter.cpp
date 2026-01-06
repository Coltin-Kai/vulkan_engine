#include "gpuGraphExecuter.h"

using namespace gpu_graph;

void GPU_GraphExecuter::compile() {
	assert(passes.size() > 0 && "Graph Executer attempted to execute on an empty list of passes");

	compileAdjacencyList();
	compileOrderedPasses();
	compileDependencyLevels();
}

//Fill adjacencyList from loaded passes
void GPU_GraphExecuter::compileAdjacencyList() {
	size_t passCount = passes.size();
	adjacencyLists.assign(passCount, {});

	for (PassHandle i = 0; i < passCount; i++) {
		const Pass& pass1 = passes[i];

		//Find if there is a directed adjacency from pass1 to pass2
		for (PassHandle j = 0; j < passCount; j++) {
			const Pass& pass2 = passes[j];

			//Note: Should use createinfo Instead since graphResources wont be created till after

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

					for (vDataSubResourceHandle readSubResourceHandle : readDependency.subResources) {
						if (subResourceHandle == readSubResourceHandle)
							return true;
						else {
							ImageSubResource readSubResource = this->graphResource.getImageSubResource(readSubResourceHandle);
							VkImageSubresourceRange readRange = readSubResource.range;

							//Check if Image Aspects intersect
							if (readRange.aspectMask & range.aspectMask) {
								//Check if Array Layers and Mip Levels intersect
								uint32_t lastLayer = range.baseArrayLayer + range.layerCount;
								uint32_t readLastLayer = readRange.baseArrayLayer + readRange.layerCount;
								uint32_t lastLevel = range.baseMipLevel + range.levelCount;
								uint32_t readLastLevel = readRange.baseMipLevel + readRange.levelCount;
								if ((readRange.baseArrayLayer <= lastLayer && readLastLayer >= range.baseArrayLayer) && (readRange.baseMipLevel <= lastLevel && readLastLevel >= range.baseMipLevel)) {
									return true
								}
							}
						}
					}
				}

				return false;
			};

			//Query if Pass2 is dependent on Pass1 through either buffer or image dependencies
			bool isDependent = std::any_of(pass1.writeBufferTargets.begin(), pass1.writeBufferTargets.end(), isThereBufferSubresourceDependency) || std::any_of(pass1.writeImageTargets.begin(), pass1.writeImageTargets.end(), isThereImageSubresourceDependency);

			//Pushes Pass2 handle to the adjacency list of Pass1
			if (isDependent) {
				adjacencyLists[i].push_back(j);
			}
		}
	}
}

//Fill orderedPasses with Topologically Sorted list of the loaded passes.
void GPU_GraphExecuter::compileOrderedPasses() {
	//Reset containers
	orderedPasses.resize(passes.size());
	DFS_visited.assign(passes.size(), false);
	DFS_onStack.assign(passes.size(), false);
	
	auto it = orderedPasses.rbegin();

	PassHandle i = 0;
	while (it != orderedPasses.rend()) {
		if (!DFS_visited[i]) {
			DFS_orderStack.push(i);
			i++;
		}

		while (!DFS_orderStack.empty()) {
			PassHandle j = DFS_orderStack.top();

			DFS_visited[j] = true;
			DFS_onStack[j] = true;

			const std::vector<PassHandle>& adjacencyList = adjacencyLists[i];

			if (adjacencyList.size() == 0) {
				*it = i;
				it++;
				DFS_orderStack.pop();
				DFS_onStack[j] = false;
			}

			else {
				for (PassHandle k = 0; k < adjacencyList.size(); k++) {
					if (DFS_visited[k]) {
						if (DFS_onStack[k])
							throw std::runtime_error("Circular Dependency in Graph");
					}

					else
					{
						DFS_onStack.push_back(k);
					}
				}
			}
		}
	}
}

void GPU_GraphExecuter::compileDependencyLevels() {
	dependencyLevels.assign(orderedPasses.size(), 0);

	for (PassHandle currentPass : orderedPasses) {
		for (PassHandle adjacentPass : adjacencyLists[currentPass]) {
			if (dependencyLevels[adjacentPass] < dependencyLevels[currentPass] + 1) {
				dependencyLevels[adjacentPass] = dependencyLevels[currentPass] + 1;
			}
		}
	}
}

void GPU_GraphExecuter::pushBackPass(const PassCreateInfo& passInfo) {
	size_t depLevel = 0;

	//Check all currently known written too subResources to evaluate the Pass' Dependency Level. If none found, dep level kept at 0.
	for (const SubResourceDependencyInfo& subResDepInfo : subResourceDepInfos) {
		SubResource& subResource = dataSubResources[subResDepInfo.subResource];

		//Evaluate if the current subResource is read from the Pass directly or through subResources that intersect with it
		for (vDataSubResourceHandle intersectingSubRsource : subResource.intersectingSubResources) {
			if (passInfo.readTargets.contains(intersectingSubRsource))
				if (depLevel <= subResDepInfo.passDepInfo.depLevel)
					depLevel = subResDepInfo.passDepInfo.depLevel + 1;
		}
	}

	//Insert Dependency Information for all SubResources that are written by this pass
	for (auto writeTarget : passInfo.writeTargets) {
		SubResourceDependencyInfo info;
		info.subResource = writeTarget.first;
		info.passDepInfo.name = passInfo.name
		info.passDepInfo.depLevel = depLevel;
		subResourceDepInfos.push_back(info);
	}

	
}

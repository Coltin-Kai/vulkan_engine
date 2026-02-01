#include "gpuGraphExecuter.h"

using namespace gpu_graph;

void gpu_graph::GPU_GraphExecuter::beginRecording() {
	assert(creatingRecording && "Can't Begin Recording in another Recording");
}

void gpu_graph::GPU_GraphExecuter::endRecording() {
	assert(!creatingRecording && "Cant End a Recording that was never Began");

	//Compile Last Pass
	if (creatingPass) {

	}

	//Compile Last Execution
	if (creatingExecution) {

	}

	creatingPass = false;
	creatingExecution = false;
	creatingRecording = false;
}

ExecutionHandle gpu_graph::GPU_GraphExecuter::beginExecution() {
	assert(!creatingRecording && "Execution Should be within a Recording");

	//If already true, then ther exists a previous Execution within the recording this execution is being created on.
	if (creatingExecution) {
		compileExecution();
	}
	else
		creatingExecution = true;

	return ExecutionHandle();
}

/*
	Set the states and sync of the following adding action commands.
*/
void gpu_graph::GPU_GraphExecuter::beginPass(const PassParameterInfo& passInfo) {
	assert(!creatingRecording && "Pass Should be within a Recording");
	assert(!creatingExecution && "Pass Should be within an Execution");

	//If already true, then there exists a previous Pass within the Recording this pass is being created on.
	if (creatingPass) {
		
	}

	else
		creatingPass = true;

	QueueHandle queue; //Specifies which Queue Pass in run on
	if (passInfo.queueType == QueueType::Primary)
		currentPassQueue = this->primaryQueue;
	else if (passInfo.queueType == QueueType::AsyncCompute)
		currentPassQueue = this->aSyncComputeQueue;
	else if (passInfo.queueType == QueueType::TransferDedicated)
		currentPassQueue = this->transferDedicatedQueue;

	//Dependency Level of Pass. If no dependency was found that the pass is reliant on, kept as 0
	currentPassDepLevel = 0;

	//Check all currently known written too subResources (dependent subresources) to evaluate potential dependency between it and the pass. And update relevent subresource dependencies and pass information based on it.
	evaluateDependencies(passInfo, externalSubResDeps, false);
	evaluateDependencies(passInfo, transientSubResDeps, true);

	//Update Read from External Subresource Dependenices with information pertaining to Pass and Dependency Level
	for (size_t dependencyHandle : readExternalSubResDeps) {
		SubResourceDependency& subResDep = externalSubResDeps[dependencyHandle];
		SubResourceDependency::ReadQueueAccess& readQueueAccess = subResDep.readQueueAccesses[queue];

		//If matches with writeDepLevel, then represents first instance of reading and thus just updated with read dep level
		if (readQueueAccess.earliestReadDepLevel == subResDep.writeDepLevel) {
			readQueueAccess.earliestReadDepLevel = currentPassDepLevel;
		}

		//Else check if the dependency is being read from a pass with a dep level smaller than its currently known dep level, and update accordingly.
		else {
			if (readQueueAccess.earliestReadDepLevel > currentPassDepLevel)
				readQueueAccess.earliestReadDepLevel = currentPassDepLevel;
		}
	}

	//Update Read from Transient Subresource Dependenices with information pertaining to Pass and Dependency Level
	for (size_t dependencyHandle : readTransientSubResDeps) {
		SubResourceDependency& subResDep = transientSubResDeps[dependencyHandle];
		SubResourceDependency::ReadQueueAccess& readQueueAccess = subResDep.readQueueAccesses[queue];

		//If matches with writeDepLevel, then represents first instance of reading and thus just updated with read dep level
		if (readQueueAccess.earliestReadDepLevel == subResDep.writeDepLevel) {
			readQueueAccess.earliestReadDepLevel = currentPassDepLevel;
		}

		//Else check if the dependency is being read from a pass with a dep level smaller than its currently known dep level, and update accordingly.
		else {
			if (readQueueAccess.earliestReadDepLevel > currentPassDepLevel)
				readQueueAccess.earliestReadDepLevel = currentPassDepLevel;
		}
	}

	//Insert Dependency Information for all SubResources that are written by this pass and Instantiate/Update Dependency Info for connected Transient Resources
	for (auto writeTarget : passInfo.writeTargets) {
		if (dataSubResourceMetaInfos[writeTarget.subResource].isTransient) {
			transientSubResDeps.push_back({});
			SubResourceDependency& subResDep = transientSubResDeps[transientSubResDeps.size() - 1];
			subResDep.subResource = writeTarget.subResource;
			subResDep.writeDepLevel = currentPassDepLevel;
			subResDep.readQueueAccesses = queue;
			subResDep.writeStages = writeTarget.stageAccessFlags;
			subResDep.writeAccessTypes = writeTarget.accessTypeFlags;

			SubResourceDependency::ReadQueueAccess readQueueAccess{};
			readQueueAccess.readStages = VK_PIPELINE_STAGE_2_NONE;
			readQueueAccess.readAccess = VK_ACCESS_2_NONE;
			readQueueAccess.earliestReadDepLevel = currentPassDepLevel;
			subResDep.readQueueAccesses.resize(this->queues.size(), readQueueAccess);
		}
		else {
			externalSubResDeps.push_back({});
			SubResourceDependency& subResDep = externalSubResDeps[externalSubResDeps.size() - 1];
			subResDep.subResource = writeTarget.subResource;
			subResDep.writeDepLevel = currentPassDepLevel;
			subResDep.readQueueAccesses = queue;
			subResDep.writeStages = writeTarget.stageAccessFlags;
			subResDep.writeAccessTypes = writeTarget.accessTypeFlags;

			SubResourceDependency::ReadQueueAccess readQueueAccess{};
			readQueueAccess.readStages = VK_PIPELINE_STAGE_2_NONE;
			readQueueAccess.readAccess = VK_ACCESS_2_NONE;
			readQueueAccess.earliestReadDepLevel = currentPassDepLevel;
			subResDep.readQueueAccesses.resize(this->queues.size(), readQueueAccess);
		}
	}

	readExternalSubResDeps.clear();
	readTransientSubResDeps.clear();
}

void gpu_graph::GPU_GraphExecuter::evaluateDependencies(const PassParameterInfo& passInfo, std::vector<SubResourceDependency>& subResourceDependencies, bool isTransient) {
	for (size_t i = 0; i < subResourceDependencies.size(); i++) {
		SubResourceDependency& subResDep = subResourceDependencies[i];
		DataSubResourceMetaInfo& dependentSubResourcesMetaInfo = dataSubResourceMetaInfos[subResDep.subResource];

		for (SubResourceReadTargetGroupHandle readTargetGroupHandle : passInfo.readTargets) {
			SubResourceReadTargetGroup& readTargetGroup = subResourceReadTargetGroups[readTargetGroupHandle];

			//Check to see if the resource of dependent subresource is read by pass
			if (readTargetGroup.readResources.contains(dependentSubResourcesMetaInfo.resource)) {
				//Evaluate if the dependent subresource is read from or intersects with subresources read by the pass
				const std::vector<SubResourceTargetInfo>& subresourceReadTargets = readTargetGroup.readResources[dependentSubResourcesMetaInfo.resource];

				for (SubResourceTargetInfo readTarget : subresourceReadTargets) {
					bool isDependent = false;

					//Check if the same subresource
					if (subResDep.subResource == readTarget.subResource) {
						isDependent = true;
					}

					//Else check for intersection between dependent and read subresource
					else {
						if (dependentSubResourcesMetaInfo.type == ResourceType::BufferType) {
							SubBufferRange dependentsRange = std::get<SubBufferRange>(dependentSubResourcesMetaInfo.range);
							SubBufferRange readsRange = std::get<SubBufferRange>(dataSubResourceMetaInfos[readTarget.subResource].range);

							if (dependentsRange.offset + dependentsRange.size > readsRange.offset && dependentsRange.offset < readsRange.offset + readsRange.size) {
								isDependent = true;
							}
						}

						else if (dependentSubResourcesMetaInfo.type == ResourceType::ImageType) {
							SubImageRange dependentsRange = std::get<SubImageRange>(dependentSubResourcesMetaInfo.range);
							SubImageRange readsRange = std::get<SubImageRange>(dataSubResourceMetaInfos[readTarget.subResource].range);

							if (dependentsRange.aspectMask | readsRange.aspectMask) {
								if (dependentsRange.baseArrayLayer + dependentsRange.layerCount > readsRange.baseArrayLayer && dependentsRange.baseArrayLayer > readsRange.baseArrayLayer + readsRange.layerCount) {
									if (dependentsRange.baseMipLevel + dependentsRange.levelCount > readsRange.baseMipLevel && dependentsRange.baseMipLevel > readsRange.baseMipLevel + readsRange.levelCount) {
										isDependent = true;
									}
								}
							}
						}
					}

					if (isDependent) {
						//Update Pass's Dependency Level if found to read from a subresource dependency written at a equal or larget dep level.
						if (currentPassDepLevel <= subResDep.writeDepLevel) {
							currentPassDepLevel = subResDep.writeDepLevel + 1;
						}

						//Update Depndent subresource dependency and push it to container
						SubResourceDependency::ReadQueueAccess& readQueueAccess = subResDep.readQueueAccesses[queue];
						readQueueAccess.readStages |= readTarget.stageAccessFlags;
						readQueueAccess.readAccess |= readTarget.accessTypeFlags;

						if (isTransient) {
							readTransientSubResDeps.push_back(i);
						}
						else
							readExternalSubResDeps.push_back(i);
					}
				}
			}
		}
	}
}

void gpu_graph::GPU_GraphExecuter::compileExecution() {
	//Generate and Compile Sync Structures and 

	//Compile Transient Resources
	compileTransientResources();
}

void gpu_graph::GPU_GraphExecuter::compileTransientResources() {
}

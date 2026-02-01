#pragma once
#include "renderGraph.h"
#include <stack>
#include <span>

namespace gpu_graph {
	using ExecutionHandle = size_t; //Identifies a execution of a graph
	using QueueHandle = size_t; //Identifies a Queue
	using PassHandle = size_t; //Identifies a unique pass within the executers list of passes that is filled during pass setup
	using ResourceHandle = size_t; //Represents handle to actual resource data.
	using ActionCommands = std::function<void(GraphResources)>;

	enum ResourceType {
		BufferType,
		ImageType
	};

	struct SubResourceTargetInfo {
		VkPipelineStageFlags2 stageAccessFlags;
		VkAccessFlags2 accessTypeFlags;
		vDataSubResourceHandle subResource;
	};

	struct ExecutionParameterInfo {
		const char* name;
		std::span<ExecutionHandle> ExecutionContingencies; //Executions that the Execution must wait on to finish
	};

	struct PassParameterInfo {
		const char* name;
		PassType type;
		QueueType queueType; //Specifies which queue to utilize
		std::vector<SubResourceReadTargetGroupHandle> readTargets;
		std::span<SubResourceTargetInfo> writeTargets;
	};

	struct Execution {

	};

	struct TransientBufferCreateInfo {
		bool bufferDeviceAddressable; //Generae a addressable buffer with the appropriate address.
		VkDeviceSize size;
	};

	struct TransientImageCreateInfo {
		VkImageType type;
		VkFormat format;
		VkExtent3D extent;
		uint32_t mipLevels;
		uint32_t arrayLayers;
		VkSampleCountFlags samples;
		VkImageTiling tiling;
	};

	struct SubBufferRange {
		VkDeviceSize offset;
		VkDeviceSize size;
	};

	struct SubImageRange {
		VkImageAspectFlags aspectMask;
		uint32_t baseMipLevel;
		uint32_t levelCount;
		uint32_t baseArrayLayer;
		uint32_t layerCount;
	};

	struct BufferViewCreateInfo {
		VkBufferViewCreateFlags flags;
		VkFormat format;
	};

	struct BufferSubResourceCreateInfo {
		vDataResourceHandle resource;
		SubBufferRange range;

		std::optional<BufferViewCreateInfo> bufferViewInfo;
	};

	struct ImageViewCreateInfo {
		VkImageViewCreateFlags flags;
		VkImageViewType viewType;
		VkFormat format;
		VkComponentMapping components;
	};

	struct ImageSubresourceCreateInfo {
		vDataResourceHandle resource;
		SubImageRange range;

		std::optional<ImageViewCreateInfo> imageViewInfo;
	};

	enum class DescriptorType {
		SampledImage,
		StorageImage,
		UniformBuffer,
		UniformTexelBuffer,
		StorageTexelBuffer
	};

	struct GraphicsPipelineCreateInfo {
		VkPipelineCreateFlags flags;
		uint32_t stageCount;
		const VkPipelineShaderStageCreateInfo* pStages;
		const VkPipelineVertexInputStateCreateInfo* pVertexInputState;
		const VkPipelineInputAssemblyStateCreateInfo* pInputAssemblyState;
		const VkPipelineTessellationStateCreateInfo* pTessellationState;
		const VkPipelineViewportStateCreateInfo* pViewportState;
		const VkPipelineRasterizationStateCreateInfo* pRasterizationState;
		const VkPipelineMultisampleStateCreateInfo* pMultisampleState;
		const VkPipelineDepthStencilStateCreateInfo* pDepthStencilState;
		const VkPipelineColorBlendStateCreateInfo* pColorBlendState;
		const VkPipelineDynamicStateCreateInfo* pDynamicState;
		uint32_t pushConstantRangeCount;
		const VkPushConstantRange* pPushConstantRanges;
	};

	struct ComputePipelineCreateInfo {
		VkPipelineCreateFlags flags;
		VkPipelineShaderStageCreateInfo stage;
		uint32_t pushConstantRangeCount;
		const VkPushConstantRange* pPushConstantRanges;
	};

	class GPU_GraphExecuter {
	public:
		//Resource Registration and Creation
		void addOperationQueue(QueueType type, VkQueue queue, uint32_t queueFamily);

		//Pipelines are compiled once executer starts compiling, as graphics pipeline is dependedent on renderPass/renderingInfo, which is defined by passes. 
		vPipelineHandle addPipeline(const GraphicsPipelineCreateInfo& info); 
		vPipelineHandle addPipeline(const ComputePipelineCreateInfo& info); 

		//External resources represent resources persistent across execution that only need to be added once for Executer to remember across executions. Ultimately managed by user. 
		vDataResourceHandle addExternalDataResource(VkBufferCreateInfo bufferCreateInfo);
		vDataResourceHandle addExternalDataResource(VkImageCreateInfo imageCreateInfo);
		vDataSubResourceHandle addExternalSubResource(BufferSubResourceCreateInfo info);
		vDataSubResourceHandle addExternalSubResource(ImageSubresourceCreateInfo info);

		//Destroys the underlying external resource and frees the handle
		void removeExternalBufferResource(vDataResourceHandle handle);
		void removeExternalImageResource(vDataResourceHandle handle);
		void removeExternalBufferSubResource(vDataSubResourceHandle handle);
		void removeExternalImageSubResource(vDataSubResourceHandle handle);

		//Transient resources only exist on a per execution bases and how its allocated and managed is done by the Executer. Do not need to be updated or removed by user
		vDataResourceHandle addTransientDataResource(TransientBufferCreateInfo info); 
		vDataResourceHandle addTransientDataResource(TransientImageCreateInfo info);

		vDataSubResourceHandle addTransientDataSubResource(BufferSubResourceCreateInfo info);
		vDataSubResourceHandle addTransientDataSubResource(ImageSubresourceCreateInfo info);

		//Adds SubresourceReadTargets. External ones can only hold non-transient subresources. Transient ones can hold both.
		SubResourceReadTargetGroupHandle addExternalReadTargets(std::span<vDataSubResourceHandle> subResources, std::span<SubResourceTargetInfo> subResourceTargetInfos); //Note: subResourcesTargetInfo i describes subresource i.
		SubResourceReadTargetGroupHandle addTransientReadTargets(std::span<vDataSubResourceHandle> subResources, std::span<SubResourceTargetInfo> subResourceTargetInfos);

		void addSubResourceToReadTargets(SubResourceReadTargetGroupHandle readTargetHandle, vDataSubResourceHandle subResourceHandle, SubResourceTargetInfo Targetinfo);
		void removeSubResourceFromReadTargets(SubResourceReadTargetGroupHandle readTargetHandle, vDataSubResourceHandle subResourceHandle);

		void removeExternalReadTargets(SubResourceReadTargetGroupHandle handle);

		//Registers an External subresource to the Global Descriptor Set. Returns index to their descriptor in the respective binding.
		uint32_t registerGlobalDescriptor(vDataSubResourceHandle handle, DescriptorType type);
		uint32_t registerSampler(VkSamplerCreateInfo info);

		//Execution Functions
		void beginRecording();
		void endRecording(); //Submit Commands
		ExecutionHandle beginExecution(); 
		void beginPass(const PassParameterInfo& passInfo);

		void addAction(std::function<void(GraphResources)> actionCommands);

		//State Setting Functions...

	private:
		//Represents resource's meta info and relationships with subresources
		struct DataResourceMetaInfo {
			bool isTransient;
			ResourceType type;
			VkImageLayout imageLayout; //If the Resource is a Buffer or a Transient Image, layout is Undefined. Never changes outside Executions
			std::vector<vDataSubResourceHandle> subResources;
		};

		//Represents subResources's meta info and relatioship with its representing resource and other subresources
		struct DataSubResourceMetaInfo {
			bool isTransient;
			bool hasViewObject; //Indicates if Subresource also represents a vkBufferView/vkImageView object
			ResourceType type;
			vDataResourceHandle resource;

			std::variant<SubBufferRange, SubImageRange> range;
		};

		struct QueueInfo {
			VkQueue queue;
			uint32_t queueFamily;
		};

		//Represents a bundle of subresources that are targetted for read or write access by a pass
		struct SubResourceReadTargetGroup {
			bool isTransient;
			std::unordered_map<vDataResourceHandle, std::vector<SubResourceTargetInfo>> readResources; //Contains a mapping of the resources to all the subresources that are read as part of the ReadTarget
		};
		
		//Describes a Subresource that is Dependency within a Execution Graph. AKA Subresources that are written too.
		struct SubResourceDependency {
			struct ReadQueueAccess {
				VkPipelineStageFlags2 readStages; //Combined Read Stages performed by the Queue
				VkAccessFlags2 readAccess; //Combined Read Access performed by the Queue
				uint32_t earliestReadDepLevel;
			};

			vDataSubResourceHandle subResource;
			QueueHandle writeQueue;
			uint32_t writeDepLevel;
			VkPipelineStageFlags2 writeStages;
			VkAccessFlags2 writeAccessTypes;
			std::vector<ReadQueueAccess> readQueueAccesses; //Index with QueueHandle, the queues that perform read access on the subresource
		};

		//MemoryBarrier

		//Represents all the types of barriers for a pipeline barrier to utilize
		struct PipelineBarrier {
			std::vector<VkMemoryBarrier2> memoryBarriers;
			std::vector<VkBufferMemoryBarrier2> bufferBarriers;
			std::vector<VkImageMemoryBarrier2> imageBarriers;
		};

		struct QueueCommandRecording {
			struct CommandBufferRange {
				bool immutable; //Specifies that the range cant be altered after initlization. This is usually the result of it being used to designate a set of operations as part of an Execution
				size_t depLevelOffset; //Dep Level Offset of the Command Buffer
				size_t depLevelCount; //Dep Level Count from the offset
			};

			std::vector<PipelineBarrier> pipelineBarriers; //Describes the PipelineBarriers that needs to be inserted before and after each dep level. Where PipelineBarrier i is inserted before dep level i and i+1 is inserted after dep level i.
			std::vector<std::vector<ActionCommands>> actionCommands; //Represents a series of Action Commands within each Dependency Level of a Queue
			std::vector<CommandBufferRange> commadBufferRanges; //Represents each Command Buffer to record and the offset they start at
		};

		//
		struct StateUpdates {

		};

		//A Edge group represents a collection of edges representing all combinations of ((queue, srcStage),(queue, dstStage)) directed edges
		struct EdgeGroup {
			struct QueueStages {
				VkPipelineStageFlags2 srcStages;
				VkPipelineStageFlags2 dstStages;
			};

			std::unordered_set<size_t> contingentEdgeGroups; //Represents a set of handles pointing to edge groups that exist on a path to this edge group
			std::vector<QueueStages> queueStages; //Index with QueueHandle, represents the stages on each queue and specifies the tails and heads within the Edge group
		};

		//Executer State
		bool creatingRecording;
		bool creatingExecution; //Indicates if currently creating an execution
		bool creatingPass; //Indicates if currently creating a pass within an execution

		//Pass Read and Write Target Data
		std::vector<SubResourceReadTargetGroup> subResourceReadTargetGroups; //Index with SubResourceTargetHandle

		//Containers for building up passes and execution
		size_t currentPassDepLevel; //Represents the Dependency Level the current creating Pass is inserted into
		QueueHandle currentPassQueue; //Represents the Queue the current creating Pass is inserted onto
		std::vector<SubResourceDependency> externalSubResDeps; //Represents subresources of external resources that were written too. Reset after every recording
		std::vector<SubResourceDependency> transientSubResDeps; //Represents subresources of transient resources that were written too. Reset after every Execution.
		std::vector<size_t> readExternalSubResDeps; //Holds the handles to subresource dependencies in externalSubResDeps and represents thsoe read from a pass. Used to update relevant subresource dependenies after figuring out pass info. Reset after every pass
		std::vector<size_t> readTransientSubResDeps; //Holds the handles to subresource dependencies in transientSubResDeps and represents thsoe read from a pass. Used to update relevant subresource dependenies after figuring out pass info. Reset after every pass

		std::vector<StateUpdates> stateUpdates; //Index with QueueHandle. Contains all the updates to states
		
		std::vector<QueueCommandRecording> queueCommandRecordings; //Index with QueueHandle, represents each Recording

		//List of Transient Resource Creation Infos that are dependent on pass info.
		std::unordered_map<vDataResourceHandle, std::variant<TransientBufferCreateInfo, TransientImageCreateInfo>> transientResourceCreateInfos;
		std::unordered_map<vDataSubResourceHandle, std::variant<BufferViewCreateInfo, ImageViewCreateInfo>> transientSubResourceViewCreateInfos;
		std::vector<vDataResourceHandle> transientResourceCreationQueue; //Holds the list of Transient Resource whos underlying resource need to be instatiated
		std::vector<vDataSubResourceHandle> transientSubResourceCreationQueue; //Holds the list of Transient SubREsources whos underlying resource needs to be instantiated.

		//Resources and their Relationships. Reset after every execution so that any transient resource handles and relations are removed but external are kept
		std::vector<DataResourceMetaInfo> dataResourceMetaInfos; //Index with vDataResourceHandle
		std::vector<DataSubResourceMetaInfo> dataSubResourceMetaInfos; //Index with vDataSubResourceHandle

		//Command Accessable Data
		GraphResources graphResources;

		//Underlying Vulkan Object Data
		std::vector<std::variant<std::pair<VkBuffer, VkDeviceAddress>, VkImage>> vDataResources;
		std::vector<std::variant<VkBufferView, VkImageView>> vDataSubResources;

		//Queue
		QueueHandle primaryQueue;
		QueueHandle aSyncComputeQueue;
		QueueHandle transferDedicatedQueue;
		std::vector<QueueInfo> queues;

		void evaluateDependencies(const PassParameterInfo& passInfo, std::vector<SubResourceDependency>& subResourceDependencies, bool isTransient);
		void compileExecution();
		void compileTransientResources();
	};
}

/*
	Have to figure out how many command buffers each queue needs, as for every cross queue dependency requres a new command buffer for both queues to start a new recording.
	Also want to layout data of passes for recording where passes on the same queue are recorded together. Thus requires knowing the cross queue dependencies, 
	as each one requires the participating queues to use one more command buffer and to split recordings.

	For each queue, can use a vector to store all passes that need to be recorded on them, ordered by dependency levels so they can be recorded together.
	To insert syncs correctly, need to know when to place barriers (and what kind) and when to bundle a recording with wait and signal info.

	So, at each dep level, if at least one of the passes participate in cross queue dependency, split the recording. Passes not participating in cross queue and those that
	are only writting to resources that do will be kept in the first half of recording. This submission will signal the cross queue semaphore. Passes reading cross queue resources
	will then be put on a new command buffer recording and future passes will do the same until another cross queue dependency occurs at a future dep level. Thus repeat.
	If there are passes that both read and write cross queue dependenices, would need split into a third command buffer that would wait for the reads and signal to writes.
	
	So it would probably look like that for each queue, we have a vector of vector of passes, where each vector represents all the passes for one command buffer recording.

	Though remember to minimize cross queue syncing so that we only need to sync at the earlist instances where a cross queue read is neccesary after a write to a resource.

	Aliasing. In order for transient resources to alias with each other, they must both have non-conflicting lifetimes and there must be a path from the pass that last
	reads from it to the first pass that writes to the other resource. 
*/
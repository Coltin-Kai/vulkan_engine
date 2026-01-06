#pragma once
#include "renderGraph.h"
#include <stack>
#include <span>

namespace gpu_graph {
	using ExecutionHandle = size_t; //Identifies a execution of a graph
	using QueueHandle = size_t; //Identifies a Queue
	using PassHandle = size_t; //Identifies a unique pass within the executers list of passes that is filled during pass setup
	using ResourceHandle = size_t; //Represents handle to actual resource data.

	enum ResourceType {
		BufferType,
		ImageType
	};

	struct SubResourceTargetInfo {
		ResourceType type;
		VkPipelineStageFlags2 stageAccessFlags;
		VkAccessFlags2 accessTypeFlags;
	};

	struct PassCreateInfo {
		const char* name;
		PassType type;
		QueueType queueType; //Specifies which queue to utilize
		bool transientOperation; //Specifies if Pass may not exist more than one frame. Hint on how to treat aliasing transient resources it writes too.
		std::unordered_map<vDataSubResourceHandle, SubResourceTargetInfo> readTargets;
		std::span<std::pair<vDataSubResourceHandle, SubResourceTargetInfo>> writeTargets;

		std::variant<GraphicsCommands, ComputeCommands, TransferCommands> commands;
	};

	struct GraphExecution {

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

	struct BufferSubResourceCreateInfo {
		vDataResourceHandle resource;
		VkDeviceSize offset;
		VkDeviceSize range;
	};

	struct ImageSubresourceCreateInfo {
		struct ImageViewCreateInfo {
			VkImageViewCreateFlags flags;
			VkImageViewType viewType;
			VkFormat format;
			VkComponentMapping components;
		};

		vDataResourceHandle resource;
		VkImageAspectFlags aspectMask;
		uint32_t baseMipLevel;
		uint32_t levelCount;
		uint32_t baseArrayLayer;
		uint32_t layerCount;

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
		void pushBackPass(const PassCreateInfo& passInfo);
		void addQueue(QueueType type, VkQueue queue, uint32_t queueFamily);
		void waitOnExecutions(const GraphExecution* executions, size_t numExecutions); //CPU wait on Exectuions.
		GraphExecution execute(const GraphExecution* dependentExecutions, size_t numExecutions); //Compile and Execute Passes

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

		//Registers an External subresource to the Global Descriptor Set. Returns index to their descriptor in the respective binding.
		uint32_t registerGlobalDescriptor(vDataSubResourceHandle handle, DescriptorType type);
		uint32_t registerSampler(VkSamplerCreateInfo info);

	private:
		using VkBufferHandle = size_t;
		using VkImageHandle = size_t;

		//Represents resources and their logical relationships with each other and the underlying resource		
		struct ImageInfo {
			VkImageHandle image;
		};

		struct BufferInfo {
			VkBufferHandle buffer; //Represents the real underlying VkBuffer
			VkDeviceAddress deviceAddress; //The address to the underlying VkBuffer if available
			VkDeviceSize offset; //Offset and Range in the underlying VkBuffer
			VkDeviceSize range;
		};

		//Represents an Image Subresource
		struct SubImageInfo {
			VkImageView imageView;
			VkImageSubresourceRange range;
		};

		//Represents a Buffer Subresource
		struct SubBufferInfo {
			VkBufferView bufferView;
			VkDeviceSize offset; //Offset and Range of the subrange of the virtual buffer.
			VkDeviceSize range;
		};

		struct Resource {
			std::vector<vDataSubResourceHandle> subResources;

			std::variant<ImageInfo, BufferInfo> resourceInfo;
		};

		struct SubResource {
			vDataResourceHandle resource;
			std::vector<vDataSubResourceHandle> intersectingSubResources;

			std::variant<SubImageInfo, SubBufferInfo> subResourceInfo;
		};

		struct DirectedAdjacencyList {
		private:
			size_t virtualListSize = 0; //Represents the size of the list of available vectors. Guarentees that vectors existing outside this range are empty.
			std::vector<std::vector<PassHandle>> list;
		public:
			size_t size() const {
				return virtualListSize;
			}

			//Updates container to support the given number of lists
			void resize(size_t size) {
				//If internal list is less than size, update it with empty vectors till matches size
				if (list.size() < size) {
					list.reserve(size);
				}

				while (list.size() < size) {
					list.emplace_back();
				}

				//If size is less than virtual size, clear all vectors between size and virtual size range
				for (size_t i = size; i < virtualListSize; i++) {
					list[i].clear();
				}
			}

			//Clears all vectors that were utilized by the list.
			void clear() {
				for (PassHandle i = 0; i < virtualListSize; i++) {
					list[i].clear();
				}

				virtualListSize = 0;
			}

			//Destruct vectors outside the virtual size range so that the internal container size matches the virtual. Used to free memory occupied by unused internal vectors
			void compactLists() {
				list.resize(virtualListSize);
			}

			//Potential for accessing lists that are out of bounds of the given virtual size.
			const std::vector<PassHandle>& operator[](PassHandle pass) const {
				return list[pass];
			}

			std::vector<PassHandle>& operator[](PassHandle pass) {
				return list[pass];
			}
		};

		struct QueueInfo {
			VkQueue queue;
			uint32_t queueFamily;
		};

		struct PassDepndencyInfo { //Not really sure need this. Can maybe just have SubResourceDependencyInfo contain the
			std::string name;
			uint32_t depLevel;
		};

		struct SubResourceDependencyInfo {
			vDataSubResourceHandle subResource;
			PassDepndencyInfo passDepInfo; //Describes the Pass that writes to the subResource;
		};

		struct QueueCommandList {
			std::vector<std::vector<std::variant<GraphicsCommands, ComputeCommands, TransferCommands>>> commands; //First Dimension represents the dependency level. Second is the list of passes within each dependency level
		};

		struct PassDependentTransientResourceCreationInfo {
			std::variant<VkBufferUsageFlags, VkImageUsageFlags> usageFlags;
			VkSharingMode sharingMode;
			uint32_t queueFamily;

		};

		//idk to keep:
		struct DataSubResourceAccessInfo {
			VkPipelineStageFlags2 accessStages;
			VkAccessFlags2 accessTypes;
		};

		//Represents a distinct GPU Operation performed on a read and write Targets. Internal struct managed by Executer
		struct Pass {
			const char* name;
			PassType type; //Specifies what kind of commands this pass executes
			QueueType queueType; //Specifies which queue the pass commands should execute on.
			bool transientOperation;

			std::unordered_map<vDataResourceHandle, std::vector<vDataSubResourceHandle>> readBufferTargets;
			std::unordered_map<vDataResourceHandle, std::vector<vDataSubResourceHandle>> readImageTargets;

			std::vector<std::pair<vDataResourceHandle, std::vector<vDataSubResourceHandle>>> writeBufferTargets;
			std::vector<std::pair<vDataResourceHandle, std::vector<vDataSubResourceHandle>>> writeImageTargets;

			std::unordered_map<vDataSubResourceHandle, DataSubResourceAccessInfo> accessInfos; //Specifies what stages and the type of access performed on a subResource

			std::variant<GraphicsCommands, ComputeCommands, TransferCommands> commands;
		};

		//Containers used for compiling and executing a graph of passes. Cleared after the compilation and initiating execution of the graph.
		std::vector<Pass> passes; //Loaded passes for execution. Will contain the actual passes and PassHandle is used to index through it.
		std::vector<std::vector<PassHandle>> adjacencyLists; //Contains adjacenies List for each Pass. 
		std::vector<PassHandle> orderedPasses; //Ordered Passes by Topological Sorting
		std::stack<PassHandle> DFS_orderStack; //Reserves visited nodes that need to wait until algorithm finished on it's adjacent nodes
		std::vector<bool> DFS_visited; //Index with PassHandle, indicates if a pass has been visited by the sort algorithm.
		std::vector<bool> DFS_onStack; //Index with PassHandle, indicates if a pass is on the DFS_orderStack
		std::vector<uint32_t> dependencyLevels; //Index with PassHandle

		//Containers2 for building up graph of passes
		std::vector<SubResourceDependencyInfo> subResourceDepInfos; //Represents all currently known written to subResources and their dependency information. Reset after every execution
		
		std::vector<QueueCommandList> queuePassList; //Each represents a Queue and the commands they run.

		//List of Transient Resource Creation Infos that are dependent on pass info.
		std::vector<std::pair<vDataResourceHandle, VkBufferCreateInfo>> transientBufferCreationQueue; //Transient Buffers that need to have their underlying resource created or given from pass executions
		std::vector<std::pair<vDataResourceHandle, VkImageCreateInfo>> transientImageCreationQueue; //Transient Images that need to have their underlying resource created or given from pass execution

		//Graph Accessable Resources and Relationships. Reset after every execution so that any transient resource handles and relations are removed but external are kept
		std::vector<Resource> dataResources; //Index with vDataResourceHandle
		std::vector<SubResource> dataSubResources; //Index with vDataSubResourceHandle

		//Underlying Vulkan Object Resources


		//Queue
		QueueHandle primaryQueue;
		QueueHandle aSyncQueue;
		QueueHandle transferDedicatedQueue;
		QueueHandle presentQueue;
		std::vector<QueueInfo> queues;

		void compile(); //Compile Pass Data
		void compileAdjacencyList();
		void compileOrderedPasses();
		void compileDependencyLevels();
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
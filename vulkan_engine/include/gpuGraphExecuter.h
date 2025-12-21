#pragma once
#include "renderGraph.h"

namespace gpu_graph {
	using ExecutionHandle = size_t; //Identifies a execution of a graph
	using PassHandle = size_t; //Identifies a unique pass within the executers list of passes.
	using ResourceHandle = size_t; //Represents handle to actual resource data

	struct GraphExecution {

	};

	//Plan to wrap vector to represent a vector of vectors to have more control over the memory and to allow clearing of the outer vector to "reset" without having to destruct inner vectors
	struct DirectedAdjacencyList {

	};

	class GPU_GraphExecuter {
	public:
		void loadPass();
		void attachQueue(QueueType type, VkQueue queue, uint32_t queueFamily);
	private:
		struct QueueInfo {
			VkQueue queue;
			uint32_t queueFamily;
		};

		//Containers used for compiling and executing a graph of passes. Cleared after the compilation and initiating execution of the graph.
		std::vector<Pass> passes; //Loaded passes for execution. 
		GraphResource graphResource; //Compiled resoruces for a graph execution.
		std::vector<std::vector<PassHandle>> directedAdjacencyList; //Contains adjacenies for each Pass. 
		std::vector<PassHandle> orderedPasses; //Ordered Passes by Dependency Level
		
		//Maybe make a class dedicated to handling resource allocation. So can also do stuff like reuse available handles.
		//Index with ResourceHandle, external data resources
		std::vector<BufferResource> externalBufferResources;
		std::vector<ImageResource> externalImageResources;

		//Index with ResourceHandle, represents actual transient resources tracked by executer and used to supply executiong graphs the resources they would need for their passes
		std::vector<BufferResource> bufferResource;
		std::vector<ImageResource> imageResource; 
		std::vector<ImageSubResource> imageSubResources;
		std::vector<BufferSubResource> bufferSubResources;
		std::vector<VkSampler> imageSamplers; 
		std::vector<VkDescriptorSet> descriptorSets;

		QueueInfo primaryQueue;
		QueueInfo asyncComputeQueue;
		QueueInfo transferQueue;
		QueueInfo presentQueue;

		void compile(); //Compile and Run Loaded Passes. 
		void compileAdjacencyList();
	};
}
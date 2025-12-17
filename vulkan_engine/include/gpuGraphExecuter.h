#pragma once
#include "renderGraph.h"

namespace gpu_graph {
	using PassHandle = size_t; //Identifies a unique pass within the executers list of passes.

	class GPU_GraphExecuter {
	public:
		void loadPass();
		ExecutionHandle execute(); //Compile and Run Loaded Passes. 
		void attachQueue(QueueType type, VkQueue queue, uint32_t queueFamily);
	private:
		struct QueueInfo {
			VkQueue queue;
			uint32_t queueFamily;
		};

		std::vector<Pass> passes; //Loaded passes for execution. Cleared after every execution

		QueueInfo primaryQueue;
		QueueInfo asyncComputeQueue;
		QueueInfo transferQueue;
		QueueInfo presentQueue;

		void generateAdjacencyList(const std::vector<std::vector<PassHandle>>& adjacencyList);
	};
}
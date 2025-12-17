#include "gpuGraphExecuter.h"

gpu_graph::ExecutionHandle gpu_graph::GPU_GraphExecuter::execute() {
	assert(passes.size() > 0 && "Graph Executer attempted to execute on an empty list of passes");

	std::vector<std::vector<PassHandle>> adjacencyList; //Contains adjacenies for each Pass
	generateAdjacencyList(adjacencyList);
}

void gpu_graph::GPU_GraphExecuter::generateAdjacencyList(const std::vector<std::vector<PassHandle>>& adjacencyList) {
	PassHandle i = 0;
	for (const Pass& pass : passes) {
		;
	}
}
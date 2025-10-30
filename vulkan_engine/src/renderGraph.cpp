#include "renderGraph.h"
#include <algorithm>
#include <stack>

RenderGraph RenderGraphBuilder::buildRenderGraph() {
	//If there are no passes, just return an empty Render Graph.
	if (_unorderedPasses.size() == 0) {
		return RenderGraph();
	}

	//Construct Graph's Adjacency List of Unordered Pass (Might want to use unordered map instead for storing adjacency lists, makes things easier and thus dont need to regenerate an adjacency list after sorting)
	//std::vector<std::vector<int>> passAdjacencyList = generateAdjacencyList(_unorderedPasses);
	PassAdjacencyMap passAdjacencies = generateAdjacencyList(_unorderedPasses);

	//Order Graph by Topoligcal Sort + Check for Circular Depedencies and other wrong constructions
	std::vector<Pass> orderedPasses = topologicalSort(passAdjacencies, _unorderedPasses);

	//Find and Assign Depedency Levels to each node, aka Node's longest Depth via "Longest Path Search"
	std::vector<std::vector<PassIndex>> dependencyLevels = generateDependencyLevels(passAdjacencies, orderedPasses);

	//Figure out Resource Aliasing for graph by gathering all transient resources used in the graph, using depedency levels as timelines for each, and then partitioning them into buckets, giving a list of offsets that show how memory should be partitioned

	//Generate SSIS (set of indices representing closest nodes/passes) for each pass to figure out all the dependencies between nodes.

	//Use the SSIS to cull indirect dependencies to reduce sync points

	//Reroute Transitions to a "main" queue (likely graphics) for resources that are used by multiples queues and can possibly cause conflict.
}

PassAdjacencyMap RenderGraphBuilder::generateAdjacencyList(const std::vector<Pass>& passes) {
	PassAdjacencyMap result;

	for (int i = 0; i < passes.size(); i++) {
		Pass currentPass = passes[i];

		for (int j = 0; j < passes.size(); j++) {
			Pass dependentPass = passes[j];

			bool isDependent = std::any_of(dependentPass.inputResources.begin(), dependentPass.inputResources.end(), [&currentPass](ResourceName dependentResource) { return currentPass.outputResources.contains(dependentResource)});

			//If target pass has an input Resource that is an output Resource of the current pass, then its adjacent/dependent to it.
			if (isDependent) {
				result[currentPass].push_back(j);
			}
		}
	}

	return result;
}

//Sorts a given list of unordered passes, and using the adjacency list, returns a topoligcaly sorted list of passes. Expected passes to have a size greater than 0.
std::vector<Pass> RenderGraphBuilder::topologicalSort(const PassAdjacencyMap& adjacencyList, const std::vector<Pass>& passes) {
	size_t passCount = passes.size();

	std::vector<Pass> result;
	std::vector<bool> visited(passCount, false); //Specfies if the pass in passes has been visited 
	std::vector<bool> onStack(passCount, false); //Specifies if the pass in passes is on the stack

	//DFS - Get the list of passes ordered by decreasing-depth
	std::stack<size_t> DFS_stack; //Keeps track the unwinding of connected components
	bool passListFilled = false; //Indicates if the resulting list of passes has been completely filled.

	DFS_stack.push(0);
	onStack[0] = true;
	while (!passListFilled) { //Will continue DFS until the resulting list is completely filled.
		for (!DFS_stack.empty()) { 
			size_t currentPassIndex = DFS_stack.top();
			visited[currentPassIndex] = true;

			bool hasVisitableDependents = false;

			//Goes through the current pass' dependents and check if they have been visited, if not, push to stack. If so and it is already on stack, then it is a circular dependency and the graph is invalid.
			for (size_t dependentPassIndex : adjacencyList.at(passes[currentPassIndex])) {
				if (!visited[dependentPassIndex]) { 
					hasVisitableDependents = true;

					DFS_stack.push(dependentPassIndex);
					onStack[dependentPassIndex];
				}
				else if (onStack[dependentPassIndex])
					throw std::exception("Built Render Graph contains circular dependency passes");
			}

			//If the current Pass has no more visitable dependents, then we can push it to results and pop it off the stack
			if (!hasVisitableDependents) {
				DFS_stack.pop();
				onStack[currentPassIndex] = false;
				result.push_back(passes[currentPassIndex]);
			}
		}

		//Verifies if all passes have been accounted for and the result is completely filled. If not, then the graph is disconnected, thus find the next unvisited pass then perform DFS on it.
		if (result.size() == passCount) {
			passListFilled = true;
		}

		else {
			for (int i = 0; i < visited.size(); i++) {
				if (!visited[i]) {
					DFS_stack.push(i);
					onStack[i] = true;
				}
			}
		}
	}

	//Reverse Order then return result
	std::reverse(result.begin(), result.end());

	return result;
}

//Generate The List of Dependency Levels, where each level constains the list of PassIndices indicating which Dependency the Pass exists
std::vector<std::vector<PassIndex>> RenderGraphBuilder::generateDependencyLevels(const PassAdjacencyMap& adjacencyList, const std::vector<Pass>& passes) {
	std::vector<std::vector<PassIndex>> result;
	std::vector<size_t> depth(passes.size(), 0); //Tracks the depth of each pass, which would directly correlate to the dependency level of the pass

	for (PassIndex i = 0; i < passes.size(); i++) {
		for (PassIndex adjacentPassIndex : adjacencyList.at(passes[i])) {
			if (depth[adjacentPassIndex] < depth[i] + 1)
				depth[adjacentPassIndex] = depth[i] + 1;
		}
	}

	for (PassIndex i = 0; i < depth.size(); i++) {
		size_t dependencyLevel = depth[i];

		if (dependencyLevel >= result.size()) //Ensures that the dependencyLevel index exists in the vector. If not, then resize the vector before adding the pass.
			result.resize(dependencyLevel + 1);
		result[dependencyLevel].push_back(i);
	}

	return result;
}

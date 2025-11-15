#include "renderGraph.h"
#include <algorithm>
#include <stack>

using namespace render_graph;

RenderGraph RenderGraphBuilder::buildRenderGraph() {
	RenderGraph renderGraph;

	//If there are no passes, just return an empty Render Graph.
	if (_unorderedPasses.size() == 0) {
		return renderGraph;
	}

	//Construct Graph's Adjacency List of Unordered Pass (NOt sure if should has it map to the indices of the Pass or pointers/references refering to the Pass
	//std::vector<std::vector<int>> passAdjacencyList = generateAdjacencyList(_unorderedPasses);
	renderGraph.passAdjacencies = generateAdjacencyList(_unorderedPasses);

	//Order Graph by Topoligcal Sort + Check for Circular Depedencies and other wrong constructions
	renderGraph.passes = topologicalSort(renderGraph.passAdjacencies, _unorderedPasses);

	//Find and Assign Depedency Levels to each node, aka Node's longest Depth via "Longest Path Search". Gives info on what nodes are independent from each other (and can run concurrently)
	renderGraph.dependencyLevels = generateDependencyLevels(renderGraph.passAdjacencies, renderGraph.passes); 

	//Pass down Pass Function Codes
	renderGraph.passCmdCodes = _passCmdCodes;

	//Figure out Resource Aliasing for graph by gathering all transient resources used in the graph, using depedency levels as timelines for each, and then separating them into different aliasable regions
	renderGraph.transientMemoryAllocInfos = generateTransientResourceAliasingInfo(renderGraph.dependencyLevels, _transientResourceInfos, renderGraph.passes);

	//Pass down ExternalResoucesInfo
	renderGraph.externalResourceInfos = _externalResourceInfos;

	//Generate SSIS (set of indices representing closest nodes/passes) for each pass to figure out all the inter-dependencies between nodes.
	//Input: Need How many Queues, which queue a node is associated with, Adjacency List, The list of passes.

	//Use the SSIS to cull indirect dependencies to reduce sync points
}

PassAdjacencyMap RenderGraphBuilder::generateAdjacencyList(const std::vector<Pass>& passes) {
	PassAdjacencyMap result;

	for (int i = 0; i < passes.size(); i++) {
		Pass currentPass = passes[i];

		for (int j = 0; j < passes.size(); j++) {
			Pass dependentPass = passes[j];

			bool isDependent = std::any_of(dependentPass.inputResources.begin(), dependentPass.inputResources.end(), [&currentPass](ResourceName dependentResource) { return currentPass.outputResources.contains(dependentResource); });

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
		while (!DFS_stack.empty()) { 
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

	//Find depth of all pases
	for (PassIndex i = 0; i < passes.size(); i++) {
		for (PassIndex adjacentPassIndex : adjacencyList.at(passes[i])) {
			if (depth[adjacentPassIndex] < depth[i] + 1)
				depth[adjacentPassIndex] = depth[i] + 1;
		}
	}

	//Assign each dependencyLevel the the PassIndex
	for (PassIndex i = 0; i < depth.size(); i++) {
		size_t dependencyLevel = depth[i];

		if (dependencyLevel >= result.size()) //Ensures that the dependencyLevel index exists in the vector. If not, then resize the vector before adding the pass.
			result.resize(dependencyLevel + 1);
		result[dependencyLevel].push_back(i);
	}

	return result;
}

std::vector<TransientMemoryAliasableRegion> RenderGraphBuilder::generateTransientResourceAliasingInfo(const std::vector<std::vector<PassIndex>>& dependencyLevels, const std::unordered_map<ResourceName, transResourceInfoStruct>& transientResourceInfos, const std::vector<Pass>& passes) {
	struct ResourceEffectiveLifetime {
		ResourceName name; //The refered Resource
		size_t begin; //Dependency Level where resource is first used
		size_t end; //Dependency Level where resource is last used
	};

	//Generate list of EffectiveLifetimes for each Resource
	std::vector<ResourceEffectiveLifetime> resourceLifeTimes;
	std::unordered_map<ResourceName, size_t> resourceNameToLifeTimeIndex; //Maps Resource Name to Index of its EffectiveTimeline in the lifetime list.

	for (size_t level = 0; level < dependencyLevels.size(); level++) {
		for (PassIndex passIndex : dependencyLevels[level]) {
			const Pass& pass = passes[passIndex];

			//Resources designated as input represent a possible end of its lifetime. 
			for (ResourceName resourceName : pass.inputResources) {
				//Check to make sure Resource is Transient (and existing in the resourceName to Index Map)
				if (resourceNameToLifeTimeIndex.contains(resourceName)) {
					size_t i = resourceNameToLifeTimeIndex[resourceName];
					resourceLifeTimes[i].end = level;
				}
			}

			//Resource designated as output represent as a start of it's lifetime
			for (ResourceName resourceName : pass.outputResources) {
				//Check to make sure Resource is Transient
				if (transientResourceInfos.contains(resourceName)) {
					resourceLifeTimes.push_back({ .name = resourceName, .begin = level, .end = level });
					resourceNameToLifeTimeIndex[resourceName] = resourceLifeTimes.size() - 1;
				}
			}
		}
	}

	//Sort list by memory size in descending order
	std::sort(resourceLifeTimes.begin(), resourceLifeTimes.end(), [&transientResourceInfos](ResourceEffectiveLifetime a, ResourceEffectiveLifetime b) {
		return transientResourceInfos[a.name].sizeWhatever > transientResourceInfos[b.name].sizeWhatever;
		});

	//Generate Transient Aliasable Memory Regions
	std::vector<TransientMemoryAliasableRegion> result;

	//-Keep Generating Memory Regions unitl there are no more resource lifetimes left.
	while (resourceLifeTimes.size() != 0) {
		//Create new region with size matching that with the current largest existing resource in list of resource lifetimes.
		result.emplace_back();
		TransientMemoryAliasableRegion& currentRegion = result.back();
		
		currentRegion.size = transientResourceInfos[resourceLifeTimes.front().name].calcualteSizeWhatever;

		//Add all possible Resources to the Region, removing those that have been added from the lifetimes list.
		std::unordered_map<ResourceName, ResourceEffectiveLifetime> addedResourceLifetimes; //Holds all the lifetime of resources that can occupy the aliasable region.
		resourceLifeTimes.erase(std::remove_if(resourceLifeTimes.begin(), resourceLifeTimes.end(), [&currentRegion, &addedResourceLifetimes, &transientResourceInfos](ResourceEffectiveLifetime resourceLifetime) {
			enum class PointType
			{
				Start,
				End
			};
			
			//Represents a offset in memory representing where a region of memory starts or ends
			struct OffsetPoint {

				PointType type;
				size_t offset; //Offset in Memory
			};

			const transResourceInfo& resourceInfo = transientResourceInfos[resourceLifetime.name];
			size_t resourceSize = resourceInfo.CalcualteSizeWhatever;
			std::vector<OffsetPoint> unavailableSubregionOffsetPoints; //List of offset points representing start and end points of unavailable subregions of memory in the current Region.

			//Add Offsetpoints representing the start and end of the region of memory that is available.
			unavailableSubregionOffsetPoints.emplace_back(PointType::End, 0); 
			unavailableSubregionOffsetPoints.emplace_back(PointType::Start, currentRegion.size);

			//Generate offsetPoints representing unavailable subregions of memory based on any existing aliased resources aliasing from this region and if they conflict with their lifetimes
			for (auto aliasedResource : currentRegion.transResources) {
				ResourceEffectiveLifetime& aliasedResourceLifetime = addedResourceLifetimes[aliasedResource.resourceName];
				
				if (resourceLifetime.begin <= aliasedResourceLifetime.end && resourceLifetime.end >= aliasedResourceLifetime.begin) {
					unavailableSubregionOffsetPoints.emplace_back(PointType::Start, aliasedResource.offset);
					unavailableSubregionOffsetPoints.emplace_back(PointType::End, aliasedResource.offset + aliasedResource.size);
				}
			}

			//Sort OffsetPoints so that offsets are ordered in ascending offsets.
			std::sort(unavailableSubregionOffsetPoints.begin(), unavailableSubregionOffsetPoints.end(), [](OffsetPoint a, OffsetPoint b) {
				if (a.offset == b.offset) { //If offsets match. Then must guarentee that all end points with the same offset must come before all start points with the same offset, as any end point with matching offset as a start point and that comes after it represents an unavailable subregion of size 0, which is invalid.
					if (a.type == PointType::End)
						return true;
					else
						return false;
				}
				else if (a.offset < b.offset)
					return true;
				else
					return false;
				});

			//Find the smallest available space without lifetime conflicts by iterating through the offsetpoints (If there is one)
			uint32_t overlapCounter = 0;
			size_t smallestAvailableSubregionOffset = currentRegion.size; //Keep track of the smallest available subregion that can fit the current resource that we know so far. If none, then offset matches the region's size;
			size_t smallestAvailableSubreionSize = SIZE_MAX;
			size_t currentEndPointOffset; //Represents the current EndPoint to keep track for looking for available subregions
			for (OffsetPoint point : unavailableSubregionOffsetPoints) {
				if (point.type == PointType::End) {
					currentEndPointOffset = point.offset;

					if (overlapCounter > 0)
						overlapCounter--;
				}
				else {
					if (overlapCounter == 0) { //If Counter is 0, then the End, Start Pair is not being overlapped by other End,Start Pair. Thus it is an available subregion
						size_t availableSubregionSize = point.offset - currentEndPointOffset;

						if (availableSubregionSize <= smallestAvailableSubreionSize && availableSubregionSize >= resourceSize) { //If the availabe subregion is smaller than what we know as smallest subregion at the time and big enough to hold the resource, designate as smallest.
							smallestAvailableSubregionOffset = currentEndPointOffset;
							smallestAvailableSubreionSize = availableSubregionSize;
						}

						overlapCounter++;
					}
				}
			}

			//Check if we found an available subregion of space, if so, then add the resource to the region with an offset matching that available subregion's offset and return true to remove from lifetime list.
			if (smallestAvailableSubregionOffset != currentRegion.size) {
				currentRegion.transResources.push_back({ .offset = smallestAvailableSubregionOffset, .size = resourceSize, .resourceInfo = resourceInfo });
				return true;
			}
			else
				return false;
			}), resourceLifeTimes.end());
	}

	return result;
}

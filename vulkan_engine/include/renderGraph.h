#pragma once

#include <string>
#include <unordered_set>
#include <functional>

struct RenderGraph;
struct Pass;

using ResourceName = std::string; //Name/ID of a Resource
using PassIndex = size_t; //Index of Pass
using PassAdjacencyMap = std::unordered_map<Pass, std::vector<PassIndex>, Pass::Hasher>; //Maps a Pass to a list of indices of dependent Passes in the RenderGraph's list of passes

/*
	Will be used to construct a RenderGraph. Declare what Passes exist and what Resources will exist/needed for the Graph. Should also declare what queues are available for graph to use
*/
class RenderGraphBuilder {
public:
	void addPass(passParamsStruct*, size_t numPasses);

	RenderGraph buildRenderGraph();

private:
	std::vector<Pass> _unorderedPasses;

	//Functions to modulize and breakdown the steps of graph generation...
	PassAdjacencyMap generateAdjacencyList(const std::vector<Pass>& passes);
	std::vector<Pass> topologicalSort(const PassAdjacencyMap& adjacencyList, const std::vector<Pass>& passes);
	std::vector<std::vector<PassIndex>> generateDependencyLevels(const PassAdjacencyMap& adjacencyList, const std::vector<Pass>& passes);
};

/*
	Represents a graph of passes that make up one whole render. Can be used to execute one whole frame, and manages transient (possibly aliased) resources, and manages state (if readable/writable) of persistent
	resources that exist outside the Graph, ie Swapchain's RenderTargers

*/
struct RenderGraph {
	std::vector<std::vector<Pass>> passes; //Not sure if should hold Adjacency List or justa list
};

/*
	Represents a Render/Compute Pass.
*/
struct Pass {
	std::string name;
	std::unordered_set<ResourceName> inputResources;
	std::unordered_set<ResourceName> outputResources;

	std::function<void()> passCode;

	bool operator==(const Pass& other) const {
		return name == other.name && inputResources == other.inputResources && outputResources == other.outputResources; //Not sure if comparing function pointers would work for all circumstances. So for now just compares other members except the function code
	}

	struct Hasher {
		size_t operator()(const Pass& pass) const {
			size_t hashValue = std::hash<std::string>{}(pass.name);
			for (const auto& resource : pass.inputResources) {
				hashValue ^= std::hash<std::string>{}(resource);
			}
			for (const auto& resource : pass.outputResources) {
				hashValue ^= std::hash<std::string>{}(resource);
			}
			return hashValue;
		}
	};
};

/*
	Not sure if resource should be struct or some kind of string. And how to connect the elusive Resource to what they actually represent (the object/data).
	Types: RenderTarget, DepthStencil, Texture, Buffer.

	Perhaps have Resource represent a string, then have either the rendergraph or a class that executes the rendergraph to use that string to connect it the actual resource (via unordered map)
*/
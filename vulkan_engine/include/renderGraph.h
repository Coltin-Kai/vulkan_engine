#pragma once

#include <string>
#include <unordered_set>
#include <functional>

#include "vulkan_helper_types.h"

namespace render_graph {

	struct RenderGraph;
	struct Pass;

	using ResourceName = std::string; //Name/ID of a Resource
	using PassIndex = size_t; //Index of Pass
	using PassAdjacencyMap = std::unordered_map<Pass, std::vector<PassIndex>, Pass::Hasher>; //Maps a Pass to a list of indices of dependent Passes in the RenderGraph's list of passes
	using ResourceBufferRef = std::unordered_map<ResourceName, VkBuffer>;
	using ResourceImageRef = std::unordered_map<ResourceName, VkImage>;

	/*
		Will be used to construct a RenderGraph. Declare what Passes exist and what Resources will exist/needed for the Graph. Should also declare what queues are available for graph to use
	*/
	class RenderGraphBuilder {
	public:
		//Add Resource Functions are used to 
		void addResource(transResourceBufferInfoStruct); //Adds info for Buffer construction of a Transient Buffer
		void addResource(transResourceImageInfoStruct); //Add info for Image construction of a Transient Image
		void addResource(externResourceBufferInfoStruct); //Adds state info of an existing Buffer
		void addResource(externResourceImageInfoStruct); //Adds state info of an existing Image

		void addPass(std::string passName, passFlags, std::vector<ResourceName> inputResources, std::vector<ResourceName> outputResources, std::function<void(const ResourceBufferRef&, const ResourceImageRef&)> passCode); //Maybe should pass a struct of params since need to do stuff like indicate if render/compute pass and other info about pass as well
		RenderGraph buildRenderGraph();
	private:
		std::vector<Pass> _unorderedPasses;
		std::vector<transResourceBufferInfoStruct> _transientResourceBufferInfos;
		std::vector<transResourceImageInfoStruct> _transientResourceImageInfos;
		std::vector<externResourceBufferInfoStruct> _externalResourceBufferInfos;
		std::vector<externResourceImageInfoStruct> _externalResourceImageInfos;

		//Functions to modulize and breakdown the steps of graph generation...
		PassAdjacencyMap generateAdjacencyList(const std::vector<Pass>& passes);
		std::vector<Pass> topologicalSort(const PassAdjacencyMap& adjacencyList, const std::vector<Pass>& passes);
		std::vector<std::vector<PassIndex>> generateDependencyLevels(const PassAdjacencyMap& adjacencyList, const std::vector<Pass>& passes);
	};

	/*
		Represents a graph of passes that make up one whole render. Can be used to execute one whole frame, and manages transient (possibly aliased) resources, and manages state (if readable/writable) of externel
		resources that exist outside the Graph, ie Swapchain's RenderTargers

	*/
	struct RenderGraph {
		//Pass and Pass-Dependency Info
		std::vector<Pass> passes; //List of Passes of the RenderGraph (Topoligcally Sorted). Will represent ownership of the pass and should be the structure used to access a pass
		PassAdjacencyMap passAdjacencies; //Maps Passes to a list of their directed adjacents (AKA the indices of Passes Dependent on it)
		std::vector<std::vector<PassIndex>> dependencyLevels; //Represents all Dependency Levels of the RenderGraph and what passes (as indices) exists at each level, where passes on the same level are independent from each other and can run concurrently. (Maybe can be a vector of unordered sets of PassIndices instead?)
		
		//Resource Info
		std::vector<transResourceBufferInfoStruct> _transientResourceBufferInfos;
		std::vector<transResourceImageInfoStruct> _transientResourceImageInfos;
		std::vector<externResourceBufferInfoStruct> _externalResourceBufferInfos;
		std::vector<externResourceImageInfoStruct> _externalResourceImageInfos;
	};

	/*
		Represents a Render/Compute Pass.
	*/
	struct Pass {
		std::string name;
		std::unordered_set<ResourceName> inputResources;
		std::unordered_set<ResourceName> outputResources;

		std::function<void(const ResourceBufferRef&, const ResourceImageRef&)> passCode; //Captures the Input and Output Resource Names (Likely variables holding the names) it needs to perform its pass code. And is passed in its parameters the maps that point to the resources it needs (Using ResourceNames to access the resource itslef)

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
}
/*
	Not sure if resource should be struct or some kind of string. And how to connect the elusive Resource to what they actually represent (the object/data).
	Types: RenderTarget, DepthStencil, Texture, Buffer.

	Perhaps have Resource represent a string, then have either the rendergraph or a class that executes the rendergraph to use that string to connect it the actual resource (via unordered map)
*/
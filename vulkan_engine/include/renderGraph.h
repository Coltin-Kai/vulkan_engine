#pragma once

#include <string>
#include <unordered_set>
#include <functional>

#include "vulkan_helper_types.h"

namespace render_graph {

	struct RenderGraph;
	struct Pass;
	struct ResourceDependencyInfo;
	struct TransientMemoryAliasableRegion;

	using ResourceName = std::string; //Name/ID of a Resource
	using PassIndex = size_t; //Index of a Pass
	using PassAdjacencyMap = std::unordered_map<Pass, std::vector<PassIndex>, Pass::Hasher>; //Maps a Pass to a list of indices of dependent Passes in the RenderGraph's list of passes
	using ResourceBufferRef = std::unordered_map<ResourceName, VkBuffer>;
	using ResourceImageRef = std::unordered_map<ResourceName, VkImage>;
	using PassCommandCode = std::function<void(const ResourceBufferRef&, const ResourceImageRef&)>;

	/*
		Will be used to construct a RenderGraph. Declare what Passes exist and what Resources will exist/needed for the Graph. Should also take in what queues each pass uses
	*/
	class RenderGraphBuilder {
	public:
		void addResource(transResourceInfoStruct); //Adds info for Buffer/Image construction of a Transient Resource
		void addResource(externResourceInfoStruct); //Adds info fo referencing a Buffer/Image as an External Resource

		void addPass(passInfoStruct); //Maybe should pass a struct of params since need to do stuff like indicate if render/compute pass and other info about pass as well
		RenderGraph buildRenderGraph();
	private:
		std::vector<Pass> _unorderedPasses;
		std::unordered_map<std::string, PassCommandCode> _passCmdCodes; //Map Function Name to Function Code
		std::unordered_map<ResourceName, transResourceInfoStruct> _transientResourceInfos; //Infos for creating Transient Resources. In map so it's easier to access for constructing aliasing memory regions.
		std::vector<externResourceInfoStruct> _externalResourceInfos; //Infos for referencing External Resources

		//Functions to modulize and breakdown the steps of graph generation...
		PassAdjacencyMap generateAdjacencyList(const std::vector<Pass>& passes);
		std::vector<Pass> topologicalSort(const PassAdjacencyMap& adjacencyList, const std::vector<Pass>& passes);
		std::vector<std::vector<PassIndex>> generateDependencyLevels(const PassAdjacencyMap& adjacencyList, const std::vector<Pass>& passes);
		std::vector<TransientMemoryAliasableRegion> generateTransientResourceAliasingInfo(const std::vector<std::vector<PassIndex>>& dependencyLevels, const std::unordered_map<ResourceName, transResourceInfoStruct>& transientResourceInfos, const std::vector<Pass>& passes);
	};

	/*
		Represents a graph of passes that make up one whole render. Can be used to execute one whole frame, and manages transient (possibly aliased) resources, and manages state (if readable/writable) of externel
		resources that exist outside the Graph, ie Swapchain's RenderTargets.
		Note: Primary limitations to the Graph is that there can be no Circular Dependencies/Paths and no multiple write dependencies to the same resource (No two or more passes can output to the same resource). Though latter could be accounted for to decrease memory bandwidth.

	*/
	struct RenderGraph {
		//Pass and Pass-Dependency Info
		std::vector<Pass> passes; //List of Passes of the RenderGraph (Topoligcally Sorted). Will represent ownership of the pass and should be the structure used to access a pass
		PassAdjacencyMap passAdjacencies; //Maps Passes to a list of their directed adjacents (AKA the indices of Passes Dependent on it)
		std::vector<std::vector<PassIndex>> dependencyLevels; //Represents all Dependency Levels of the RenderGraph and what passes (as indices) exists at each level, where passes on the same level are independent from each other and can run concurrently. (Maybe can be a vector of unordered sets of PassIndices instead?)
		
		//Maps Function Names/ID contained in Passes to the actual executable function
		std::unordered_map<std::string, PassCommandCode> passCmdCodes;

		//Resource Info
		std::vector<TransientMemoryAliasableRegion> transientMemoryAllocInfos; //Contains Info on the Allocations needed for created Transient Resources to use (And the Transient Resource Creation info itself)
		std::vector<externResourceInfoStruct> externalResourceInfos; //External Resources Infos

		//Queue
		std::vector<QueueInfo> queueInfos;
	};

	/*
		Represents a Render/Compute Pass.
	*/
	struct Pass {
		std::string name;
		std::unordered_set<ResourceName> inputResources; 
		std::unordered_set<ResourceName> outputResources;
		std::unordered_map<ResourceName, ResourceDependencyInfo> resourceDepInfos;
		std::string passCommandCodeName; //Referenced Function Captures the Input and Output Resource Names (Likely variables holding the names) it needs to perform its command code. And is passed in its parameters the maps that point to the resources it needs (Using ResourceNames to access the resource itslef)
		size_t queueID; //References the Queue the Pass Command Code submits to.
		
		bool operator==(const Pass& other) const {
			return name == other.name && inputResources == other.inputResources && outputResources == other.outputResources 
				&& resourceDepInfos == other.resourceDepInfos && passCommandCodeName == other.passCommandCodeName && queueID == other.queueID;
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

				for (const auto& resourceName_DepInfo : pass.resourceDepInfos) {
					hashValue ^= std::hash<std::string>{}(resourceName_DepInfo.first) ^ ResourceDependencyInfo::Hasher{}(resourceName_DepInfo.second);
				}

				hashValue ^= std::hash<std::string>{}(pass.passCommandCodeName);
				hashValue ^= std::hash<size_t>{}(pass.queueID);

				return hashValue;
			}
		};
	};

	/*
		Specifies how the resource will be used in a pass: What Pipeline Stages it participates in the Pass, What kind of Access the Pass will perform on resource, (And if an image) the expected layout and the subresource Range into.
		Used for setting up the appropriate VkDependencyInfos and its barriers that syncronize between passes
	*/
	struct ResourceDependencyInfo {
		VkPipelineStageFlags2 pipelineStages = VK_PIPELINE_STAGE_NONE; 
		VkAccessFlags2 accessType = VK_ACCESS_NONE;
		VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageSubresourceRange imageRange = { .aspectMask = VK_IMAGE_ASPECT_NONE, .baseMipLevel = 0, .levelCount = 0, .baseArrayLayer = 0, .layerCount = 0 };

		bool operator==(const ResourceDependencyInfo& other) const {
			return pipelineStages == other.pipelineStages && accessType == other.accessType && imageLayout == other.imageLayout
				&& imageRange.aspectMask == other.imageRange.aspectMask && imageRange.baseArrayLayer == other.imageRange.baseArrayLayer
				&& imageRange.baseMipLevel == other.imageRange.baseMipLevel && imageRange.layerCount == other.imageRange.layerCount
				&& imageRange.levelCount == other.imageRange.levelCount;
		}
	
		struct Hasher {
			size_t operator()(const ResourceDependencyInfo& resDepInfo) const {
				size_t hashValue = std::hash<uint64_t>{}(resDepInfo.pipelineStages);
				hashValue ^= std::hash<uint64_t>{}(resDepInfo.accessType);
				hashValue ^= std::hash<size_t>{}(resDepInfo.imageLayout);
				hashValue ^= std::hash<uint32_t>{}(resDepInfo.imageRange.aspectMask) ^ std::hash<uint32_t>{}(resDepInfo.imageRange.baseMipLevel)
					^ std::hash<uint32_t>{}(resDepInfo.imageRange.levelCount) ^ std::hash<uint32_t>{}(resDepInfo.imageRange.baseArrayLayer) 
					^ std::hash<uint32_t>{}(resDepInfo.imageRange.layerCount);
				return hashValue;
			}
		};
	};

	/*
		Represents One Allocation of a region of memory and the list of resources that will alias from it.
	*/
	struct TransientMemoryAliasableRegion {
		struct AliasingResource {
			size_t offset; //Offset into the region of memory it's allocating from
			size_t size; //Overall Memory size of Resource
			transResourceInfoStruct resourceInfo;
		};

		size_t size = 0; //Overall size of the allocation of memory
		std::vector<AliasingResource> transResources;
	};
}

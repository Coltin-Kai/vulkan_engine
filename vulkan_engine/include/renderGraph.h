#pragma once

#include <string>
#include <unordered_set>
#include <functional>
#include <variant>

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
		void addQueue(QueueInfo);

		void addPass(passInfoStruct); //Maybe should pass a struct of params since need to do stuff like indicate if render/compute pass and other info about pass as well
		RenderGraph buildRenderGraph();
	private:
		std::vector<Pass> _unorderedPasses;
		std::unordered_map<std::string, PassCommandCode> _passCmdCodes; //Map Function Name to Function Code
		std::unordered_map<ResourceName, transResourceInfoStruct> _transientResourceInfos; //Infos for creating Transient Resources. In map so it's easier to access for constructing aliasing memory regions.
		std::vector<externResourceInfoStruct> _externalResourceInfos; //Infos for referencing External Resources
		std::vector<QueueInfo> _queueInfos;

		//Functions to modulize and breakdown the steps of graph generation...
		PassAdjacencyMap generateAdjacencyList(const std::vector<Pass>& passes);
		std::vector<Pass> topologicalSort(const PassAdjacencyMap& adjacencyList, const std::vector<Pass>& passes);
		std::vector<std::vector<PassIndex>> generateDependencyLevels(const PassAdjacencyMap& adjacencyList, const std::vector<Pass>& passes);
		std::vector<TransientMemoryAliasableRegion> generateTransientResourceAliasingInfo(const std::vector<std::vector<PassIndex>>& dependencyLevels, const std::unordered_map<ResourceName, transResourceInfoStruct>& transientResourceInfos, const std::vector<Pass>& passes);
		std::unordered_map<Pass, std::vector<PassIndex>> generateSyncronizationIndexSet(size_t queueCount);
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
		std::unordered_map<Pass, std::vector<PassIndex>> syncronizationIndexSet; //Represents the list of Passes (as Indices) the referencing Pass needs to syncronize with within each Queue (Index of the list associates with respective Queue)

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

namespace gpu_graph {
	using ExecutionHandle = size_t;

	using vDataResourceHandle = size_t; //Can represent either a virutal buffer or image resource handle
	using vDataSubResourceHandle = size_t; //Can represents either a virtual buffer or image subresource handle
	using vBufferResourceHandle = size_t;
	using vImageResourceHandle = size_t;
	using vBufferSubResourceHandle = size_t;

	using vImageSubResourceHandle = size_t;
	using vSamplerResourceHandle = size_t; //Virtual handle to Samplers
	using vDescriptorSetResourceHandle = size_t; //Virtual handle to DescriptorSets

	struct ImageResource {
		VkImage image;
	};

	struct BufferResource {
		VkBuffer buffer; //Represents the Buffer the Resource points to
		VkDeviceAddress deviceAddress; //0 indicates no available address. Buffer must have been created to support Buffer Device Addresses to use.
	};

	//Represents an Image Subresource
	struct ImageSubResource {
		vImageResourceHandle imageResource;
		VkImageView imageView;
		VkImageSubresourceRange range;
		VkOffset3D offset;
		VkExtent3D extent;
	};

	//Represents a Buffer Subresource
	struct BufferSubResource {
		vBufferResourceHandle bufferResource;
		VkDeviceSize offset;
		VkDeviceSize range;
	};

	//Represents all the resources of an executing graph that each pass of it can use to access resources using the virtual handles in their setup and execution code
	class GraphResource {
		std::vector<ImageResource> imageResources;
		std::vector<BufferResource> bufferResources;
		std::vector<ImageSubResource> imageSubResources;
		std::vector<BufferSubResource> bufferSubResources;
		std::vector<VkSampler> samplers;
		std::vector<VkDescriptorSet> descriptorSets;
	public:
		ImageResource getImage(vDataResourceHandle handle) {
			return imageResources[handle];
		}

		BufferResource getBuffer(vDataResourceHandle handle) {
			return bufferResources[handle];
		}

		ImageSubResource getImageSubResource(vDataSubResourceHandle handle) {
			return imageSubResources[handle];
		}

		BufferSubResource getBufferSubResource(vDataSubResourceHandle handle) {
			return bufferSubResources[handle];
		}

		VkSampler getSampler(vSamplerResourceHandle handle) {
			return samplers[handle];
		}

		VkDescriptorSet getDescriptorSet(vDescriptorSetResourceHandle handle) {
			return descriptorSets[handle];
		}
	};

	//Dependency info describing a Buffer or Image Resource and all subResources under it
	struct DataResourceDependency {
		vDataResourceHandle resource;
		std::vector<vDataSubResourceHandle> subResources;

		//Checks if two Dependencies refer to the same resource
		bool operator==(const DataResourceDependency& other) {
			return this->resource == other.resource;
		}

		struct Hasher {
			size_t operator()(const DataResourceDependency& dep) {
				return std::hash<vDataResourceHandle>{}(dep.resource);
			}
		};
	};

	enum class QueueType {
		Primary,
		AsyncCompute,
		TransferDedicated,
		PresentDedicated
	};

	//Represents a distinct GPU Operation performed on a read and write Targets
	struct Pass {
		std::string name;
		QueueType passType; //Specifies which queue the pass should be executed on

		std::unordered_set<DataResourceDependency, DataResourceDependency::Hasher> readBufferTargets;
		std::unordered_set<DataResourceDependency, DataResourceDependency::Hasher> readImageTargets;

		std::vector<DataResourceDependency> writeBufferTargets;
		std::vector<DataResourceDependency> writeImageTargets;

		std::function<void(GraphResource)> passSetup;
		std::function<void(GraphResource)> passExecution; 
	};
}

/*
How it would work:
During run time, create pass to represent one indivuidual GPU task.
Can wrap these pass creation in functions for reusability of lambdas while also letting passes represent distinct executions (ex Upload Data from Staging Buffer to another Buffer)
Helps reduce the number of anonymous classes and allows reusability of these implicit classes.
So can pass these functions with handles to the desired resourcs. And the user attaches the actual resource/creation data to these resources handles, letting passes being able to know which resources to point to.
So basically a function scope defines and attaches all the resources it understand it needs to supply its one or more passes it creates, and may supply those down the chain of functions that also do the same.
Aka a scope represents a group of passes. 

Executer has to do following:
Allocate and Generate TransResources
Sort and Order Passes
Perform setupcode of passes
Execute commandcode of passes in order

Resources.
Allow Resources and Subresources. There is a directed line from Pass 1 to Pass 2 if Pass 2 reads from a subresource that intersects with a subresource written to in Pass 1 from the same Resource.
If a pass may requre usage of the actual buffer/image itself. Would count that as a subresource covering the entire whole.
For Buffers, a subresource is basically a subregion of the buffer: offset and range vkdevicesizes.
For Images, its complicated:
	There are layers, levels, aspect, and the more granular image regions describing texels.
	There are three main structs for describing image subresource except for regions: vkimagesubresource, vkimagesubresourcelayers, and vkimagesubresourcerange; where first just describes a singer array layer
	and mip level, multiple layers and a single mip level, and multiple layers and levels. Easy to find and evaluate intersection.
	Operations involving shaders and the pipeliens would usualy take the whole image regions. So only really care about the subresources defining aspect, layers, and miplevels.
	Texel level operations like those in transfer ones would require specifying the regions as well. And barriers
	Since Imageviews represents an actual vkobject as well than jsut being a subresource, can specify and attach imageview creation info to subresources structs if we know we need to use them.
	THe choice of allowing external subresources like imageviews kind of makes it difficult of giving flexibility to functions to generate necessary subresources on their own. So subresource creation is fully relegated compilation

Command Code should be able to access these subresources to both access the the resource the subresource points to and data pertaining to that subresource (Like ranges and imageview)

Some resources will be likely shared but also contextual to what a pass does. For example a pass may need a sampler to use alongside an imageview. But since samplers are just metadata not tied to a resource,
it is sharable across the entire graph. Need a way to allow passes to declare what kind of sampler they would use, while resolving the correct pointer to resource during pass setup during compilation (as during compilation is when we know how much we need)

Also applies to descriptor sets. As they can be sharable objects, but used in both setup (updating descriptor set) and commands (binding descriptor set) for each pass object, with former also requiring accessing resources on demand.
Descriptor sets require knowledge of the subresources they use and how it will be attached to fit the descriptor layout of pipeline computation of a pass. So the scope describing and setingup the pass
should be the one to declare how its descriptor set (if any) should be. But since descriptor sets are sharable, need to resolve to allow handle to point to sharedresource.
Can also reuse descriptor sets from previous executions. As long as we aware if the execution is finished and resources are released. Then can either reuse the descriptor set or dont even have to udpate it 
if resources are same. 

For a lot of sharable resources, can use an associative container between creation info of resource and resource handle in execution struct, to allow fast check if past execution utilized such and such resource.

Push Constants, data is simply passed and loaded via commands so as simple as capturing whatever data the pass object needs in execution code.

BDA, easy to do with external resources, just make sure to pass the address data through appropriate external buffers or push constant datas. 
For transient resources, need to either copy addresses to external buffer in setup code or push addresses through pus constants in command code. Since multiple passes can use the address, acquiring the
address should be done by executer after creating resource if flagged to for that specific resource buffer. 
Transient resource buffers flagged for BDA should be allocated as their own VkBuffer. As Executer normally would probably pass a buffer that represents multiple virtual buffers if not done so.
Though can leverage buffer subresoruce offset and range info to pass it to shaders to appropriately pointer and bound the correct address of data in the BDA buffer. Though shader has to be setup for it.

Ordering. Simply order the passes based on their targets via BFS.
Memory Aliasing. Knowledge of the pass and its subpasses can help. For example, if we can tell if a pass utilizes no trans resources, it has no affect on if we can reuse resources or not so its not considered.

If a pass that uses trans resources may only be used only used sparingly like a conditional that only runs one frame that its needed but unused otherwise till then, it would be better separate their trans 
resources from more persistent passes' trans resources, so we dont have to reallocate persistent passes.

Passes interface with Virtual Resource Handles. Executer connect these handles to the actual resources it manages.
*/
#pragma once

#include <string>
#include <unordered_set>
#include <functional>
#include <variant>

#include "vulkan_helper_types.h"
#include <optional>

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
	using SubResourceReadTargetGroupHandle = size_t;
	using vPipelineHandle = size_t;

	using vDataResourceHandle = size_t; //Can represent either a virutal buffer or image resource handle
	using vDataSubResourceHandle = size_t; //Can represents either a virtual buffer or image subresource handle

	//Use State Handles to allow sharing states between commands within and across passes
	using vDescriptorSetStateHandle = size_t;

	//Contains the indices to their respective descriptor for a transient resource
	struct GlobalDescriptorSetIndices {
		uint32_t sampledImageId;
		uint32_t samplerId;
		uint32_t storageImageId;
		uint32_t uniformBufferId;
		uint32_t uniformTexelBufferId;
		uint32_t storageTexelBufferId;
	};
	
	//The main way users access and interface with data resources with action commands
	struct SubresourceBufferData {
		VkBuffer buffer;
		VkDeviceAddress address;
		VkDeviceSize offset;
		VkDeviceSize range;
	};

	struct SubresourceImageData {
		VkImage image;
		VkImageView imageView;
		VkImageSubresourceRange range;
		VkImageLayout readLayout; //Specifies the layout used for reading
		VkImageLayout writeLayout; //Specifies the layout used for writting
	};

	//Represents the exposed resources accessable by the command code
	class GraphResources {
	private:
		std::vector<std::variant<SubresourceBufferData, SubresourceImageData>> subResources;
	public:
		SubresourceBufferData getBufferData(vDataSubResourceHandle handle) {
			return std::get<SubresourceBufferData>(subResources[handle]);
		}

		SubresourceImageData getImageData(vDataSubResourceHandle handle) {
			return std::get<SubresourceImageData>(subResources[handle]);
		}
	};

	enum class QueueType {
		Primary,
		AsyncCompute,
		TransferDedicated,
	};

	enum class PassType {
		Graphics,
		Compute,
		Transfer
	};

	struct RenderingAttachmentInfo {
		VkFormat format;
		vDataSubResourceHandle subResourceImage;
		VkResolveModeFlags resolveMode;
		vDataSubResourceHandle subResourceResolveImage;
		VkAttachmentLoadOp loadOp;
		VkAttachmentStoreOp storeOp;
		VkClearValue clearValue;
	};

	struct RenderingInfo {
		VkRect2D renderArea;
		uint32_t viewMask;
		uint32_t colorAttachmentCount;
		const RenderingAttachmentInfo* pColorAttachments;
		const RenderingAttachmentInfo* pDepthAttachment;
		const RenderingAttachmentInfo* pStencilAttachment;
	};

	struct VertexBindInfo {
		VkDeviceSize offset;
		vDataResourceHandle buffer;
	};

	struct IndexBindInfo {
		VkDevice offset;
		VkIndexType type;
		vDataResourceHandle buffer;
	};

	struct DynamicViewportInfo {
		float x;
		float y;
		float width;
		float height;
		float minDepth;
		float maxDepth;
	};

	struct DynamicScissorInfo {
		VkOffset2D offset;
		VkExtent2D extent;
	};

	struct DynamicDepthBiasInfo {
		float depthBiasConstant;
		float depthBiasClamp;
		float depthBiasSlopeFactor;
	};

	struct DynamicStencilCompareInfo {
		VkStencilFaceFlags faceMask;
		uint32_t compareMask;
	};

	struct DynamicStencilWriteInfo {
		VkStencilFaceFlags faceMask;
		uint32_t writeMask;
	};

	struct DynamicStencilRefInfo {
		VkStencilFaceFlags faceMask;
		uint32_t reference;
	};


	struct PushConstantInfo {
		VkShaderStageFlags shaders;
		uint32_t offset;
		uint32_t size;
		void* pValues;
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
	Since Imageviews represents an actual vkobject as well than jsut being a subresource, can specify and attach imageview creation info to subresources structs if we know we need to use them.
	THe choice of allowing external subresources like imageviews kind of makes it difficult of giving flexibility to functions to generate necessary subresources on their own. So subresource creation is fully relegated compilation

Command Code should be able to access these subresources to both access the the resource the subresource points to and data pertaining to that subresource (Like ranges and imageview)

Some resources will be likely shared but also contextual to what a pass does. For example a pass may need a sampler to use alongside an imageview. But since samplers are just metadata not tied to a resource,
it is sharable across the entire graph. Need a way to allow passes to declare what kind of sampler they would use, while resolving the correct pointer to resource during pass setup during compilation (as during compilation is when we know how much we need)

Descriptor Sets:
Also applies to descriptor sets. As they can be sharable objects, but used in both setup (updating descriptor set) and commands (binding descriptor set) for each pass object, with former also requiring accessing resources on demand.
Descriptor sets require knowledge of the subresources they use and how it will be attached to fit the descriptor layout of pipeline computation of a pass. So the scope describing and setingup the pass
should be the one to declare how its descriptor set (if any) should be. But since descriptor sets are sharable, need to resolve to allow handle to point to sharedresource.
Can also reuse descriptor sets from previous executions. As long as we aware if the execution is finished and resources are released. Then can either reuse the descriptor set or dont even have to udpate it 
Though also need a way to ensure that we may know what passes use the same descriptor set(s) in an execution as well. As set bindings can be reused between passes as long as
the passes utilized sets that have both matching layouts and the sets bindings have the same resources. And resources in this case is subresources of images and buffers
and samplers (Only these three).

Basically want to minmize the number of binding descriptor sets during execution. And also absolutely minminize the need to recreate them/update them between executions

But if going for bindless with update after bind. Only need to bind descriptor set once, and as long as a pipeline supports its layout if it uses sets at all, then no need
to rebind. Thus can maintain the same set across even executions as long as updates to the descriptor set do not touch bindings/binding array elements that are being used
by previous executions.
For this case, instead of having executer check for this descriptor set in previous executions, checking if layouts match and if pointing to same resource(s) for every descriptor, treat it
differently

Perhaps best to distinguish external and transient descriptor sets.
External descriptor sets are those maintained by user and registered by user to executer. Executer doesnt have to worry about its creation or deletion. Just ensure it connects whatever it needs.
Transient Descriptor sets are those created and managed by executer. This means that it also has to check if it can be reused across executions that are waited by the CPU. 
Mostly use external descriptor sets for external resources. Though can allow transient resources to slot in the set though need to ensure when updates are needed if transient resource is invalidated.
Transient Descriptor sets  are also free to utilize external and transient resources.

Binding:
State Setting commands that do binding like pipelines, descriptor sets, vertex and index buffer, and even technically push constants can be both reusable or dynamically
changed across not only passes but within individual passes as well (Since a pass merely describes operations done using given read and write target resources). That means
that passes can contain multiple drawings/dispatches where they can either have so many bindings of these states. Or these bindings can span across multiple passes, where
either pipelines persist, sets persists, the vertex and index binding buffers persists, and push constants remain the same. (This mainly affects Graphic and Compute).

Binding commands could be given to the executer to figure out. Since some passes can utilize the same bindings, can ensure that passes are ordered in a way to minimize the
number of bindings needed to execute passes. Just need passes to declare what pipelines, descriptor sets, vertex and index bindings, and push constants are being used
by each pass. 
Though to allow inter-pass state changing between draws/dispatches, requires passes to be given flexibility to decide what bindings they want to use for a set of draw/dispatch
calls. Let passes contain a vector of structures that each define what pipeline, dSet, vertex and index buffer binding, and push constants they use (if they do) and commands
to run under these conditions. All are optional depending on the command code run in each. Like Transfer doesnt need any of these. And Compute only really needs a pipeline
and dSet. These structs are executed sequentially while either binding or pushing whatever is needed. The executer just checks what types of bindings each of thse use and
indicate when to bind after getting an idea on how the whole graph will execute. 
Note: The State Commands only affect action commands within individual command buffers so need to reinsert binds when recording to a new command buffer.

So in summary: Passes can only Graphics, Compute, or Transfer but not a combination of them as these three as that could lead to WAW race conditions as the scope of access is limited to the subresource: Image Subresource range and Buffer offset+range
Graphics Passes can utilize multiple pipelines, dSets, etc; with each having their set of draw commands;  As render Passes (More so subpasses) ensure that draw calls within them are enforced with primitive and rasterization order,
where Primitive Order ensures that primitives are ordered both within and across draw calls, and rasterization order enforces order of depth testing, blending, etc; for each primitive.
Compute Passes can only use one combination of pipeline, dSet, etc; and one lambda function to provide its commands
And Transfer, without any bindings or state setting really, only needs one lambda function to provide its commands

Other:
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

Passes can may have setup command code seperate from the structure code that is executed before these. Can be mostly used for non-state determined actions like clear

Would need to create/recreate graphics pipelines JIT as pipeline creation relies on render passes and the specific subpasses. And render passes
and subpasses depend on the order of pass executions and how attachments are used between them (With Dependencies as well). And even if using dynamic rendering, 
Perhaps best to also supply executer pipeline creation info so that it may use for both to support possibly of using renderpasses and to allow dynamically create and cache pipelines.
Best to have system where can add and gain handles to graphics pipeline, but the actual underlying resource creation is created/or reused in compilation of execution based on rendering info

During a Render Pass Instance, can only perform rendering within. So best practice ensures that graphics rendering is put together with minimal splits caused by
passes performing non-graphics action commands inbetween them. And better to maintain minimal split command buffers as well (Cross Queue Dependency), as that requires
a new render pass instance for the new command buffer.
Note: Dynamic Rendering allows rendering pass instances to suspend and resume. This allows render pass instances to persist across command buffers. So helps in 
situations with cross queue dependencies. Though can't perform any sync or action commands between a suspending and resuming render pass instance so non-graphic
action commands on the same queue can split instances.

Best to ignore subpasses for now as simply implementation dependent and tile-based.
Though problem is that pipeline barriers cant be used within instances, as subpasses are the primary method of syncronization within them. Though extensions like dynamic rendering-local-read
allow dynamic rendering to adapt subpass implementation and inter-syncing. Thus allows self-dependency as well.

So. for now, can just have individual render passes for each rendering part. Applies to both using dynamic rendering and render passes (Just one subpass). Can look into dynamic render-local-read
and render passes with one subpass (But with self-dependency) later if seeking to integrate barriers within multi passes

For managing and checking if resources are valid, aka reusable or need udpating, can do a system where invalidation propogates to connected resources. For example, if a buffer resource is
either removed or not used, then we can know that subresource of that buffer are invalidated. Another example could be with descriptor sets. If any subresources are invalidated that are a part
of that descriptor set, then can tell that the descriptor set needs to be updated.

!!!!
Just design around 1.3 Features in mind. Utilize One universal descriptor set that represents textures/sampler heap used by all pipelines, utilize buffer device addresses and push constants.
No longer need to manage descriptor sets, just let manager control of descriptors.
Can maybe still keep a heap of buffers in the global descriptor set, though decision to use Uniform Buffers vs BDA SSBO depends on dynamic changing data and size of data + runtime arrays
So best to only access them via BDA.
Since onlying using BDA to access buffers, can only rely on push constants. Though push constants are limited in size and can only send usualy just 16 buffer addresses through them.
Though can do some indirection by storing addresses in a buffer and passing the address of that via push constant, expanding the number of accessable addresses.
Can offset the buffer device address in application code to get addresses for pointing to data within the buffer that potentially represents sub-buffer resources.
Allows the app to dynamically supply runtime pointers to buffers and have shaders not needing to worry about anything else but just the SSBO buffer it needs.
Shaders overall just need to know/given the index into the global descriptor set for Textures and Uniform Buffers and the buffer addresses it needs

Technically can make multiple descriptor sets for each pipeline bind point, as each set can bind to it without interfering with each other's bindings. And can provide more slots to be available
by allowing each pipeline their own sets. Though requires more indexs to track for each resource for each pipeline

Have execution wait be indicated before pass set up/add transient resources and passes. Allows ahead of knowledge of what resources and handles are available to use. Mainly transient resources
and global descriptor set ids.

Resource representation and what handles should represent:
	For external data resources. Since only called once and not reliant on graph order, so can immediately create the resource and have handle directly connect to it.
	
	Transient data resource. First execution: Handle should point to a potential object that need to be created after pass order is parsed and know how to allocate the transient resource. Thus need to keep
	a list of the transient data resources that need to be created as wait till compilation starts.
	Future execution and reuse: If set so that we know what executions is dependent on by the next execution, can tell what data resources may be available for reuse ahead of time, like matching creation info.
	However factors like allocation of the memory and the usage may be different if the order of passes are seen to be different enough to warrent reallocation of aliasing resources.

	JIT: User provides the details what they want for transient resources. Executer will take that to both create and update resources after compilation of passes to ensure that these states and resources are correct and
	available when command srun. 

If doing independent descriptor sets in the future. Maintain both external sets and transient sets. Method of providing binding info to both type of sets for JIT updates after JIT initlization of resources,
and knowing when and how it can be reused. If the descriptor set has bindings that point to the same underlying subresource, then it can be reused from the previous depndent execution. Else create and update
a new descriptor set.

For push constants. Have method for specifying ranges of raw data, whether it may be known or transient and unknown at the time, then executer will manage that automatically. Can use a vector of std::byte and
pointer math. For example like with uploading buffer device addresses of transient resources via the push constants.

Similiar practice for uploading raw data, both known and unknown until compilation, to buffers like those used for uniform data. 

Perhaps use handles for state info just like with descriptor sets, for stuff like dynamic pipeline state and push constant, makes it easy to make it known what state to use for each pass by just seeing if handle
match. Though requires now passing around state info handles now, which could be hassle on user.

Top Down Build approach. Instead of inserting passes random then sorting. Ensure that passes dependent on another always comes after.

State Setting.
-Maintain a Global consistent set of states during execution recording that designates what values to use for a operation IF they can
use them. Command Buffer Change Invalidations and Pipeline Change Invalidations
-When we know that a specific set of states were invalidated due to these circumstances, signal to system that these states were invalidated.
-But to make sure no state settings are done uneccesary, maintain a system of flags an state availability for each pipeline and pass type
that indicates what states can actually be set.
-For Graphic Passes. Requires a pipeline bind. And needs to query the pipeline for what dynamic states it uses, 

Aliasing wth Pipeline Barrier and Submission Waiting
Since barriers and submission wait utilize syncing on a per-stage granularity, can't effectively use aliasing without having to spend cycles evaluating
dependency on a per-stage basis. So can leave out for now.
Could do stuff like using a heuristic to insert stop gaps in the code where it guarentees that resources before a certain dependency level is guarenteed
to be aliasable across all queues. Though figure out in future

Internal Resource Management of Transient Resources
Have handles managed by user correspond to custom objects representing the underlying vulkan objects.
For example. A dataResourceHandle corresponds to a unique Resource Object, and that object holds a VkBuffer/VkImage.
But since Vulkan objects are simply pointers, can describe these custom objects as simply use-a ownership over
the Vulkan object, but still allow the ownership to be transfered to other custom objects.
And if the custom object does not have ownership/never had the object, can set the Vulkan object to nullptr.
Allows Execution-Execution Dependency to both transfer ownership of Vulkan objects between Execution's transient objects
and for the previous owner tagging their Vulkan objects as nullptr.

So for Executions, track the handles for their transient objects via vectors: transientBuffers, transientImages, etc.
Can use an unordered map to map transient creation data with a container of handles that hold data that are similiar,
then can transfer ownship between two objects if a match is found.

For Deletion of transient resources, use the list of transient object handles for each Execution waited on to iterate through
them. If they do not have nullptr, then can perform deletion.

Image Layout
The executer, due to how data is given for read and write subresources, can only effectively know what subresources to image layout
transition if they are write targets. 
For transient subresources, not really a problem as they are always guarenteed to be written too. And can always assumed to be transitioning from undefined,
as the subresource may be being initally created or reused from a previous execution.
For External subresources, need to track layout to maintain data across executions. But also troublesome to try iterate through every read target subresource
to check if they need to be transitioned, especially since read targets are stored in a unordered set/map. So best to also keep it consistent
So let external image suberesources maintain a consistent image layout dependent on the inital layout of the resource it represents.
Writting to the external subresource that would require a image layout suitable for the specific write is valid, as long as the pass transitions it back to the layout it was right after.
Reading though is limited to the capabilities of the layout. Thus an external subresources layout, and by extension the external resources layout, represents the layout suitable for reading.
This alleviates the need to iterate through read targets as always assumed that read operations perform on a image subresource is always consistent with the layout compatibility.
Only exception is swapchain image resources and subresources. They start off as undefined. Though that is okay since normal passes will never read from them. Can treat them as undefined
and transition from that to the appropriate layout when writting to them and set the dst layout to undefined for now. But once a present pass is added, can simply change the barrier representing
the subresource to have a dst layout for presenting.
In fact, can do the same for all subresources with undefined layout, external or transient. Transition to whatever write layout. Keep dstLayout as undefined temporary. But once a read from
a dependency is found, can update the respective subresources layout to the desired read layout. This works as guarenteed written too subresources will always show up in the list of dependenies
of the executioon which is iterated though.
This also ensures that multiple separate executions are guarenteed to know that a read-only external resource will always have a consistent layout despite being utilized by multiple
of them for reading at the same time.

Swapchain.
Have Executer manage and create Swapchain images and subresource images, just haveto supply it with a surface, since
each swapchain is associated with a surface. Executer will create a swapchain for the surface. But since swapchain manages its
own images, executer doesnt exactly have ownership over the underlying VkImages. So after creating the swapchain, executer
acquires its images and assigns them to vDataResourceHandles, that should be in a list and ordered in the same order as
these acquired swapchain images. Then it creates subresource range image views as well for each one. Depending on parameters,
these imageviews can be adjusted as such. But usually, an imageview is created to target for each image covering the one array
layer.
Then return a Swaphchain handle, that points to the swapchain object and the list of resources.

Then, during an execution. Can then acquire next swapchain image via the swapchain handle. Which internally, have executer
track the current index for that swapchain to use for presenting. Then the acquire function returns the handle to the 
swapchain image and the associated subresource image view.

With the above idea on how layout transition in mind, have to let executer handle swapchain image presenting.
Can define a special Present Pass that accepts a different type of parameters. SInce it can present multiple swapchain images,
let it accept multiple swapchain handle(s) as input. The executer will resolve syncing by checking for the subresource of the
current swapchain image (via its current acquired index) with the subresource dependenices. Likely that 

Acquiring Swapchain image allows signalling non-timeline semaphores as well. So technically part of an execution as well.
This should be sync such that the queue(s) that require usage of the swapchain image should wait on this semaphore.
Acuiring Swapchain image can't be really treated like a pass that read/writes to resources, as it is simply designating
when a swapchain image is ready for writting. 
Let Acquiring be somewhat aport of execution recording. Before an Execution, have user call Executer to Acquire the next
swapchain image, returning the index of appropriate vDataSubresourceHandle of Swapchain subresource image
operation on swapchain semaphore.
Then when Executer is given a pass that writes to the swapchain image, it starts a new command buffer for the appropriate
queue that waits on the semaphore for the respective swapchain acquisition, so that it waits for the subresource to be
available to actually write too.

Host Reading
Host Reading requires performing a memory barrier to make a resource available for reading. For this case, just specify
an external resource as one that will be read on host, then the executer will insert appropriate barrier info for host
reading after done writting to it.

ReadTargets
Use a specialized object for collecting subresource read information, both external and transient versions. 
This allows maintaing what subresource passes read from without having to iterate through large collection of data resources
just to specify what is being read while performing other operations that will help parsing these read targets.

 Interesting SubResources
 Instead of Read Targets storing maps with subresource as keys. Instead store maps with resources as keys, that map to the list
 of subresources used for reading. Thus each writing subresource dependency can quickly check for a matching resource read from
 the pass then iterate through the list of read subresources, checking for insection for each.

 Queue Family Ownership Transfer
 Have to worry about queue family ownership of resources as well before an execution starts

Note that since Queue Family Ownership Transfer only matters in the context of reading operations, as can still write
to resources that are owned by a different queue from another queue, its just that the contents of the data in the resource
will become undefined.
With this info, dont have to worry about performing ownership transfer over transient resources, as assumed undefined even if 
its a reused underlying resource

For external resources, in order to maintain the data, similiar to image layout, can maintain a constant queue family owner
for the resource that must be respected. Or it can be queue family shared resource.
A execution can write to a subresource of this external resource from any queue, but must guarentee that after the operation
that ownership is transfered to queue family owner defined by the resource for the specified subresource range.
It's difficult to say on how the subresource is determine for the automatic implicit queue family owenership acquisition.
Can perhaps experiment with like a dummy barrier to see if its possible to specify the subresource range.
If its too much trouble, can just make the resource concurrent instead. And have write to exclusive resources limited so that
it will be known that it will end up undefined for the whole resource when performing a write from a different queue family
then it belong too.

For Concurrent resources, since its specified what queue families use the reosurce, both writes and read are restricted
to the specified queue families.

Executions
Want to treat them as more broad series of tasks that user can interact with to establish dependencies and to allow
waiting on them. To allow resource sharing between dependenies, want to establish full command waiting via semaphores.

Note: In order to allow executions with dependencies but are not neccesary to have separate vkqueue submits, can have
the submission/execute function execute all currently know executions in the order they were instatiated. 

One example coulde be like Rendering Exection -> Post Processing+Present Execution per frame. So create the first, then the second
with the first specified as dependency. After that can execute so that both are submitted via the same vkqueue submit.
Then in the next frame, in order to reuse transient resource, have this frame's rendering execution depend on the 
previous frame's rendering execution, and then the frame's post processing one depending on the same frame rendering exectuion
and the previous frames post processing execution. Thus the second frame's rendering execution can execute while first frame
does its post process + present.

But also dont want to do a full command wait between rendering and post process executions. And also prefer them not stealing
resoruces from each other.

Instead, let Executions represents distinct set of passes within a recording that can be assign as a dependency for 
another Execution, and this dependency enforces a full command wait for the dependening Execution until all its dependencies
finish. But Executions in the same recording, if they have no dependency connection, will have the same tight syncronization
utilized normally. Transient resources still are limited to the Executions they are created in, thus reading and writing
of these transient resources are also limited within an Execution. Thus external resources must be used for read-write interaction
between Executions.

Can more so think of Executions as just separate rendergraaphs that read/write external resources.

Another example is staging data. Can have one execution represent the upload of staging data from the staging buffer to other
external resources. By decoupling it from the render execution, can now just wait on the Staging Execution when we need to wait
for the subresource range of the staging buffer to be available for reuse instead of waiting for like the whole rendering execution.

So basically use Executions to represent distinct parts of tasks that work on external resources. Though doesnt restrict writting and reading the same external resource in a Execution
Allow Executions within the same recording to have tight syncronization with each other, 
and allow executions to have a dependency with other executions that enforce
all commands wait, allowing reuse of transient resources of the dependent execution. And Executions allow multiple parts of a frame
to run concurrently with other frame's executions.

For syncing executions within the same recording, more so how to sync with respect to external resources with overhead of syncing
queues in mind. To keep the number of command buffers minimal, for any cross queue dependency involving external resources
that are interacted between different executions, want to utilize the semaphore signalling established by executions as much
as possible. So when creating executions, can mark the deplevels for each queue where execution ends and use that as a good place to start
for finding where to cut command buffers, as its guarenteed by execution to be cut.

How to ensure that everything within an execution finishs?
Can't reliably use just the last commandd buffer for each queue as simply
the ones that just need to signal. Need to track dependencies between command buffers and figure out which ones
are the last in a respective dependency chain
Actually, due to semaphore signalling and waiting guarenteeing that all commands are included in the respective scope in submission order occuring before or after,
just need the last command buffers to signal and the first command buffers to wait within an execution.
 
 The Stage flags should be fine grained and specific to what the Execution does. This reduces the chances of 
 waiting for uneccesary worked to be drained before a dependent Execution can execute. One example is two
 separate Executions, one runs a transfer on the Primary Queue, the other compute on the same Queue. If have another
 Execution that is dependent on the first, thus having the signaling stage mask jsut include transfer will prevent
 the Execution from waiting on the latter.

 Above requires just tracking all the stages performed by the Execution on each queue. Then having it wait and
 signal with just those stages.

 Note: Using PipelineBarriers2 and VkQueueSubmit2

 ImageLayout2
 The previous layout of the image chages if a iamge subreoursce shoudl be transitioned and if to jsut use memory barrier instead
 Only care about transitioning if actually wrote to the subresource in my impementation.
 If General, though unsure of performance difference, can leave out transioning the image for any kind of writes or reads
 Some layouts also supports layouts where they cover multiple bases like read and write or an attachment that can be used for both
 color and depth+stencil.
 This means tthat finding the optimal layout is dependent on both the write and reads performed on a subresource.
For external, guarenteed that the layout after writting will match what it was created with. So its up to user to define
a layout suitable for not only its read but also the expected usual writes to the image to reduce pre write layout transitions
when believed to be suitable..
For transient, since trying to figure out the layout based on write and reads. So hard to say how to optimize it.
Aim for minimizing transitions by determing the an appropriate layout based on the write and reads performed on the
iamge subresource, but trying not to use general unless neccesary (storage image write)
ex: A subrsource is used as a wirte in a color attachment, but hen used as a read as a depth attachment, can go for the generic
attachment optimal, skipping the need to transition image.
But this requires holding off of adding the barrier representing the subresource to the list of barriers, as the image subresource
will end up not needing to transition, thus can instead opt for a memory barrier.
Same can be said for when a subresource requires queue ownership transition, the subresource must use a buffer/image memory
barrier instead.
So best to instead of barrier information for each subresource dependency then iterate through dependencies after, creating
the actual barriers needed for execution.

Since Commands parameters usually take the layout for images, want them to access the final decided layout for both writting
and reading. For Action Commands especially, want to pass layout for reading and writting.

Minimizing Barriers
First start off with subresource dependencies having their specified write and read stage masks, write and read image layout,\
and queue write and read access info.
Iterate through list, first figure out if utilize just a memory barrier or a more granular barrier.
Then if utilizing global memory barrier, check for any existing global memory barriers within the same dep level
by iterating through the list of current memory barriers for overlapping stage masks. For each adding barrier, 
check with each existing barrier if they already represent the adding barrier
or if it can be updated to do so. And if no more existing barrrers represent the barrier,
then create a new barrier to represent the non represented stages we have left. 
Representing means that the adding barrier is a subset of the existing barrier.
If it the adding barrier intersects with the existing or the existing is a subset of the adding, then can try
update the existing to try fully or partially represent the adding barrier based on matching or intersecting src and dst stage
masks.

Timeline Semaphores
A timeline semaphore signal operation can represent multiple pairs of (signal, wait) as long as signal is the same for
these set of pairs, same dep level of signalling and same src stage flags. Thus for every wait, at the dep level specified
for waiting, the value representing that timeline semaphore signal should be waited on.
How are timeline semaphores utilized?
A timeline semaphore is needed for every path of signaling within the entire execution across all queues.
If two signals are not connected by stage mask dependency chain, then would need two separate timeline semaphores for them.
Can happen when they represent two distinct operations or a fork of operations.
But can use any timeline semaphore if a signal is connected to multiple previous signals.
This works as a timeline semaphore works by guarenteeing that if signal1 sets timeline semaphoreA to 1, and signal2 sets timeline
semaphoreB to 2, then signal2 implies that signal1 sets off, and thus the operation represented by signal1 is finished

For Syncing Executions, can reuse timeline semaphores that are used to sync the internal cross queue dependencies.
Treat Timeline semaphores as just

Minimizing Semaphores
Prioritize minimizing semaphores by placing them with respect of indirect dependenices.
Semaphore signalling and waiting is similiar to a barrier, but a disjoint where the placement of signal and wait
and the stage flags complicate this further.

For each cross queue accessed subresource dependency, let there be (signal, wait) pair that would represent a semaphore
signal and wait operation. With signal having the queue that signals and dep level of it. And wait the queue that waits and dep level of it

For each adding pair, check with existing pairs signal and waits like with global memory barriers to reduce redundant signals
and waits within the same dep levels.
Note that multiple waits can be tied to a single signal. And a signal basically represents a single semaphore (though timeline
semaphores its kind of weird).
Let the existing pair be pair1 (sig1, wait1) and adding pair be pair2 (sig2, wait2). Pair1 fully represents pair2 if the dep 
and write levels of both their sig and waits match and the stage masks of pair2's sig2 and wait2s is a subset of pair1's.
Thus dont need to add pair2.
When pair2 intersects with pair1 or pair1 is a subset of pair2 in terms of stage masks and dep level+queue of access of sig and wait,
can try to update pair1 to fully or partially represent pair2, and add pair2 with the rest of nonrepresented parts of it.
So can end up pair2 not being needed or added with the unrepresented parts or just fully added. Can also have situations where
pair2 is updated as (sig1, wait2) so wait2 waits on the signal operation represented by sig1, thus use same semaphore signalling. Though doesnt work other way
around as something like pair2 being changed to (sig2, wait1) still represents usage of a different semaphore.

For culling redundant dependencies do to the chain of dependencies between stages established by barriers and other semaphores
operatons.
To do this, perform a second pass over the list of pairs after the first pass performs its culling, For each pair, compare it 
with other pairs and see if both the other pair's signal and dep level exist between the first, inclusive, iterate through dep levels
from both ends to see if they are connected by stage mask chain through both barriers and semaphores. Then if so, cull the outer pair.

Note: Granularity of both semaphore signaling and waiting is by dep levels. And treat semaphores similiar to barrier in terms
on how to handle multiple reads acros s multiple dep levels, have only one wait that include all dstStages for reading at the earliest dep level. 
Trying to do more granular approach doesnt work with queue family ownership transfer, and would require also tracking multiple
points where a subresource is read from, and more semaphores and split command buffers.

Note: Queue family ownership transfer doesnt allow granular stages for signaling. Must use all commands stage for srcMask
when signaling.
*/  
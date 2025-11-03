#pragma once
#include "renderGraph.h"

namespace render_graph {
	/*
			Prepares RenderGraph for execution and executes the RenderGraph on given Command Queues. For now, should only have one RenderGraphExecuter running at a time.
	*/
	class RenderGraphExecuter {
	public:
		void attachQueues(listOfStructsWithInfoForEachQueue);
		void attachRenderGraph(const render_graph::RenderGraph& renderGraph);
		void executeRenderGraph();
	private:
		RenderGraph _renderGraph; //Have one rendergraph for now. Not sure if should be pointer or not. Keep as value for now
		ResourceBufferRef _resourceBuffers;
		ResourceImageRef _resourceImages;

		std::unordered_set<ResourceName> _isResourceExternal; //Checks if Resource is a External. Used to check if we need to destroy it or not.
	};
}
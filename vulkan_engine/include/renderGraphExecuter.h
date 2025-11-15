#pragma once
#include "renderGraph.h"

namespace render_graph {
	/*
			Prepares RenderGraph for execution and executes the RenderGraph on given Command Queues. For now, should only have one RenderGraphExecuter running at a time.
	*/
	class RenderGraphExecuter {
	public:
		//Add Resource Function here instead of RenderGraph? Maybe should be the one to managed resources
		void attachRenderGraph(const render_graph::RenderGraph& renderGraph);
		void executeRenderGraph();
	private:
		RenderGraph _renderGraph; //Have one rendergraph for now. Not sure if should be pointer or not. Keep as value for now
		ResourceBufferRef _resourceBuffers;
		ResourceImageRef _resourceImages;
	};
}

/*
	For Setting up syncronization, using barries for syncing using the same queue. Use Timeline Semaphores for syncing across multiple queues. Note can use multiple queues submits with semaphores to
	make multi queue syncing work while allowing concurrency. 
*/
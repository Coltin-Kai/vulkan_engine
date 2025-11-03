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
		RenderGraph renderGraph; //Have one rendergraph for now. Not sure if should be pointer or not. Keep as value for now
		std::unordered_map<ResourceName, ResourceRef> _resourceRef; //Maps ResourceName to actual underlying resource: Buffer, Image, etc
	};
}
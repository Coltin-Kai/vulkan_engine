#include "renderGraphExecuter.h"

using namespace render_graph;

void RenderGraphExecuter::attachRenderGraph(const render_graph::RenderGraph& renderGraph) {
	//Figure out Resource Aliasing for graph by gathering all transient resources used in the graph, using depedency levels as timelines for each, and then partitioning them into buckets, giving a list of offsets that show how memory should be partitioned
	//Input: DependencyLevels, ResourceNames; Output: Buckets for each allocation, with each info about the resourcename, size, and offset

	//Initalize and allocate resources: vkbuffers, vkiamge, etc.

	//Partition Graph's Nodes to however many Queues specified.

	//Generate SSIS (set of indices representing closest nodes/passes) for each pass to figure out all the inter-dependencies between nodes.
	//Input: Need How many Queues, which queue a node is associated with, Adjacency List, The list of passes.

	//Use the SSIS to cull indirect dependencies to reduce sync points

	//Reroute Transitions to a "main" queue (likely graphics) for resources that are used by multiples queues and can possibly cause conflict.
}

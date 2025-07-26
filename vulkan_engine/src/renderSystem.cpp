#include "renderSystem.h"
#include <iostream>
#include <format>
#include <stack>

void RenderSystem::init(VkExtent2D windowExtent) {
	init_swapchain(windowExtent);
	init_frames();
	init_vertexInput();
	init_descriptorSet();
	init_graphicsPipeline();

	setup_depthImage();

	_deviceBufferTypesCounter[DeviceBufferType::ViewProj] = 0;
	_deviceBufferTypesCounter[DeviceBufferType::Indirect] = 0;
	_deviceBufferTypesCounter[DeviceBufferType::PrimID] = 0;
	_deviceBufferTypesCounter[DeviceBufferType::PrimInfo] = 0;
	_deviceBufferTypesCounter[DeviceBufferType::Model] = 0;
	_deviceBufferTypesCounter[DeviceBufferType::Index] = 0;
	_deviceBufferTypesCounter[DeviceBufferType::Vertex] = 0;
	_deviceBufferTypesCounter[DeviceBufferType::Material] = 0;
	_deviceBufferTypesCounter[DeviceBufferType::Texture] = 0;
	_deviceBufferTypesCounter[DeviceBufferType::Light] = 0;
}

VkResult RenderSystem::run() {
	VkResult result;
	result = draw();

	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		std::cerr << "Render System: Failed to draw" << std::endl;
	}

	return result;
}

void RenderSystem::shutdown() {
	//Depth Image
	_vkContext.destroy_image(_depthImage);

	//Cleanup Pipeline
	vkDestroyPipelineLayout(_vkContext.device, _pipelineLayout, nullptr);
	vkDestroyPipeline(_vkContext.device, _pipeline, nullptr);

	//Cleanup Descriptor Stuff
	vkDestroyDescriptorSetLayout(_vkContext.device, _descriptorSetLayout, nullptr);
	vkDestroyDescriptorPool(_vkContext.device, _descriptorPool, nullptr);

	for (Frame& frame : _frames) {
		vkDestroyCommandPool(_vkContext.device, frame.commandPool, nullptr);
		vkDestroySemaphore(_vkContext.device, frame.renderSemaphore, nullptr);
		vkDestroySemaphore(_vkContext.device, frame.swapchainSemaphore, nullptr);
		vkDestroyFence(_vkContext.device, frame.renderFence, nullptr);

		_vkContext.destroy_buffer(frame.drawContext.indirectDrawCommandsBuffer);
		_vkContext.destroy_buffer(frame.drawContext.vertexPosBuffer);
		_vkContext.destroy_buffer(frame.drawContext.vertexOtherAttribBuffer);
		_vkContext.destroy_buffer(frame.drawContext.indexBuffer);
		_vkContext.destroy_buffer(frame.drawContext.viewprojMatrixBuffer);
		_vkContext.destroy_buffer(frame.drawContext.modelMatricesBuffer);
		_vkContext.destroy_buffer(frame.drawContext.primitiveIdsBuffer);
		_vkContext.destroy_buffer(frame.drawContext.primitiveInfosBuffer);
		_vkContext.destroy_buffer(frame.drawContext.materialsBuffer);
		_vkContext.destroy_buffer(frame.drawContext.texturesBuffer);
		_vkContext.destroy_buffer(frame.drawContext.lightsBuffer);
	}

	//Cleanup Swapchain
	destroy_swapchain();
}

Image RenderSystem::get_currentSwapchainImage() {
	return _swapchain.images[_swapchainImageIndex];
}

VkExtent2D RenderSystem::get_swapChainExtent() {
	return _swapchain.extent;
}

VkFormat RenderSystem::get_swapChainFormat() {
	return _swapchain.format;
}

void RenderSystem::init_swapchain(VkExtent2D windowExtent) {
	vkb::SwapchainBuilder builder{ _vkContext.physicalDevice, _vkContext.device, _vkContext.surface };

	_swapchain.format = VK_FORMAT_B8G8R8A8_UNORM; //Might change when needed to implement high precision stuff.

	builder.set_desired_format(VkSurfaceFormatKHR{ .format = _swapchain.format, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR });
	builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
	builder.set_desired_extent(windowExtent.width, windowExtent.height);
	builder.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT);

	vkb::Swapchain vkbSwapchain = builder.build().value();

	_swapchain.extent = vkbSwapchain.extent;
	_swapchain.vkSwapchain = vkbSwapchain.swapchain;

	std::vector<VkImage> imgs = vkbSwapchain.get_images().value();
	std::vector<VkImageView> imgViews = vkbSwapchain.get_image_views().value();

	_swapchain.images.clear(); //Ensures that future calls to this function, like when resizing swapchain, will remove existing deleted images
	_swapchain.images.reserve(imgs.size());
	for (int i = 0; i < imgs.size(); i++) {
		_swapchain.images.push_back({ .image = imgs[i], .imageView = imgViews[i], .extent = { .width = 0, .height = 0, .depth = 0 } }); //Since using swapchain struct's extent, no need for indivudual image extents.
	}
}

void RenderSystem::init_frames() {
	//Init frames' command and sync-structures
	VkCommandPoolCreateInfo cmdPoolInfo{};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.pNext = nullptr;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cmdPoolInfo.queueFamilyIndex = _vkContext.graphicsQueueFamily;

	VkFenceCreateInfo fenceCreateInfo{};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkSemaphoreCreateInfo semaphoreCreateInfo{};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;

	for (int i = 0; i < FRAMES_TOTAL; i++) {
		VK_CHECK(vkCreateCommandPool(_vkContext.device, &cmdPoolInfo, nullptr, &_frames[i].commandPool));

		VkCommandBufferAllocateInfo cmdAllocInfo{};
		cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdAllocInfo.pNext = nullptr;
		cmdAllocInfo.commandPool = _frames[i].commandPool;
		cmdAllocInfo.commandBufferCount = 1;
		cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

		VK_CHECK(vkAllocateCommandBuffers(_vkContext.device, &cmdAllocInfo, &_frames[i].commandBuffer));

		VK_CHECK(vkCreateFence(_vkContext.device, &fenceCreateInfo, nullptr, &_frames[i].renderFence));
		VK_CHECK(vkCreateSemaphore(_vkContext.device, &semaphoreCreateInfo, nullptr, &_frames[i].swapchainSemaphore));
		VK_CHECK(vkCreateSemaphore(_vkContext.device, &semaphoreCreateInfo, nullptr, &_frames[i].renderSemaphore));
	}
}

void RenderSystem::init_vertexInput() {
	//Vertex Buffer Format
	//VB1: [pos1, pos2, ...]
	//VB2: [(norm1, tan1, ...), (norm2, tan2, ...), ...] where each () corresponds to a vertex

	//Set up Vertex Input Descriptions
	_bindingDescriptions = { {},{} };

	//Vertex Position Buffer
	_bindingDescriptions[0].binding = 0;
	_bindingDescriptions[0].stride = sizeof(glm::vec3);
	_bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	//Vertex Other Attributes Buffer
	_bindingDescriptions[1].binding = 1;
	_bindingDescriptions[1].stride = sizeof(RenderShader::VertexAttributes);
	_bindingDescriptions[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	_attribueDescriptions = { {}, {}, {}, {}, {} };

	//Position
	_attribueDescriptions[0].binding = 0;
	_attribueDescriptions[0].location = 0;
	_attribueDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	_attribueDescriptions[0].offset = 0;

	//Normal
	_attribueDescriptions[1].binding = 1;
	_attribueDescriptions[1].location = 1;
	_attribueDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	_attribueDescriptions[1].offset = 0;

	//Tangent
	_attribueDescriptions[2].binding = 1;
	_attribueDescriptions[2].location = 2;
	_attribueDescriptions[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
	_attribueDescriptions[2].offset = offsetof(RenderShader::VertexAttributes, RenderShader::VertexAttributes::tangent);

	//Color_0
	_attribueDescriptions[3].binding = 1;
	_attribueDescriptions[3].location = 3;
	_attribueDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
	_attribueDescriptions[3].offset = offsetof(RenderShader::VertexAttributes, RenderShader::VertexAttributes::color);

	//UV_0
	_attribueDescriptions[4].binding = 1;
	_attribueDescriptions[4].location = 4;
	_attribueDescriptions[4].format = VK_FORMAT_R32G32_SFLOAT;
	_attribueDescriptions[4].offset = offsetof(RenderShader::VertexAttributes, RenderShader::VertexAttributes::uv);
}

void RenderSystem::init_descriptorSet() {
	//Create Descriptor Pool
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = MAX_SAMPLED_IMAGE_COUNT },
		{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = MAX_SAMPLER_COUNT }
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 1;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

	if (vkCreateDescriptorPool(_vkContext.device, &poolInfo, nullptr, &_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create Descriptor Pool");

	//Create Descriptor Set Layout for Descriptors
	//-Set Layout Bindings
	std::vector<VkDescriptorSetLayoutBinding> layout_bindings = {
		{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = MAX_SAMPLED_IMAGE_COUNT,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr },
		{.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = MAX_SAMPLER_COUNT,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr }
	};

	//-Set Binding Flags
	std::vector<VkDescriptorBindingFlags> binding_flags = {
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
	};

	VkDescriptorSetLayoutBindingFlagsCreateInfo set_binding_flags{};
	set_binding_flags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	set_binding_flags.bindingCount = static_cast<uint32_t>(binding_flags.size());
	set_binding_flags.pBindingFlags = binding_flags.data();

	//-Now Create Descriptor Set Layout
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext = &set_binding_flags;
	layoutInfo.bindingCount = static_cast<uint32_t>(layout_bindings.size());
	layoutInfo.pBindings = layout_bindings.data();
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

	if (vkCreateDescriptorSetLayout(_vkContext.device, &layoutInfo, nullptr, &_descriptorSetLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to Create Descriptor Set Layout");

	//Create Descriptor Set
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &_descriptorSetLayout;

	if (vkAllocateDescriptorSets(_vkContext.device, &allocInfo, &_descriptorSet) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate Descriptor Set");
}

void RenderSystem::setup_drawContexts(const GraphicsDataPayload& payload) { 
	RenderShaderData renderData;
	DeviceBufferTypeFlags dataType;
	dataType.setAll();
	extract_render_data(payload, dataType, renderData);

	//Add Data to DrawContexts
	const size_t buffer_size = 40000000;

	size_t alloc_vertPos_size = sizeof(glm::vec3) * renderData.positions.size();
	size_t alloc_vertAttrib_size = sizeof(RenderShader::VertexAttributes) * renderData.attributes.size();
	size_t alloc_index_size = sizeof(uint32_t) * renderData.indices.size();
	size_t alloc_indirect_size = sizeof(VkDrawIndexedIndirectCommand) * renderData.indirect_commands.size();
	size_t alloc_viewprojMatrix_size = sizeof(glm::mat4) * 2;
	size_t alloc_modelMatrices_size = sizeof(glm::mat4) * renderData.model_matrices.size();
	size_t alloc_primIds_size = sizeof(int32_t) * renderData.primitiveIds.size();
	size_t alloc_primInfo_size = sizeof(RenderShader::PrimitiveInfo) * renderData.primitiveInfos.size();
	size_t alloc_materials_size = sizeof(RenderShader::Material) * renderData.materials.size();
	size_t alloc_textures_size = sizeof(RenderShader::Texture) * renderData.textures.size();
	size_t alloc_lights_size = sizeof(RenderShader::Lights);

	int i = 1;
	for (Frame& frame : _frames) {
		DrawContext& currentDrawContext = frame.drawContext;
		currentDrawContext.drawCount = renderData.indirect_commands.size();

		//Draw Coommand and Vertex Input Buffers	
		VmaAllocationCreateFlags allocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
		currentDrawContext.indirectDrawCommandsBuffer = _vkContext.create_buffer(std::format("Indirect Draw Commands Buffer {}", i).c_str(), buffer_size, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
		currentDrawContext.vertexPosBuffer = _vkContext.create_buffer(std::format("Vertex Position Buffer {}", i).c_str(), buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
		currentDrawContext.vertexOtherAttribBuffer = _vkContext.create_buffer(std::format("Vertex Other Attributes Buffer {}", i).c_str(), buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
		currentDrawContext.indexBuffer = _vkContext.create_buffer(std::format("Index Buffer {}", i).c_str(), buffer_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
		//BDA Buffers
		VkBufferUsageFlags storageUsageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		currentDrawContext.viewprojMatrixBuffer = _vkContext.create_buffer(std::format("View and Projection Matrix Buffer {}", i).c_str(), buffer_size, storageUsageFlags, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags); //Careful as if the alloc size is 0. Will cause errors
		currentDrawContext.modelMatricesBuffer = _vkContext.create_buffer(std::format("Model Matrices Buffer {}", i).c_str(), buffer_size, storageUsageFlags, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
		currentDrawContext.primitiveIdsBuffer = _vkContext.create_buffer(std::format("Primitive IDs Buffer {}", i).c_str(), buffer_size, storageUsageFlags, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
		currentDrawContext.primitiveInfosBuffer = _vkContext.create_buffer(std::format("Primitive Infos Buffer {}", i).c_str(), buffer_size, storageUsageFlags, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
		currentDrawContext.materialsBuffer = _vkContext.create_buffer(std::format("Materials Buffer {}", i).c_str(), buffer_size, storageUsageFlags, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
		currentDrawContext.texturesBuffer = _vkContext.create_buffer(std::format("Textures Buffer {}", i).c_str(), buffer_size, storageUsageFlags, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
		currentDrawContext.lightsBuffer = _vkContext.create_buffer(std::format("Lights Buffer {}", i).c_str(), buffer_size, storageUsageFlags, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);

		VkBufferDeviceAddressInfo address_info{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		address_info.buffer = currentDrawContext.viewprojMatrixBuffer.buffer;
		currentDrawContext.viewprojMatrixBufferAddress = vkGetBufferDeviceAddress(_vkContext.device, &address_info);
		address_info.buffer = currentDrawContext.modelMatricesBuffer.buffer;
		currentDrawContext.modelMatricesBufferAddress = vkGetBufferDeviceAddress(_vkContext.device, &address_info);
		address_info.buffer = currentDrawContext.primitiveIdsBuffer.buffer;
		currentDrawContext.primitiveIdsBufferAddress = vkGetBufferDeviceAddress(_vkContext.device, &address_info);
		address_info.buffer = currentDrawContext.primitiveInfosBuffer.buffer;
		currentDrawContext.primitiveInfosBufferAddress = vkGetBufferDeviceAddress(_vkContext.device, &address_info);
		address_info.buffer = currentDrawContext.materialsBuffer.buffer;
		currentDrawContext.materialsBufferAddress = vkGetBufferDeviceAddress(_vkContext.device, &address_info);
		address_info.buffer = currentDrawContext.texturesBuffer.buffer;
		currentDrawContext.texturesBufferAddress = vkGetBufferDeviceAddress(_vkContext.device, &address_info);
		address_info.buffer = currentDrawContext.lightsBuffer.buffer;
		currentDrawContext.lightsBufferAddress = vkGetBufferDeviceAddress(_vkContext.device, &address_info);
		
		//Copy Data to the Buffers
		_vkContext.update_buffer(currentDrawContext.indirectDrawCommandsBuffer, renderData.indirect_commands.data(), alloc_indirect_size, renderData.indirect_copy_info);
		_vkContext.update_buffer(currentDrawContext.vertexPosBuffer, renderData.positions.data(), alloc_vertPos_size, renderData.pos_copy_info);
		_vkContext.update_buffer(currentDrawContext.vertexOtherAttribBuffer, renderData.attributes.data(), alloc_vertAttrib_size, renderData.attrib_copy_info);
		_vkContext.update_buffer(currentDrawContext.indexBuffer, renderData.indices.data(), alloc_index_size, renderData.index_copy_info);
		_vkContext.update_buffer(currentDrawContext.viewprojMatrixBuffer, &renderData.viewproj, alloc_viewprojMatrix_size, renderData.viewprojMatrix_copy_info);
		_vkContext.update_buffer(currentDrawContext.modelMatricesBuffer, renderData.model_matrices.data(), alloc_modelMatrices_size, renderData.modelMatrices_copy_infos);
		_vkContext.update_buffer(currentDrawContext.primitiveIdsBuffer, renderData.primitiveIds.data(), alloc_primIds_size, renderData.primId_copy_info);
		_vkContext.update_buffer(currentDrawContext.primitiveInfosBuffer, renderData.primitiveInfos.data(), alloc_primInfo_size, renderData.primInfo_copy_infos);
		_vkContext.update_buffer(currentDrawContext.materialsBuffer, renderData.materials.data(), alloc_materials_size, renderData.material_copy_infos);
		_vkContext.update_buffer(currentDrawContext.texturesBuffer, renderData.textures.data(), alloc_textures_size, renderData.texture_copy_infos);

		RenderShader::Lights lights;
		lights.pointLightCount = renderData.pointLightsCount;
		std::copy(renderData.pointLights.begin(), renderData.pointLights.end(), lights.pointLights);
		_vkContext.update_buffer(currentDrawContext.lightsBuffer, (void*)&lights, alloc_lights_size, renderData.light_copy_info);
		i++;
	}
}

std::vector<VkBuffer> RenderSystem::get_vertexBuffers() {
	DrawContext& currentDrawContext = get_current_frame().drawContext;
	return std::vector<VkBuffer>{currentDrawContext.vertexPosBuffer.buffer, currentDrawContext.vertexOtherAttribBuffer.buffer};
}

VkBuffer RenderSystem::get_indexBuffer() {
	DrawContext& currentDrawContext = get_current_frame().drawContext;
	return currentDrawContext.indexBuffer.buffer;
}

RenderShader::PushConstants RenderSystem::get_pushConstants() {
	DrawContext& currentDrawContext = get_current_frame().drawContext;
	RenderShader::PushConstants pushconstants{};
	pushconstants.primitiveIdsBufferAddress = currentDrawContext.primitiveIdsBufferAddress;
	pushconstants.primitiveInfosBufferAddress = currentDrawContext.primitiveInfosBufferAddress;
	pushconstants.viewProjMatrixBufferAddress = currentDrawContext.viewprojMatrixBufferAddress;
	pushconstants.modelMatricesBufferAddress = currentDrawContext.modelMatricesBufferAddress;
	pushconstants.materialsBufferAddress = currentDrawContext.materialsBufferAddress;
	pushconstants.texturesBufferAddress = currentDrawContext.texturesBufferAddress;
	pushconstants.lightsBufferAddress = currentDrawContext.lightsBufferAddress;
	return pushconstants;
}

VkBuffer RenderSystem::get_indirectDrawBuffer() {
	DrawContext& currentDrawContext = get_current_frame().drawContext;
	return currentDrawContext.indirectDrawCommandsBuffer.buffer;
}

uint32_t RenderSystem::get_drawCount() {
	DrawContext& currentDrawContext = get_current_frame().drawContext;
	return currentDrawContext.drawCount;
}

void RenderSystem::signal_to_updateDeviceBuffers(DeviceBufferTypeFlags bufferType) {
	//Update COunter associated with type of data
	if (bufferType.viewProjMatrix)
		_deviceBufferTypesCounter[DeviceBufferType::ViewProj] = FRAMES_TOTAL;
	if (bufferType.indirectDraw)
		_deviceBufferTypesCounter[DeviceBufferType::Indirect] = FRAMES_TOTAL;
	if (bufferType.primID)
		_deviceBufferTypesCounter[DeviceBufferType::PrimID] = FRAMES_TOTAL;
	if (bufferType.primInfo)
		_deviceBufferTypesCounter[DeviceBufferType::PrimInfo] = FRAMES_TOTAL;
	if (bufferType.modelMatrix)
		_deviceBufferTypesCounter[DeviceBufferType::Model] = FRAMES_TOTAL;
	if (bufferType.index)
		_deviceBufferTypesCounter[DeviceBufferType::Index] = FRAMES_TOTAL;
	if (bufferType.vertex)
		_deviceBufferTypesCounter[DeviceBufferType::Vertex] = FRAMES_TOTAL;
	if (bufferType.material)
		_deviceBufferTypesCounter[DeviceBufferType::Material] = FRAMES_TOTAL;
	if (bufferType.texture)
		_deviceBufferTypesCounter[DeviceBufferType::Texture] = FRAMES_TOTAL;
	if (bufferType.light)
		_deviceBufferTypesCounter[DeviceBufferType::Light] = FRAMES_TOTAL;
}

void RenderSystem::updateSignaledDeviceBuffers(const GraphicsDataPayload& payload) {
	DeviceBufferTypeFlags dataType;
	//FIgure out which render data type flags were signaled
	if (_deviceBufferTypesCounter[DeviceBufferType::ViewProj] == FRAMES_TOTAL)
		dataType.viewProjMatrix = true;
	if (_deviceBufferTypesCounter[DeviceBufferType::Indirect] == FRAMES_TOTAL)
		dataType.indirectDraw = true;
	if (_deviceBufferTypesCounter[DeviceBufferType::PrimID] == FRAMES_TOTAL)
		dataType.primID = true;
	if (_deviceBufferTypesCounter[DeviceBufferType::PrimInfo] == FRAMES_TOTAL)
		dataType.primInfo = true;
	if (_deviceBufferTypesCounter[DeviceBufferType::Model] == FRAMES_TOTAL)
		dataType.modelMatrix = true;
	if (_deviceBufferTypesCounter[DeviceBufferType::Index] == FRAMES_TOTAL)
		dataType.index = true;
	if (_deviceBufferTypesCounter[DeviceBufferType::Vertex] == FRAMES_TOTAL)
		dataType.vertex = true;
	if (_deviceBufferTypesCounter[DeviceBufferType::Material] == FRAMES_TOTAL)
		dataType.material = true;
	if (_deviceBufferTypesCounter[DeviceBufferType::Texture] == FRAMES_TOTAL)
		dataType.texture = true;
	if (_deviceBufferTypesCounter[DeviceBufferType::Light] == FRAMES_TOTAL)
		dataType.light = true;

	//Stage Data of those that were only recently signaled to be updated
	extract_render_data(payload, dataType, _stagingUpdateData);

	//Updatae Buffers
	if (_deviceBufferTypesCounter[DeviceBufferType::ViewProj] > 0) {
		size_t viewSize = sizeof(RenderShader::ViewProj);
		_vkContext.update_buffer(get_current_frame().drawContext.viewprojMatrixBuffer, &_stagingUpdateData.viewproj, viewSize, _stagingUpdateData.viewprojMatrix_copy_info);
		_deviceBufferTypesCounter[DeviceBufferType::ViewProj]--;
	}

	if (_deviceBufferTypesCounter[DeviceBufferType::Indirect] > 0) {
		get_current_frame().drawContext.drawCount = _stagingUpdateData.indirect_commands.size();
		size_t indirectSize = sizeof(VkDrawIndexedIndirectCommand) * _stagingUpdateData.indirect_commands.size();
		_vkContext.update_buffer(get_current_frame().drawContext.indirectDrawCommandsBuffer, _stagingUpdateData.indirect_commands.data(), indirectSize, _stagingUpdateData.indirect_copy_info);
		_deviceBufferTypesCounter[DeviceBufferType::Indirect]--;
	}
	
	if (_deviceBufferTypesCounter[DeviceBufferType::PrimID] > 0) {
		size_t primIDsize = sizeof(int32_t) * _stagingUpdateData.primitiveIds.size();
		_vkContext.update_buffer(get_current_frame().drawContext.primitiveIdsBuffer, _stagingUpdateData.primitiveIds.data(), primIDsize, _stagingUpdateData.primId_copy_info);
		_deviceBufferTypesCounter[DeviceBufferType::PrimID]--;
	}

	if (_deviceBufferTypesCounter[DeviceBufferType::PrimInfo] > 0) {
		size_t primInfoSize = sizeof(RenderShader::PrimitiveInfo) * _stagingUpdateData.primitiveInfos.size();
		_vkContext.update_buffer(get_current_frame().drawContext.primitiveInfosBuffer, _stagingUpdateData.primitiveInfos.data(), primInfoSize, _stagingUpdateData.primInfo_copy_infos);
		_deviceBufferTypesCounter[DeviceBufferType::PrimInfo]--;
	}

	if (_deviceBufferTypesCounter[DeviceBufferType::Model] > 0) {
		size_t modetSize = sizeof(glm::mat4) * _stagingUpdateData.model_matrices.size();
		_vkContext.update_buffer(get_current_frame().drawContext.modelMatricesBuffer, _stagingUpdateData.model_matrices.data(), modetSize, _stagingUpdateData.modelMatrices_copy_infos);
		_deviceBufferTypesCounter[DeviceBufferType::Model]--;
	}

	if (_deviceBufferTypesCounter[DeviceBufferType::Index] > 0) {
		size_t indiceSize = sizeof(uint32_t) * _stagingUpdateData.indices.size();
		_vkContext.update_buffer(get_current_frame().drawContext.indexBuffer, _stagingUpdateData.indices.data(), indiceSize, _stagingUpdateData.index_copy_info);
		_deviceBufferTypesCounter[DeviceBufferType::Index]--;
	}

	if (_deviceBufferTypesCounter[DeviceBufferType::Vertex] > 0) {
		size_t vertexPosSize = sizeof(glm::vec3) * _stagingUpdateData.positions.size();
		size_t vertexAttribSize = sizeof(RenderShader::VertexAttributes) * _stagingUpdateData.attributes.size();
		_vkContext.update_buffer(get_current_frame().drawContext.vertexPosBuffer, _stagingUpdateData.positions.data(), vertexPosSize, _stagingUpdateData.pos_copy_info);
		_vkContext.update_buffer(get_current_frame().drawContext.vertexOtherAttribBuffer, _stagingUpdateData.attributes.data(), vertexAttribSize, _stagingUpdateData.attrib_copy_info);
		_deviceBufferTypesCounter[DeviceBufferType::Vertex]--;
	}

	if (_deviceBufferTypesCounter[DeviceBufferType::Material] > 0) {
		size_t matSize = sizeof(RenderShader::Material) * _stagingUpdateData.materials.size();
		_vkContext.update_buffer(get_current_frame().drawContext.materialsBuffer, _stagingUpdateData.materials.data(), matSize, _stagingUpdateData.material_copy_infos);
		_deviceBufferTypesCounter[DeviceBufferType::Material]--;
	}

	if (_deviceBufferTypesCounter[DeviceBufferType::Texture] > 0) {
		size_t textureSize = sizeof(RenderShader::Texture) * _stagingUpdateData.textures.size();
		_vkContext.update_buffer(get_current_frame().drawContext.texturesBuffer, _stagingUpdateData.textures.data(), textureSize, _stagingUpdateData.texture_copy_infos);
		_deviceBufferTypesCounter[DeviceBufferType::Texture]--;
	}

	if (_deviceBufferTypesCounter[DeviceBufferType::Light] > 0) {
		size_t lightSize = sizeof(RenderShader::Lights);
		RenderShader::Lights lights;
		lights.pointLightCount = _stagingUpdateData.pointLightsCount;
		std::copy(_stagingUpdateData.pointLights.begin(), _stagingUpdateData.pointLights.end(), lights.pointLights);
		_vkContext.update_buffer(get_current_frame().drawContext.lightsBuffer, (void*)&lights, lightSize, _stagingUpdateData.light_copy_info);
		_deviceBufferTypesCounter[DeviceBufferType::Light]--;
	}
}

void RenderSystem::init_graphicsPipeline() {
	//Load SHaders
	VkShaderModule vertexShader;
	if (!vkutil::load_shader_module("shaders/default_vert.spv", _vkContext.device, &vertexShader))
		throw std::runtime_error("Error trying to create Vertex Shader Module");
	else
		std::cout << "Vertex Shader successfully loaded" << std::endl;

	VkShaderModule fragShader;
	if (!vkutil::load_shader_module("shaders/default_frag.spv", _vkContext.device, &fragShader))
		throw std::runtime_error("error trying to create Frag Shader Module");
	else
		std::cout << "Fragment Shader successfully loaded" << std::endl;

	//Set Pipeline Layout - Descriptor Sets and Push Constants Layout
	VkPushConstantRange range{};
	range.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
	range.offset = 0;
	range.size = 56; //REMEMBER TO CHANGE THIS WHEN ADDING MORE BUFFERS/PUSH CONSTANTS

	VkPipelineLayoutCreateInfo pipeline_layout_info = vkutil::pipeline_layout_create_info();
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &_descriptorSetLayout;
	pipeline_layout_info.pushConstantRangeCount = 1;
	pipeline_layout_info.pPushConstantRanges = &range;

	VK_CHECK(vkCreatePipelineLayout(_vkContext.device, &pipeline_layout_info, nullptr, &_pipelineLayout));

	//Build Pipeline
	PipelineBuilder pipelineBuilder;
	pipelineBuilder._pipelineLayout = _pipelineLayout;
	pipelineBuilder.set_shaders(vertexShader, fragShader);
	pipelineBuilder.set_vertex_input(_bindingDescriptions, _attribueDescriptions);
	pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	pipelineBuilder.set_multisampling_none();
	pipelineBuilder.disable_blending();
	pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
	pipelineBuilder.set_color_attachment_format(_swapchain.format);
	pipelineBuilder.set_depth_format(VK_FORMAT_D32_SFLOAT);

	_pipeline = pipelineBuilder.build_pipeline(_vkContext.device);

	vkDestroyShaderModule(_vkContext.device, vertexShader, nullptr);
	vkDestroyShaderModule(_vkContext.device, fragShader, nullptr);
}

VkResult RenderSystem::draw() {
	VK_CHECK(vkWaitForFences(_vkContext.device, 1, &get_current_frame().renderFence, true, 1000000000));

	//Acquire the next swapchain image
	VkResult result;
	result = vkAcquireNextImageKHR(_vkContext.device, _swapchain.vkSwapchain, 1000000000, get_current_frame().swapchainSemaphore, nullptr, &_swapchainImageIndex);
	
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
	{
		std::cerr << "Render System: Failed to acquire swapchain image" << std::endl;
		return result;
	}

	VK_CHECK(vkResetFences(_vkContext.device, 1, &get_current_frame().renderFence));

	VkCommandBuffer cmd = get_current_frame().commandBuffer;

	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
	Image swapchainImage = get_currentSwapchainImage();

	//Transition Images for Drawing
	vkutil::transition_image(cmd, swapchainImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	vkutil::transition_image(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	//Clear Color
	VkClearColorValue clearValue = { {0.5f, 0.5f, 0.5f, 0.5f} };

	VkImageSubresourceRange clearRange = vkutil::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	vkCmdClearColorImage(cmd, swapchainImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

	//Draw
	draw_geometry(cmd, swapchainImage);

	//Draw GUI
	draw_gui(cmd, swapchainImage);

	//Transition for Presentation
	vkutil::transition_image(cmd, swapchainImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = vkutil::command_buffer_submit_info(cmd);
	VkSemaphoreSubmitInfo waitInfo = vkutil::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame().swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkutil::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame().renderSemaphore);

	VkSubmitInfo2 submit = vkutil::submit_info(&cmdInfo, &signalInfo, &waitInfo);

	VK_CHECK(vkQueueSubmit2(_vkContext.graphicsQueue, 1, &submit, get_current_frame().renderFence));

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &_swapchain.vkSwapchain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &get_current_frame().renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pImageIndices = &_swapchainImageIndex;

	result = vkQueuePresentKHR(_vkContext.graphicsQueue, &presentInfo);

	go_next_frame();
	
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		std::cerr << "Render System: Failed to present" << std::endl;
	}

	return result;
}

void RenderSystem::draw_geometry(VkCommandBuffer cmd, const Image& swapchainImage) {
	VkRenderingAttachmentInfo colorAttachment = vkutil::attachment_info(swapchainImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo depthAttachment = vkutil::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkExtent2D swapchainExtent = get_swapChainExtent();

	VkRenderingInfo renderInfo = vkutil::rendering_info(swapchainExtent, &colorAttachment, &depthAttachment);
	vkCmdBeginRendering(cmd, &renderInfo);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);

	//Set Dynamic States
	VkViewport viewport{};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = swapchainExtent.width;
	viewport.height = swapchainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = swapchainExtent.width;
	scissor.extent.height = swapchainExtent.height;

	vkCmdSetScissor(cmd, 0, 1, &scissor);

	//Bind Descriptor Set
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &_descriptorSet, 0, nullptr);

	//Bind Vertex Input Buffers
	std::vector<VkBuffer> vertexBuffers = get_vertexBuffers();
	std::vector<VkDeviceSize> vertexOffsets = { 0, 0 };
	vkCmdBindVertexBuffers(cmd, 0, vertexBuffers.size(), vertexBuffers.data(), vertexOffsets.data());
	vkCmdBindIndexBuffer(cmd, get_indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

	//Push Constants
	RenderShader::PushConstants pushconstants = get_pushConstants();
	vkCmdPushConstants(cmd, _pipelineLayout, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof(RenderShader::PushConstants), &pushconstants);

	//Draw
	vkCmdDrawIndexedIndirect(cmd, get_indirectDrawBuffer(), 0, get_drawCount(), sizeof(VkDrawIndexedIndirectCommand));

	vkCmdEndRendering(cmd);
}

void RenderSystem::draw_gui(VkCommandBuffer cmd, const Image& swapchainImage) {
	VkRenderingAttachmentInfo colorAttachment = vkutil::attachment_info(swapchainImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = vkutil::rendering_info(get_swapChainExtent(), &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

void RenderSystem::resize_swapchain(VkExtent2D windowExtent) {
	vkDeviceWaitIdle(_vkContext.device);
	destroy_swapchain();
	init_swapchain(windowExtent);
	//Recreate DepthImage with new adjusted Swapchain size
	_vkContext.destroy_image(_depthImage);
	setup_depthImage();
}

void RenderSystem::bind_descriptors(GraphicsDataPayload& payload) {
	std::vector<VkWriteDescriptorSet> descriptorWrites(2);

	std::vector<VkDescriptorImageInfo> sampledImages_imgInfos;
	for (auto& image : payload.images) {
		VkDescriptorImageInfo imgInfo{};
		imgInfo.imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
		imgInfo.imageView = image.imageView;
		sampledImages_imgInfos.push_back(imgInfo);
	}

	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = _descriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	descriptorWrites[0].descriptorCount = sampledImages_imgInfos.size();
	descriptorWrites[0].pImageInfo = sampledImages_imgInfos.data();

	std::vector<VkDescriptorImageInfo> sampler_imgInfos;
	for (auto& sampler : payload.samplers) {
		VkDescriptorImageInfo imgInfo{};
		imgInfo.sampler = sampler;
		sampler_imgInfos.push_back(imgInfo);
	}

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = _descriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	descriptorWrites[1].descriptorCount = sampler_imgInfos.size();
	descriptorWrites[1].pImageInfo = sampler_imgInfos.data();

	vkUpdateDescriptorSets(_vkContext.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
}

void RenderSystem::setup_depthImage() {
	VkExtent2D swapchainExtent = get_swapChainExtent();
	VkExtent3D depthExtent;
	depthExtent.width = swapchainExtent.width;
	depthExtent.height = swapchainExtent.height;
	depthExtent.depth = 1;
	_depthImage = _vkContext.create_image("Depth Image", depthExtent, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, false);
}

void RenderSystem::destroy_swapchain() {
	vkDestroySwapchainKHR(_vkContext.device, _swapchain.vkSwapchain, nullptr);

	for (int i = 0; i < _swapchain.images.size(); i++)
		vkDestroyImageView(_vkContext.device, _swapchain.images[i].imageView, nullptr);
}

//Extract data from Payload to RenderShaderData
void RenderSystem::extract_render_data(const GraphicsDataPayload& payload, DeviceBufferTypeFlags dataType, RenderShaderData& data) {
	//View and Proj Matrix and Camera Pos
	if (dataType.viewProjMatrix) {
		data.viewproj.view = payload.camera_transform;
		data.viewproj.proj = payload.proj_transform;
		data.viewproj.pos = payload.cam_pos;
		data.viewprojMatrix_copy_info = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(glm::mat4) * 2 };
	}

	if (dataType.light) {
		uint32_t pointLightsCount = 0;
		auto pointLight_it = data.pointLights.begin();
		for (const PointLight& pointLight : payload.pointLights) {
			*pointLight_it = { .pos = pointLight.pos, .color = pointLight.color, .power = pointLight.power };
			pointLightsCount++;
			pointLight_it++;
		}
		data.pointLightsCount = pointLightsCount;
		data.light_copy_info = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(RenderShader::Lights) };
	}

	//Navigate each scene, and each scene's root nodes, and through each root node's children
	if (dataType.modelMatrix || dataType.indirectDraw || dataType.primID || dataType.vertex || dataType.index || dataType.primInfo) {
		//Clear any specified data from RenderShaderData parameter
		if (dataType.modelMatrix) {
			data.model_matrices.clear();
			data.modelMatrices_copy_infos.clear();
		}

		if (dataType.indirectDraw) {
			_primID_to_drawCmd.clear();
			data.indirect_commands.clear();
		}

		if (dataType.primID) {
			data.primitiveIds.clear();
		}

		if (dataType.vertex) {
			data.positions.clear();
			data.attributes.clear();
		}
		
		if (dataType.index) {
			data.indices.clear();
		}

		if (dataType.primInfo) {
			data.primitiveInfos.clear();
			data.primInfo_copy_infos.clear();
		}

		//Extract Data from Current Scene (only)
		const Scene& scene = payload.scenes[payload.current_scene_idx];
		//Add all root nodes in current scene to Stack
		std::stack<std::shared_ptr<Node>> dfs_node_stack;
		for (auto root_node : scene.root_nodes) {
			dfs_node_stack.push(root_node);
		}
		//Perform DFS, traverse all Nodes
		while (!dfs_node_stack.empty()) {
			std::shared_ptr<Node> node = dfs_node_stack.top();
			dfs_node_stack.pop();

			//Add currentNode's children to Stack
			for (std::shared_ptr<Node> child_node : node->child_nodes) {
				dfs_node_stack.push(child_node);
			}

			//Model Matrices
			if (dataType.modelMatrix) {
				data.modelMatrices_copy_infos.push_back({ .srcOffset = data.model_matrices.size() * sizeof(glm::mat4), .dstOffset = node->getID() * sizeof(glm::mat4), .size = sizeof(glm::mat4) });
				data.model_matrices.push_back(node->get_WorldTransform());
			}

			//Check if Node represents Mesh
			if (node->mesh != nullptr) {
				std::shared_ptr<Mesh>& currentMesh = node->mesh;

				//Iterate through it's primitives and add their data to 
				for (Mesh::Primitive primitive : currentMesh->primitives) {
					//Indirect Draw Command
					if (dataType.indirectDraw) {
						VkDrawIndexedIndirectCommand indirect_command{};
						indirect_command.firstIndex = data.indices.size();
						indirect_command.indexCount = primitive.indices.size();
						indirect_command.vertexOffset = data.positions.size();
						indirect_command.firstInstance = 0;
						indirect_command.instanceCount = 1;

						_primID_to_drawCmd[primitive.getID()] = indirect_command; //Add to primID mapping structure

						data.indirect_commands.push_back(indirect_command);

						//Primitive Ids
						if (dataType.primID) {
							data.primitiveIds.push_back(primitive.getID());
						}
					}

					//Vertex's Position and other Vertex Attributes
					if (dataType.vertex) {
						for (Mesh::Primitive::Vertex vertex : primitive.vertices) {
							RenderShader::VertexAttributes attribute;
							attribute.normal = vertex.normal;
							attribute.tangent = vertex.tangent;
							if (vertex.colors.empty())
								attribute.color = glm::vec3(1.0f, 1.0f, 1.0f);
							else
								attribute.color = vertex.colors[0]; //Get Color_0
							if (vertex.uvs.empty())
								attribute.uv = glm::vec2(-1.0f, -1.0f);
							else
								attribute.uv = vertex.uvs[0]; //Get TexCoord_0

							data.positions.push_back(vertex.position);
							data.attributes.push_back(attribute);
						}
					}

					//Indices
					if (dataType.index) {
						data.indices.insert(data.indices.end(), primitive.indices.begin(), primitive.indices.end());
					}

					//PrimitiveInfo
					if (dataType.primInfo) {
						data.primInfo_copy_infos.push_back({ .srcOffset = data.primitiveInfos.size() * sizeof(RenderShader::PrimitiveInfo), .dstOffset = primitive.getID() * sizeof(RenderShader::PrimitiveInfo), .size = sizeof(RenderShader::PrimitiveInfo) });
						RenderShader::PrimitiveInfo prmInfo{};
						if (primitive.material.expired() != true)
							prmInfo.mat_id = primitive.material.lock()->getID();
						else
							prmInfo.mat_id = 0;
						prmInfo.model_matrix_id = node->getID();
						data.primitiveInfos.push_back(prmInfo);
					}
				}
			}
		}

		//Add Copy Infos for data that is not added to specific id locations (but just as lists).
		if (dataType.vertex) {
			data.pos_copy_info = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(glm::vec3) * data.positions.size() };
			data.attrib_copy_info = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(RenderShader::VertexAttributes) * data.attributes.size() };
		}
		if (dataType.index)
			data.index_copy_info = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(uint32_t) * data.indices.size() };
		if (dataType.indirectDraw)
			data.indirect_copy_info = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(VkDrawIndexedIndirectCommand) * data.indirect_commands.size() };
		if (dataType.primID)
			data.primId_copy_info = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(int32_t) * data.primitiveIds.size() };
	}

	//Get Materials
	if (dataType.material) {
		data.materials.clear();
		data.material_copy_infos.clear();

		for (std::shared_ptr<Material> material : payload.materials) {
			data.material_copy_infos.push_back({ .srcOffset = data.materials.size() * sizeof(RenderShader::Material), .dstOffset = material->getID() * sizeof(RenderShader::Material), .size = sizeof(RenderShader::Material) });
			RenderShader::Material mat{};

			//Base Color
			if (material->baseColor_texture.expired() != true)
				mat.baseColor_texture_id = material->baseColor_texture.lock()->getID();
			else
				mat.baseColor_texture_id = -1;
			mat.baseColor_texCoord_id = material->baseColor_coord_index;
			mat.baseColor_factor = material->baseColor_Factor;

			//Normal
			if (material->normal_texture.expired() != true)
				mat.normal_texture_id = material->normal_texture.lock()->getID();
			else
				mat.normal_texture_id = -1;
			mat.normal_texcoord_id = material->normal_coord_index;
			mat.normal_scale = material->normal_scale;

			//Metal-Rough
			if (material->metal_rough_texture.expired() != true)
				mat.metal_rough_texture_id = material->metal_rough_texture.lock()->getID();
			else
				mat.metal_rough_texture_id = -1;
			mat.metal_rough_texcoord_id = material->metal_rough_coord_index;
			mat.metallic_factor = material->metallic_Factor;
			mat.roughness_factor = material->roughness_Factor;

			//Occlusion
			if (material->occlusion_texture.expired() != true)
				mat.occlusion_texture_id = material->occlusion_texture.lock()->getID();
			else
				mat.occlusion_texture_id = -1;
			mat.occlusion_texcoord_id = material->occlusion_coord_index;
			mat.occlusion_strength = material->occlusion_strength;

			//Emission
			if (material->emission_texture.expired() != true)
				mat.emission_texture_id = material->emission_texture.lock()->getID();
			else
				mat.emission_texture_id = -1;
			mat.emission_texcoord_id = material->emission_coord_index;
			mat.emission_factor = material->emission_Factor;

			data.materials.push_back(mat);
		}
	}

	//Get Textures
	if (dataType.texture) {
		data.textures.clear();
		data.texture_copy_infos.clear();

		for (auto& texture : payload.textures) {
			data.texture_copy_infos.push_back({ .srcOffset = data.textures.size() * sizeof(RenderShader::Texture), .dstOffset = texture->getID() * sizeof(RenderShader::Texture), .size = sizeof(RenderShader::Texture) });
			RenderShader::Texture tex{};
			tex.textureImage_id = texture->image_index;
			tex.sampler_id = texture->sampler_index;
			data.textures.push_back(tex);
		}
	}
}

//Hold all the setup code for creating the pipelines and rendering to create the hdrMap
void RenderSystem::setup_hdrMap() {
	//Temp have file loading here. Might move loading code to loader.h/loader.cpp. And have only engine class actually initiate the load and pass the data to renderSystem.
	//Load HDR Equirectangular Image
	int width, height, nrChannels;

	float* data = stbi_loadf("C:\\Github\\vulkan_engine\\vulkan_engine\\assets\\HDR_Maps\\alps_field_4k.hdr", &width, &height, &nrChannels, 0);

	if (data) {
		VkExtent3D imageSize;
		imageSize.width = width;
		imageSize.height = height;
		imageSize.depth = 1;
		_hdrImage = _vkContext.create_image("HDR Image", imageSize, VK_FORMAT_R16G16B16_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false);
		_vkContext.update_image(_hdrImage, data, imageSize);

		stbi_image_free(data);
	}
	else
		std::cout << "failed to load HDR Image File" << std::endl;

	//Setup Image CubeMap
	VkImageCreateInfo imgInfo;
	imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.pNext = nullptr;
	imgInfo.imageType = VK_IMAGE_TYPE_2D;
	imgInfo.format = VK_FORMAT_R8G8B8_UNORM;
	imgInfo.extent = { .width = 512, .height = 512, .depth = 1 };
	imgInfo.mipLevels = 1;
	imgInfo.arrayLayers = 6;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imgInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	VmaAllocationCreateInfo allocInfo;
	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkImageViewCreateInfo imgViewInfo;
	imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imgViewInfo.pNext = nullptr;
	imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	imgViewInfo.format = VK_FORMAT_R8G8B8_UNORM;
	imgViewInfo.subresourceRange.baseMipLevel = 0;
	imgViewInfo.subresourceRange.levelCount = 1;
	imgViewInfo.subresourceRange.baseArrayLayer = 0;
	imgViewInfo.subresourceRange.layerCount = 6;
	imgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	_hdrCubeMap = _vkContext.create_image("HDR CubeMap", imgInfo, allocInfo, imgViewInfo);

	//Setup Shaders and Pipeline to Transform Equirectangular to Cubemap
	//-Vertex Input Bindings
	std::vector<VkVertexInputBindingDescription> cubeMap_vertexInputBindings = { {} };

	cubeMap_vertexInputBindings[0].binding = 0;
	cubeMap_vertexInputBindings[0].stride = sizeof(glm::vec3);
	cubeMap_vertexInputBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	//-Vertex Input Attribute
	std::vector<VkVertexInputAttributeDescription> cubeMap_vertexInputAttributes = { {} };

	//--Position
	cubeMap_vertexInputAttributes[0].binding = 0;
	cubeMap_vertexInputAttributes[0].location = 0;
	cubeMap_vertexInputAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	cubeMap_vertexInputAttributes[0].offset = 0;

	//-Descriptor Pool and Set
	VkDescriptorPool cubeMap_descriptorPool;
	VkDescriptorSetLayout cubeMap_descriptorSetLayout;
	VkDescriptorSet cubeMap_descriptorSet;

	//--Create Descriptor Pool
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 },
		{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 }
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 1;

	if (vkCreateDescriptorPool(_vkContext.device, &poolInfo, nullptr, &cubeMap_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create CubeMap Descriptor Pool");

	//--Set Layout Bindings
	std::vector<VkDescriptorSetLayoutBinding> layout_bindings = {
		{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .pImmutableSamplers = nullptr },
		{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr }
	};

	//--Set Binding Flags
	std::vector<VkDescriptorBindingFlags> binding_flags = {};

	VkDescriptorSetLayoutBindingFlagsCreateInfo set_binding_flags{};
	set_binding_flags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	set_binding_flags.bindingCount = static_cast<uint32_t>(binding_flags.size());
	set_binding_flags.pBindingFlags = binding_flags.data();

	//--Now Create Descriptor Set Layout
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext = &set_binding_flags;
	layoutInfo.bindingCount = static_cast<uint32_t>(layout_bindings.size());
	layoutInfo.pBindings = layout_bindings.data();

	if (vkCreateDescriptorSetLayout(_vkContext.device, &layoutInfo, nullptr, &cubeMap_descriptorSetLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to Create Cubemap Descriptor Set Layout");

	//--Create Descriptor Set
	VkDescriptorSetAllocateInfo descriptorSetallocInfo{};
	descriptorSetallocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetallocInfo.descriptorPool = cubeMap_descriptorPool;
	descriptorSetallocInfo.descriptorSetCount = 1;
	descriptorSetallocInfo.pSetLayouts = &cubeMap_descriptorSetLayout;

	if (vkAllocateDescriptorSets(_vkContext.device, &descriptorSetallocInfo, &cubeMap_descriptorSet) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate Cubemap Descriptor Set");

	//-Pipeline
	VkPipelineLayout cubeMap_pipelineLayout;
	VkPipeline cubeMap_pipeline;

	//--Load SHaders
	VkShaderModule vertexShader;
	if (!vkutil::load_shader_module("shaders/_", _vkContext.device, &vertexShader))
		throw std::runtime_error("Error trying to create Cube Map Vertex Shader Module");
	else
		std::cout << "Cube Map Vertex Shader successfully loaded" << std::endl;

	VkShaderModule fragShader;
	if (!vkutil::load_shader_module("shaders/_", _vkContext.device, &fragShader))
		throw std::runtime_error("error trying to create Cube Map Frag Shader Module");
	else
		std::cout << "Cube Map Fragment Shader successfully loaded" << std::endl;

	//--Set Pipeline Layout - Descriptor Set
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkutil::pipeline_layout_create_info();
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &cubeMap_descriptorSetLayout;
	pipeline_layout_info.pushConstantRangeCount = 0;
	pipeline_layout_info.pPushConstantRanges = nullptr;

	VK_CHECK(vkCreatePipelineLayout(_vkContext.device, &pipeline_layout_info, nullptr, &cubeMap_pipelineLayout));

	//--Build Pipeline
	PipelineBuilder pipelineBuilder;
	pipelineBuilder._pipelineLayout = cubeMap_pipelineLayout;
	pipelineBuilder.set_shaders(vertexShader, fragShader);
	pipelineBuilder.set_vertex_input(cubeMap_vertexInputBindings, cubeMap_vertexInputAttributes);
	pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	pipelineBuilder.set_multisampling_none();
	pipelineBuilder.disable_blending();
	pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);
	pipelineBuilder.set_color_attachment_format(_);
	pipelineBuilder.set_depth_format(VK_FORMAT_D32_SFLOAT);

	cubeMap_pipeline = pipelineBuilder.build_pipeline(_vkContext.device);

	vkDestroyShaderModule(_vkContext.device, vertexShader, nullptr);
	vkDestroyShaderModule(_vkContext.device, fragShader, nullptr);

	//Setup Neccesary Buffers for the Rendering CubeMap
	AllocatedBuffer cubeMap_vertexBuffer;
	AllocatedBuffer cubeMap_indexBuffer;
	AllocatedBuffer cubeMap_uniformBuffer;
	AllocatedImage cubeMap_frameBufferImage; //To Render to

	std::vector<glm::vec3> unitCube_vertices = {
		// Front face
		{-1.0f, -1.0f, 1.0f},  // 0: bottom-left-front
		{1.0f, -1.0f, 1.0f},   // 1: bottom-right-front
		{1.0f, 1.0f, 1.0f},    // 2: top-right-front
		{-1.0f, 1.0f, 1.0f},   // 3: top-left-front

		// Back face
		{-1.0f, -1.0f, -1.0f}, // 4: bottom-left-back
		{1.0f, -1.0f, -1.0f},  // 5: bottom-right-back
		{1.0f, 1.0f, -1.0f},   // 6: top-right-back
		{-1.0f, 1.0f, -1.0f}   // 7: top-left-back
	};
	std::vector<uint32_t> unitCube_indices = {
		// Front face
		0, 1, 2, 0, 2, 3,

		// Right face
		1, 5, 6, 1, 6, 2,

		// Back face
		5, 4, 7, 5, 7, 6,

		// Left face
		4, 0, 3, 4, 3, 7,

		// Top face
		3, 2, 6, 3, 6, 7,

		// Bottom face
		4, 5, 1, 4, 1, 0
	};

	VmaAllocationCreateFlags allocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
	cubeMap_vertexBuffer = _vkContext.create_buffer("CubeMap Vertex Buffer", unitCube_vertices.size() * sizeof(glm::vec3), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
	cubeMap_indexBuffer = _vkContext.create_buffer("CubeMap Index Buffer", unitCube_indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
	cubeMap_uniformBuffer = _vkContext.create_buffer("CubeMap Uniform Buffer", sizeof(glm::mat4) * 2, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
	cubeMap_frameBufferImage = _vkContext.create_image("CubeMap Frame Buffer Image", { .width = 512, .height = 512, .depth = 1 }, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, false);

	VkBufferCopy bufferCpy = { .srcOffset = 0, .dstOffset = 0, .size = unitCube_vertices.size() * sizeof(glm::vec3) };
	_vkContext.update_buffer(cubeMap_vertexBuffer, unitCube_vertices.data(), unitCube_vertices.size() * sizeof(glm::vec3), bufferCpy);
	bufferCpy.size = unitCube_indices.size() * sizeof(uint32_t);
	_vkContext.update_buffer(cubeMap_indexBuffer, unitCube_indices.data(), unitCube_indices.size() * sizeof(uint32_t), bufferCpy);

	//Create Descriptors
	std::vector<VkWriteDescriptorSet> descriptorWrites(2);

	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = cubeMap_descriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].pBufferInfo = _;

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = cubeMap_descriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[1].descriptorCount = 1;
	descriptorWrites[1].pImageInfo = _;

	vkUpdateDescriptorSets(_vkContext.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	//Setup Commands and Syncing
	VkCommandPool cubeMap_commandPool;
	VkCommandBuffer cubeMap_commandBuffer;
	VkFence cubeMap_renderFence; //Indicates that a render has finished

	VkCommandPoolCreateInfo cmdPoolInfo{};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.pNext = nullptr;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cmdPoolInfo.queueFamilyIndex = _vkContext.graphicsQueueFamily;

	VkFenceCreateInfo fenceCreateInfo{};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VK_CHECK(vkCreateCommandPool(_vkContext.device, &cmdPoolInfo, nullptr, &cubeMap_commandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo{};
	cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAllocInfo.pNext = nullptr;
	cmdAllocInfo.commandPool = cubeMap_commandPool;
	cmdAllocInfo.commandBufferCount = 1;
	cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VK_CHECK(vkAllocateCommandBuffers(_vkContext.device, &cmdAllocInfo, &cubeMap_commandBuffer));

	VK_CHECK(vkCreateFence(_vkContext.device, &fenceCreateInfo, nullptr, &cubeMap_renderFence));

	//Render to create Cubemap
	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(cubeMap_commandBuffer, &cmdBeginInfo));

	//-Transition Images for Drawing
	vkutil::transition_image(cubeMap_commandBuffer, _, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	//-Clear Color
	VkClearColorValue clearValue = { {0.5f, 0.5f, 0.5f, 0.5f} };

	VkImageSubresourceRange clearRange = vkutil::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	vkCmdClearColorImage(cubeMap_commandBuffer, _, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

	//-Draw
	VkRenderingAttachmentInfo colorAttachment = vkutil::attachment_info(_, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	VkExtent2D frameBufferExtent = { .width = 512, .height = 512 };

	VkRenderingInfo renderInfo = vkutil::rendering_info(frameBufferExtent, &colorAttachment, nullptr);

	vkCmdBeginRendering(cubeMap_commandBuffer, &renderInfo);

	vkCmdBindPipeline(cubeMap_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cubeMap_pipeline);

	//-Set Dynamic States
	VkViewport viewport{};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = frameBufferExtent.width;
	viewport.height = frameBufferExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	vkCmdSetViewport(cubeMap_commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = frameBufferExtent.width;
	scissor.extent.height = frameBufferExtent.height;

	vkCmdSetScissor(cubeMap_commandBuffer, 0, 1, &scissor);

	//-Bind Descriptor Set
	vkCmdBindDescriptorSets(cubeMap_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cubeMap_pipelineLayout, 0, 1, &cubeMap_descriptorSet, 0, nullptr);

	//-Bind Vertex Input Buffers
	std::vector<VkDeviceSize> vertexOffsets = { 0 };
	vkCmdBindVertexBuffers(cubeMap_commandBuffer, 0, _, _, vertexOffsets.data());
	vkCmdBindIndexBuffer(cubeMap_commandBuffer, _, _, _);

	//-Draw
	vkCmdDrawIndexed(cubeMap_commandBuffer, _, _, _, _, _);

	vkCmdEndRendering(cubeMap_commandBuffer);

	//-Submit to Queue
	vkutil::transition_image(cubeMap_commandBuffer, _, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(cubeMap_commandBuffer));

	VkCommandBufferSubmitInfo cmdInfo = vkutil::command_buffer_submit_info(cubeMap_commandBuffer);

	VkSubmitInfo2 submit = vkutil::submit_info(&cmdInfo, nullptr, nullptr);

	VK_CHECK(vkQueueSubmit2(_vkContext.graphicsQueue, 1, &submit, cubeMap_renderFence));

	//Dont forget to delete all objects after finishing the rendering of cubemap!!!
}

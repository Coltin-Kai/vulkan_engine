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

	setup_hdrMap2();
	setup_skybox();
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
	//HDR Cubemap + Skybox
	_vkContext.destroy_buffer(_skyboxIndexBuffer);
	_vkContext.destroy_buffer(_skyboxVertexBuffer);
	vkDestroyPipeline(_vkContext.device, _skyboxPipeline, nullptr);
	vkDestroyPipelineLayout(_vkContext.device, _skyboxPipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(_vkContext.device, _skyboxDescriptorSetLayout, nullptr);
	vkDestroyDescriptorPool(_vkContext.device, _skyboxDescriptorPool, nullptr);
	_vkContext.destroy_sampler(_cubemapSampler);
	_vkContext.destroy_image(_hdrSpecularLUT);
	_vkContext.destroy_image(_hdrSpecularCubeMap);
	_vkContext.destroy_image(_hdrIrradianceCubeMap);
	_vkContext.destroy_image(_hdrCubeMap);

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
		_vkContext.destroy_buffer(frame.drawContext.skybox_viewprojMatrixBuffer);
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
	cmdPoolInfo.queueFamilyIndex = _vkContext.primaryQueueFamily;

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
	const size_t buffer_size = 40000000; //Huge size for dynamic data buffers
	
	//Note: Shouldnt really be named alloc for most of them since the dnyamic data buffers will use the buffer_size variable for alloc a size and just use these variables for uploading initial data
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
	size_t alloc_skyboxViewprojMatrix_size = sizeof(SkyboxShader::ViewTransformMatrices);

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
		currentDrawContext.viewprojMatrixBuffer = _vkContext.create_buffer(std::format("View and Projection Matrix Buffer {}", i).c_str(), buffer_size, storageUsageFlags, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags); //Careful as if the alloc size is 0. Will cause errors. Also Note: Some of these buffers are not dynamic, thus dont need to use buffer_size variable
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
		
		//Uniform Buffers - Skybox
		currentDrawContext.skybox_viewprojMatrixBuffer = _vkContext.create_buffer(std::format("Skybox View and Projection Matrix Buffer {}", i).c_str(), sizeof(SkyboxShader::ViewTransformMatrices), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);

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

		RenderShader::Lights lights; //Have to Construct Structure first with appropriate data before uploading data
		lights.pointLightCount = renderData.pointLightsCount;
		std::copy(renderData.pointLights.begin(), renderData.pointLights.end(), lights.pointLights);
		_vkContext.update_buffer(currentDrawContext.lightsBuffer, (void*)&lights, alloc_lights_size, renderData.light_copy_info);
		i++;

		SkyboxShader::ViewTransformMatrices skybox_viewproj;
		skybox_viewproj.view = glm::mat4(glm::mat3(renderData.viewproj.view)); //Remove Translation Transform
		skybox_viewproj.proj = renderData.viewproj.proj;
		VkBufferCopy skyboxViewProj_copy_info{};
		skyboxViewProj_copy_info.srcOffset = 0;
		skyboxViewProj_copy_info.dstOffset = 0;
		skyboxViewProj_copy_info.size = sizeof(SkyboxShader::ViewTransformMatrices);
		_vkContext.update_buffer(currentDrawContext.skybox_viewprojMatrixBuffer, (void*)&skybox_viewproj, alloc_skyboxViewprojMatrix_size, skyboxViewProj_copy_info);
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

		//Skybox Buffer
		SkyboxShader::ViewTransformMatrices skybox_viewprojMatrix;
		skybox_viewprojMatrix.view = glm::mat4(glm::mat3(_stagingUpdateData.viewproj.view));
		skybox_viewprojMatrix.proj = _stagingUpdateData.viewproj.proj;

		VkBufferCopy skyboxViewProj_copy_info{};
		skyboxViewProj_copy_info.srcOffset = 0;
		skyboxViewProj_copy_info.dstOffset = 0;
		skyboxViewProj_copy_info.size = sizeof(SkyboxShader::ViewTransformMatrices);

		_vkContext.update_buffer(get_current_frame().drawContext.skybox_viewprojMatrixBuffer, &skybox_viewprojMatrix, sizeof(SkyboxShader::ViewTransformMatrices), skyboxViewProj_copy_info);

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
	range.size = sizeof(RenderShader::PushConstants);

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
	_vkContext.transition_image(cmd, swapchainImage, VK_IMAGE_LAYOUT_GENERAL);
	_vkContext.transition_image(cmd, _depthImage, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	//Clear Color
	VkClearColorValue clearValue = { {0.5f, 0.5f, 0.5f, 0.5f} };

	VkImageSubresourceRange clearRange = vkutil::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

	vkCmdClearColorImage(cmd, swapchainImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

	//Draw Geometry
	draw_geometry(cmd, swapchainImage);

	//Draw Skybox
	draw_skybox(cmd, swapchainImage);

	//Draw GUI
	draw_gui(cmd, swapchainImage);

	//Transition for Presentation
	_vkContext.transition_image(cmd, swapchainImage, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkCommandBufferSubmitInfo cmdInfo = vkutil::command_buffer_submit_info(cmd);
	VkSemaphoreSubmitInfo waitInfo = vkutil::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR, get_current_frame().swapchainSemaphore);
	VkSemaphoreSubmitInfo signalInfo = vkutil::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame().renderSemaphore);

	VkSubmitInfo2 submit = vkutil::submit_info(&cmdInfo, &signalInfo, &waitInfo);

	VK_CHECK(vkQueueSubmit2(_vkContext.primaryQueue, 1, &submit, get_current_frame().renderFence));

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &_swapchain.vkSwapchain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &get_current_frame().renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pImageIndices = &_swapchainImageIndex;

	result = vkQueuePresentKHR(_vkContext.primaryQueue, &presentInfo);

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
	viewport.y = swapchainExtent.height; //Move origin to swapchain height
	viewport.width = swapchainExtent.width;
	viewport.height = -1.0 * swapchainExtent.height; //Flip Viewport to account for flipped y-coord in glm calculations
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

void RenderSystem::draw_skybox(VkCommandBuffer cmd, const Image& swapchainImage) {
	//Create/Update Descriptors 
	std::vector<VkWriteDescriptorSet> descriptorWrites(2);

	VkDescriptorBufferInfo uniformBufferInfo{};
	uniformBufferInfo.buffer = get_current_frame().drawContext.skybox_viewprojMatrixBuffer.buffer;
	uniformBufferInfo.offset = 0;
	uniformBufferInfo.range = sizeof(glm::mat4) * 2;

	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = _skyboxDescriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].pBufferInfo = &uniformBufferInfo;

	VkDescriptorImageInfo skyboxCubeMapInfo{};
	skyboxCubeMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	skyboxCubeMapInfo.imageView = _hdrCubeMap.imageView;
	skyboxCubeMapInfo.sampler = _cubemapSampler;

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = _skyboxDescriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[1].descriptorCount = 1;
	descriptorWrites[1].pImageInfo = &skyboxCubeMapInfo;

	vkUpdateDescriptorSets(_vkContext.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	//Draw Commands
	VkRenderingAttachmentInfo colorAttachment = vkutil::attachment_info(swapchainImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo depthAttachment = vkutil::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; //Ensures depth values from previous rendering is kept.

	VkExtent2D swapchainExtent = get_swapChainExtent();

	VkRenderingInfo renderInfo = vkutil::rendering_info(swapchainExtent, &colorAttachment, &depthAttachment);

	vkCmdBeginRendering(cmd, &renderInfo);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _skyboxPipeline);

	//-Set Dynamic States
	VkViewport viewport{};
	viewport.x = 0;
	viewport.y = swapchainExtent.height;
	viewport.width = swapchainExtent.width;
	viewport.height = -1.0 * swapchainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = swapchainExtent.width;
	scissor.extent.height = swapchainExtent.height;

	vkCmdSetScissor(cmd, 0, 1, &scissor);

	//-Bind Descriptor Set
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _skyboxPipelineLayout, 0, 1, &_skyboxDescriptorSet, 0, nullptr);

	//-Bind Vertex Input Buffers
	std::vector<VkDeviceSize> vertexOffsets = { 0 };
	vkCmdBindVertexBuffers(cmd, 0, 1, &_skyboxVertexBuffer.buffer, vertexOffsets.data());
	vkCmdBindIndexBuffer(cmd, _skyboxIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	//-Draw
	vkCmdDrawIndexed(cmd, 36, 1, 0, 0, 0);

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

//Hold all the setup code for creating the pipelines and rendering to create the hdrMap and convoluted hdrMap
void RenderSystem::setup_hdrMap() {
	//Temp have file loading here. Might move loading code to loader.h/loader.cpp. And have only engine class actually initiate the load and pass the data to renderSystem.
	//Load HDR Equirectangular Image and Create it's Sampler
	AllocatedImage hdrImage;
	VkSampler hdrImage_Sampler;
	int width, height, nrChannels;


	float* data = stbi_loadf("C:\\Github\\vulkan_engine\\vulkan_engine\\assets\\HDR_Maps\\alps_field_4k.hdr", &width, &height, &nrChannels, 4);

	if (data) {
		VkExtent3D imageSize;
		imageSize.width = width;
		imageSize.height = height;
		imageSize.depth = 1;
		hdrImage = _vkContext.create_image("HDR Image", imageSize, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false);
		_vkContext.update_image(hdrImage, data, 4 * imageSize.width * imageSize.height * imageSize.depth * 4);

		stbi_image_free(data);
	}
	else {
		std::cout << "failed to load HDR Image File" << std::endl;
		return;
	}

	VkSamplerCreateInfo samplerCreateInfo{};
	samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;
	samplerCreateInfo.minLod = 0;
	samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	hdrImage_Sampler = _vkContext.create_sampler(samplerCreateInfo);

	//Setup HDR CubeMap, Convoluted HDR CubeMap, and a shared Sampler
	//-HDR Cubemap
	VkImageCreateInfo imgInfo{};
	imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.pNext = nullptr;
	imgInfo.imageType = VK_IMAGE_TYPE_2D;
	imgInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
	imgInfo.extent = { .width = 512, .height = 512, .depth = 1 };
	imgInfo.mipLevels = 1;
	imgInfo.arrayLayers = 6;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imgInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkImageViewCreateInfo imgViewInfo{};
	imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imgViewInfo.pNext = nullptr;
	imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	imgViewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
	imgViewInfo.subresourceRange.baseMipLevel = 0;
	imgViewInfo.subresourceRange.levelCount = 1;
	imgViewInfo.subresourceRange.baseArrayLayer = 0;
	imgViewInfo.subresourceRange.layerCount = 6;
	imgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	_hdrCubeMap = _vkContext.create_image("HDR CubeMap", imgInfo, allocInfo, imgViewInfo);

	//-Convoluted HDR Cubemap
	imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.pNext = nullptr;
	imgInfo.imageType = VK_IMAGE_TYPE_2D;
	imgInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
	imgInfo.extent = { .width = 512, .height = 512, .depth = 1 };
	imgInfo.mipLevels = 1;
	imgInfo.arrayLayers = 6;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imgInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imgViewInfo.pNext = nullptr;
	imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	imgViewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
	imgViewInfo.subresourceRange.baseMipLevel = 0;
	imgViewInfo.subresourceRange.levelCount = 1;
	imgViewInfo.subresourceRange.baseArrayLayer = 0;
	imgViewInfo.subresourceRange.layerCount = 6;
	imgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	_hdrIrradianceCubeMap = _vkContext.create_image("Convoluted HDR Cube Map", imgInfo, allocInfo, imgViewInfo);

	//-Cubemap Sampler
	VkSamplerCreateInfo convCubeMap_samplerCreateInfo{};
	convCubeMap_samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	convCubeMap_samplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;
	convCubeMap_samplerCreateInfo.minLod = 0;
	convCubeMap_samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	convCubeMap_samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	convCubeMap_samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	_cubemapSampler = _vkContext.create_sampler(convCubeMap_samplerCreateInfo);

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
		{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 2 },
		{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 }
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 1;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

	if (vkCreateDescriptorPool(_vkContext.device, &poolInfo, nullptr, &cubeMap_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create CubeMap Descriptor Pool");

	//--Set Binding Flags
	std::vector<VkDescriptorBindingFlags> binding_flags = {
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
	};

	VkDescriptorSetLayoutBindingFlagsCreateInfo set_binding_flags{};
	set_binding_flags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	set_binding_flags.bindingCount = static_cast<uint32_t>(binding_flags.size());
	set_binding_flags.pBindingFlags = binding_flags.data();

	//--Set Layout Bindings
	std::vector<VkDescriptorSetLayoutBinding> layout_bindings = {
		{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .pImmutableSamplers = nullptr },
		{.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr },
		{.binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr },
	};

	//--Now Create Descriptor Set Layout
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext = &set_binding_flags;
	layoutInfo.bindingCount = static_cast<uint32_t>(layout_bindings.size());
	layoutInfo.pBindings = layout_bindings.data(); 
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

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
	VkPipeline hdrCubeMap_pipeline; //Constructs HDR Cubemap from Equirectangular Loaded Image
	VkPipeline convCubeMap_pipeline; //Constructs Convoluted HDR Cubemap from Convoluting HDR Cubemap

	//--Load SHaders
	VkShaderModule vertexShader;
	if (!vkutil::load_shader_module("shaders/cubemap_vert.spv", _vkContext.device, &vertexShader))
		throw std::runtime_error("Error trying to create Cube Map Vertex Shader Module");
	else
		std::cout << "Cube Map Vertex Shader successfully loaded" << std::endl;

	VkShaderModule fragShader;
	if (!vkutil::load_shader_module("shaders/cubemap_frag.spv", _vkContext.device, &fragShader))
		throw std::runtime_error("error trying to create Cube Map Frag Shader Module");
	else
		std::cout << "Cube Map Fragment Shader successfully loaded" << std::endl;

	VkShaderModule conv_vertexShader;
	if (!vkutil::load_shader_module("shaders/convCubeMap_vert.spv", _vkContext.device, &conv_vertexShader))
		throw std::runtime_error("Error trying to create Convoluted Cube Map Vertex Shader Module");
	else
		std::cout << "Convoluted Cube Map Vertex Shader successfully loaded" << std::endl;

	VkShaderModule conv_fragShader;
	if (!vkutil::load_shader_module("shaders/convCubeMap_frag.spv", _vkContext.device, &conv_fragShader))
		throw std::runtime_error("error trying to create Convoluted Cube Map Frag Shader Module");
	else
		std::cout << "Convoluted Cube Map Fragment Shader successfully loaded" << std::endl;

	//--Set Pipeline Layout - Descriptor Set
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkutil::pipeline_layout_create_info();
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &cubeMap_descriptorSetLayout;
	pipeline_layout_info.pushConstantRangeCount = 0;
	pipeline_layout_info.pPushConstantRanges = nullptr;

	VK_CHECK(vkCreatePipelineLayout(_vkContext.device, &pipeline_layout_info, nullptr, &cubeMap_pipelineLayout));

	//--Build Pipelines
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
	pipelineBuilder.set_color_attachment_format(VK_FORMAT_B8G8R8A8_UNORM);
	pipelineBuilder.set_depth_format(VK_FORMAT_D32_SFLOAT);

	hdrCubeMap_pipeline = pipelineBuilder.build_pipeline(_vkContext.device);

	pipelineBuilder.set_shaders(conv_vertexShader, conv_fragShader);

	convCubeMap_pipeline = pipelineBuilder.build_pipeline(_vkContext.device);

	//Destroy Shaders
	vkDestroyShaderModule(_vkContext.device, vertexShader, nullptr);
	vkDestroyShaderModule(_vkContext.device, fragShader, nullptr);
	vkDestroyShaderModule(_vkContext.device, conv_vertexShader, nullptr);
	vkDestroyShaderModule(_vkContext.device, conv_fragShader, nullptr);

	//Setup Neccesary Buffers and Data for the Rendering CubeMap
	AllocatedBuffer cubeMap_vertexBuffer;
	AllocatedBuffer cubeMap_indexBuffer;
	AllocatedBuffer cubeMap_uniformBuffer;
	AllocatedBuffer cubeMap_uniformStagingBuffer; //Used to update Uniform Buffer within command recording
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

	glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

	SkyboxShader::ViewTransformMatrices transMatrices[6] = { //Specialized lookat matrices for equirrectangler map orientation
		{ .view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)), .proj = proj },
		{ .view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)), .proj = proj },
		{ .view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f,  0.0f), glm::vec3(-1.0f,  0.0f, 0.0f)), .proj = proj },
		{ .view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  -1.0f,  0.0f), glm::vec3(1.0f,  0.0f,  0.0f)), .proj = proj },
		{ .view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, 1.0f,  0.0f)), .proj = proj },
		{ .view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, 1.0f,  0.0f)), .proj = proj },
	};

	VmaAllocationCreateFlags allocFlags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
	cubeMap_vertexBuffer = _vkContext.create_buffer("CubeMap Vertex Buffer", unitCube_vertices.size() * sizeof(glm::vec3), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
	cubeMap_indexBuffer = _vkContext.create_buffer("CubeMap Index Buffer", unitCube_indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
	cubeMap_uniformBuffer = _vkContext.create_buffer("CubeMap Uniform Buffer", sizeof(SkyboxShader::ViewTransformMatrices), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
	cubeMap_uniformStagingBuffer = _vkContext.create_buffer("CubeMap Uniform Staging Buffer", sizeof(SkyboxShader::ViewTransformMatrices) * 6, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
	cubeMap_frameBufferImage = _vkContext.create_image("CubeMap Frame Buffer Image", { .width = 512, .height = 512, .depth = 1 }, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);

	VkBufferCopy bufferCpy = { .srcOffset = 0, .dstOffset = 0, .size = unitCube_vertices.size() * sizeof(glm::vec3) };
	_vkContext.update_buffer(cubeMap_vertexBuffer, unitCube_vertices.data(), unitCube_vertices.size() * sizeof(glm::vec3), bufferCpy);
	bufferCpy.size = unitCube_indices.size() * sizeof(uint32_t);
	_vkContext.update_buffer(cubeMap_indexBuffer, unitCube_indices.data(), unitCube_indices.size() * sizeof(uint32_t), bufferCpy);
	bufferCpy.size = sizeof(SkyboxShader::ViewTransformMatrices) * 6;
	_vkContext.update_buffer(cubeMap_uniformStagingBuffer, transMatrices, bufferCpy.size, bufferCpy);

	//Create Descriptors
	std::vector<VkWriteDescriptorSet> descriptorWrites(2);

	VkDescriptorBufferInfo uniformBufferInfo{};
	uniformBufferInfo.buffer = cubeMap_uniformBuffer.buffer;
	uniformBufferInfo.offset = 0;
	uniformBufferInfo.range = sizeof(glm::mat4) * 2;

	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = cubeMap_descriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].pBufferInfo = &uniformBufferInfo;

	VkDescriptorImageInfo equirectangularMapInfo{}; //HDR Image Descriptor
	equirectangularMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	equirectangularMapInfo.imageView = hdrImage.imageView;
	equirectangularMapInfo.sampler = hdrImage_Sampler;

	VkDescriptorImageInfo hdrCubeMapInfo{}; //HDR Cubemap Descriptor
	hdrCubeMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	hdrCubeMapInfo.imageView = _hdrCubeMap.imageView;
	hdrCubeMapInfo.sampler = _cubemapSampler;

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = cubeMap_descriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[1].descriptorCount = 1;
	descriptorWrites[1].pImageInfo = &equirectangularMapInfo;

	vkUpdateDescriptorSets(_vkContext.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	//Set up Resource for Copying Data
	VkBufferCopy uniformBufferCpy = { .srcOffset = 0, .dstOffset = 0, .size = sizeof(SkyboxShader::ViewTransformMatrices) }; //For Copying Uniform Data from Staging Buffer to Uniform Buffer

	VkImageCopy renderToCubeMapCpy{}; //For Copying Frame Image Data to CubeMap Image
	renderToCubeMapCpy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	renderToCubeMapCpy.srcSubresource.baseArrayLayer = 0;
	renderToCubeMapCpy.srcSubresource.layerCount = 1;
	renderToCubeMapCpy.srcSubresource.mipLevel = 0;
	renderToCubeMapCpy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	renderToCubeMapCpy.dstSubresource.baseArrayLayer = 0;
	renderToCubeMapCpy.dstSubresource.layerCount = 1;
	renderToCubeMapCpy.dstSubresource.mipLevel = 0;
	renderToCubeMapCpy.srcOffset = { .x = 0, .y = 0, .z = 0 };
	renderToCubeMapCpy.dstOffset = { .x = 0, .y = 0, .z = 0 };
	renderToCubeMapCpy.extent = { .width = 512, .height = 512, .depth = 1 };

	//Setup Commands and Syncing
	VkCommandPool cubeMap_commandPool;
	VkCommandBuffer cubeMap_commandBuffer;
	VkFence cubeMap_renderFence; //Indicates that a render has finished

	VkCommandPoolCreateInfo cmdPoolInfo{};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.pNext = nullptr;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cmdPoolInfo.queueFamilyIndex = _vkContext.primaryQueueFamily;

	VkFenceCreateInfo fenceCreateInfo{};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;

	VK_CHECK(vkCreateCommandPool(_vkContext.device, &cmdPoolInfo, nullptr, &cubeMap_commandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo{};
	cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAllocInfo.pNext = nullptr;
	cmdAllocInfo.commandPool = cubeMap_commandPool;
	cmdAllocInfo.commandBufferCount = 1;
	cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VK_CHECK(vkAllocateCommandBuffers(_vkContext.device, &cmdAllocInfo, &cubeMap_commandBuffer));

	VK_CHECK(vkCreateFence(_vkContext.device, &fenceCreateInfo, nullptr, &cubeMap_renderFence));

	//Setup Commands for Rendering
	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.pInheritanceInfo = nullptr;
	VkClearColorValue clearValue = { {0.5f, 0.5f, 0.5f, 0.5f} };
	VkImageSubresourceRange clearRange = vkutil::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);
	VkRenderingAttachmentInfo colorAttachment = vkutil::attachment_info(cubeMap_frameBufferImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkExtent2D frameBufferExtent = { .width = 512, .height = 512 };
	VkRenderingInfo renderInfo = vkutil::rendering_info(frameBufferExtent, &colorAttachment, nullptr);
	VkViewport viewport{};
	viewport.x = 0;
	viewport.y = frameBufferExtent.height;
	viewport.width = frameBufferExtent.width;
	viewport.height = -1.0 * frameBufferExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	VkRect2D scissor{};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = frameBufferExtent.width;
	scissor.extent.height = frameBufferExtent.height;
	std::vector<VkDeviceSize> vertexOffsets = { 0 };

	//Begin Recording and Submit Commands for Constructing HDR Cubemap
	VK_CHECK(vkBeginCommandBuffer(cubeMap_commandBuffer, &cmdBeginInfo));

	_vkContext.transition_image(cubeMap_commandBuffer, _hdrCubeMap, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	//Render Each Side of the Cubemap and Upload Data to HDR Cubemap
	for (int i = 0; i < 6; i++) {
		//Update Uniform Data from Data in Staging Buffer
		uniformBufferCpy.srcOffset = sizeof(SkyboxShader::ViewTransformMatrices) * i;
		vkCmdCopyBuffer(cubeMap_commandBuffer, cubeMap_uniformStagingBuffer.buffer, cubeMap_uniformBuffer.buffer, 1, &uniformBufferCpy);

		_vkContext.transition_image(cubeMap_commandBuffer, cubeMap_frameBufferImage, VK_IMAGE_LAYOUT_GENERAL);

		//Clear Color
		vkCmdClearColorImage(cubeMap_commandBuffer, cubeMap_frameBufferImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

		//Begin Rendering
		vkCmdBeginRendering(cubeMap_commandBuffer, &renderInfo);

		vkCmdBindPipeline(cubeMap_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, hdrCubeMap_pipeline);

		//Set Dynamic States
		vkCmdSetViewport(cubeMap_commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(cubeMap_commandBuffer, 0, 1, &scissor);

		//Bind Descriptor Set
		vkCmdBindDescriptorSets(cubeMap_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cubeMap_pipelineLayout, 0, 1, &cubeMap_descriptorSet, 0, nullptr);

		//Bind Vertex Input Buffers
		vkCmdBindVertexBuffers(cubeMap_commandBuffer, 0, 1, &cubeMap_vertexBuffer.buffer, vertexOffsets.data());
		vkCmdBindIndexBuffer(cubeMap_commandBuffer, cubeMap_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		//Draw
		vkCmdDrawIndexed(cubeMap_commandBuffer, unitCube_indices.size(), 1, 0, 0, 0);

		vkCmdEndRendering(cubeMap_commandBuffer);

		//Transfer Frame Image to HDR Cubemap Image
		_vkContext.transition_image(cubeMap_commandBuffer, cubeMap_frameBufferImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		renderToCubeMapCpy.dstSubresource.baseArrayLayer = i;
		vkCmdCopyImage(cubeMap_commandBuffer, cubeMap_frameBufferImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _hdrCubeMap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &renderToCubeMapCpy);
	}

	VK_CHECK(vkEndCommandBuffer(cubeMap_commandBuffer));

	VkCommandBufferSubmitInfo cmdInfo = vkutil::command_buffer_submit_info(cubeMap_commandBuffer);
	VkSubmitInfo2 submit = vkutil::submit_info(&cmdInfo, nullptr, nullptr);

	VK_CHECK(vkQueueSubmit2(_vkContext.primaryQueue, 1, &submit, cubeMap_renderFence));

	//Wait for Render commands for HDR Cubemap to finish
	VK_CHECK(vkWaitForFences(_vkContext.device, 1, &cubeMap_renderFence, true, 1000000000));

	VK_CHECK(vkResetFences(_vkContext.device, 1, &cubeMap_renderFence));

	//Update Uniform Descriptor Data with new lookat matrices that flip things around for correct cubemap face orientation 
	
	/*
	transMatrices[0] = { .view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)), .proj = proj };
	transMatrices[1] = { .view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, 1.0f,  0.0f)), .proj = proj };
	transMatrices[2] = { .view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  -1.0f)), .proj = proj };
	transMatrices[3] = { .view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, 1.0f)), .proj = proj };
	transMatrices[5] = { .view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f, -1.0f), glm::vec3(0.0f, 1.0f,  0.0f)), .proj = proj };
	transMatrices[4] = { .view = glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f,  0.0f,  1.0f), glm::vec3(0.0f, 1.0f,  0.0f)), .proj = proj };
	_vkContext.update_buffer(cubeMap_uniformStagingBuffer, transMatrices, bufferCpy.size, bufferCpy);
	*/

	//Update Image Descriptor Set with HDR Cubemap
	descriptorWrites[1].pImageInfo = &hdrCubeMapInfo;
	vkUpdateDescriptorSets(_vkContext.device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);

	//Record and Submit new commands for Convulted Cubemap Rendering
	VK_CHECK(vkResetCommandBuffer(cubeMap_commandBuffer, 0));
	VK_CHECK(vkBeginCommandBuffer(cubeMap_commandBuffer, &cmdBeginInfo));

	_vkContext.transition_image(cubeMap_commandBuffer, _hdrCubeMap, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	_vkContext.transition_image(cubeMap_commandBuffer, _hdrIrradianceCubeMap, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	for (int i = 0; i < 6; i++) {
		//Update Uniform Data from Data in Staging Buffer
		uniformBufferCpy.srcOffset = sizeof(SkyboxShader::ViewTransformMatrices) * i;
		vkCmdCopyBuffer(cubeMap_commandBuffer, cubeMap_uniformStagingBuffer.buffer, cubeMap_uniformBuffer.buffer, 1, &uniformBufferCpy);

		_vkContext.transition_image(cubeMap_commandBuffer, cubeMap_frameBufferImage, VK_IMAGE_LAYOUT_GENERAL);

		//-Clear Color
		vkCmdClearColorImage(cubeMap_commandBuffer, cubeMap_frameBufferImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

		//-Draw
		vkCmdBeginRendering(cubeMap_commandBuffer, &renderInfo);
		vkCmdBindPipeline(cubeMap_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, convCubeMap_pipeline);

		//-Set Dynamic States
		vkCmdSetViewport(cubeMap_commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(cubeMap_commandBuffer, 0, 1, &scissor);

		//-Bind Descriptor Set
		vkCmdBindDescriptorSets(cubeMap_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cubeMap_pipelineLayout, 0, 1, &cubeMap_descriptorSet, 0, nullptr);

		//-Bind Vertex Input Buffers
		vkCmdBindVertexBuffers(cubeMap_commandBuffer, 0, 1, &cubeMap_vertexBuffer.buffer, vertexOffsets.data());
		vkCmdBindIndexBuffer(cubeMap_commandBuffer, cubeMap_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		//-Draw
		vkCmdDrawIndexed(cubeMap_commandBuffer, unitCube_indices.size(), 1, 0, 0, 0);
		vkCmdEndRendering(cubeMap_commandBuffer);

		_vkContext.transition_image(cubeMap_commandBuffer, cubeMap_frameBufferImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		renderToCubeMapCpy.dstSubresource.baseArrayLayer = i;
		vkCmdCopyImage(cubeMap_commandBuffer, cubeMap_frameBufferImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, _hdrIrradianceCubeMap.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &renderToCubeMapCpy);
	}

	VK_CHECK(vkEndCommandBuffer(cubeMap_commandBuffer));

	VK_CHECK(vkQueueSubmit2(_vkContext.primaryQueue, 1, &submit, cubeMap_renderFence));

	//Wait for Rendering of Convoluted HDR Cubemap to finish
	VK_CHECK(vkWaitForFences(_vkContext.device, 1, &cubeMap_renderFence, true, 1000000000));
	
	//Delete Resources
	vkDestroyFence(_vkContext.device, cubeMap_renderFence, nullptr);
	vkDestroyCommandPool(_vkContext.device, cubeMap_commandPool, nullptr);
	_vkContext.destroy_image(cubeMap_frameBufferImage);
	_vkContext.destroy_buffer(cubeMap_uniformStagingBuffer);
	_vkContext.destroy_buffer(cubeMap_uniformBuffer);
	_vkContext.destroy_buffer(cubeMap_indexBuffer);
	_vkContext.destroy_buffer(cubeMap_vertexBuffer);
	vkDestroyPipeline(_vkContext.device, convCubeMap_pipeline, nullptr);
	vkDestroyPipeline(_vkContext.device, hdrCubeMap_pipeline, nullptr);
	vkDestroyPipelineLayout(_vkContext.device, cubeMap_pipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(_vkContext.device, cubeMap_descriptorSetLayout, nullptr);
	vkDestroyDescriptorPool(_vkContext.device, cubeMap_descriptorPool, nullptr);
	_vkContext.destroy_sampler(hdrImage_Sampler);
	_vkContext.destroy_image(hdrImage);
}

void RenderSystem::setup_hdrMap2() {
	//Temp have file loading here. Might move loading code to loader.h/loader.cpp. And have only engine class actually initiate the load and pass the data to renderSystem.
	//Load HDR Equirectangular Image and Create it's Sampler
	AllocatedImage hdrImage;
	VkSampler hdrImage_Sampler;
	int width, height, nrChannels;

	float* data = stbi_loadf("C:\\Github\\vulkan_engine\\vulkan_engine\\assets\\HDR_Maps\\alps_field_4k.hdr", &width, &height, &nrChannels, 4);

	if (data) {
		VkExtent3D imageSize;
		imageSize.width = width;
		imageSize.height = height;
		imageSize.depth = 1;
		hdrImage = _vkContext.create_image("HDR Image", imageSize, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false);
		_vkContext.update_image(hdrImage, data, 4 * imageSize.width * imageSize.height * imageSize.depth * 4);

		stbi_image_free(data);
	}
	else {
		std::cout << "failed to load HDR Image File" << std::endl;
		return;
	}

	VkSamplerCreateInfo samplerCreateInfo{};
	samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;
	samplerCreateInfo.minLod = 0;
	samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	hdrImage_Sampler = _vkContext.create_sampler(samplerCreateInfo);

	//Setup HDR CubeMap, Convoluted HDR CubeMap, and a shared Sampler
	//-HDR Cubemap
	VkImageCreateInfo imgInfo{};
	imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.pNext = nullptr;
	imgInfo.imageType = VK_IMAGE_TYPE_2D;
	imgInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imgInfo.extent = { .width = 512, .height = 512, .depth = 1 };
	imgInfo.mipLevels = 5;
	imgInfo.arrayLayers = 6;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imgInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkImageViewCreateInfo imgViewInfo{};
	imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imgViewInfo.pNext = nullptr;
	imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	imgViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imgViewInfo.subresourceRange.baseMipLevel = 0;
	imgViewInfo.subresourceRange.levelCount = 5;
	imgViewInfo.subresourceRange.baseArrayLayer = 0;
	imgViewInfo.subresourceRange.layerCount = 6;
	imgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	_hdrCubeMap = _vkContext.create_image("HDR CubeMap", imgInfo, allocInfo, imgViewInfo);

	//-Irradiance HDR Cubemap
	imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.pNext = nullptr;
	imgInfo.imageType = VK_IMAGE_TYPE_2D;
	imgInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imgInfo.extent = { .width = 512, .height = 512, .depth = 1 };
	imgInfo.mipLevels = 1;
	imgInfo.arrayLayers = 6;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	imgInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imgViewInfo.pNext = nullptr;
	imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	imgViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imgViewInfo.subresourceRange.baseMipLevel = 0;
	imgViewInfo.subresourceRange.levelCount = 1;
	imgViewInfo.subresourceRange.baseArrayLayer = 0;
	imgViewInfo.subresourceRange.layerCount = 6;
	imgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	_hdrIrradianceCubeMap = _vkContext.create_image("Irradiance HDR Cube Map", imgInfo, allocInfo, imgViewInfo);

	//-Specular HDR CubeMap
	const uint32_t HDR_SPECULAR_CUBEMAP_MIP_LEVELS_COUNT = 5;
	VkImageView hdrSpecularCubeMap_ImageViews[HDR_SPECULAR_CUBEMAP_MIP_LEVELS_COUNT]; //Imageviews for each Mip Level in HDR Specular Map

	imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.pNext = nullptr;
	imgInfo.imageType = VK_IMAGE_TYPE_2D;
	imgInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imgInfo.extent = { .width = 512, .height = 512, .depth = 1 };
	imgInfo.mipLevels = HDR_SPECULAR_CUBEMAP_MIP_LEVELS_COUNT;
	imgInfo.arrayLayers = 6;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	imgInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imgViewInfo.pNext = nullptr;
	imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	imgViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imgViewInfo.subresourceRange.baseMipLevel = 0;
	imgViewInfo.subresourceRange.levelCount = HDR_SPECULAR_CUBEMAP_MIP_LEVELS_COUNT;
	imgViewInfo.subresourceRange.baseArrayLayer = 0;
	imgViewInfo.subresourceRange.layerCount = 6;
	imgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	_hdrSpecularCubeMap = _vkContext.create_image("Specular HDR Cube Map", imgInfo, allocInfo, imgViewInfo);

	//--Create Each Mip Level ImageView for HDR Specular Map
	VkImageViewCreateInfo imgLevelViewInfo{};
	imgLevelViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imgLevelViewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	imgLevelViewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
	imgLevelViewInfo.subresourceRange.levelCount = 1;
	imgLevelViewInfo.subresourceRange.baseArrayLayer = 0;
	imgLevelViewInfo.subresourceRange.layerCount = 6;
	imgLevelViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	imgLevelViewInfo.image = _hdrSpecularCubeMap.image;

	for (int i = 0; i < HDR_SPECULAR_CUBEMAP_MIP_LEVELS_COUNT; i++) {
		imgLevelViewInfo.subresourceRange.baseMipLevel = i;
		VK_CHECK(vkCreateImageView(_vkContext.device, &imgLevelViewInfo, nullptr, &hdrSpecularCubeMap_ImageViews[i]));
	}

	//-Specular HDR LUT
	imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.pNext = nullptr;
	imgInfo.imageType = VK_IMAGE_TYPE_2D;
	imgInfo.format = VK_FORMAT_R16G16_SFLOAT;
	imgInfo.extent = { .width = 512, .height = 512, .depth = 1 };
	imgInfo.mipLevels = 1;
	imgInfo.arrayLayers = 1;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	imgInfo.flags = 0;

	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	allocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imgViewInfo.pNext = nullptr;
	imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imgViewInfo.format = VK_FORMAT_R16G16_SFLOAT;
	imgViewInfo.subresourceRange.baseMipLevel = 0;
	imgViewInfo.subresourceRange.levelCount = 1;
	imgViewInfo.subresourceRange.baseArrayLayer = 0;
	imgViewInfo.subresourceRange.layerCount = 1;
	imgViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

	_hdrSpecularLUT = _vkContext.create_image("Specular HDR LUT", imgInfo, allocInfo, imgViewInfo);

	//-Cubemap Sampler
	VkSamplerCreateInfo irradianceCubeMap_samplerCreateInfo{};
	irradianceCubeMap_samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	irradianceCubeMap_samplerCreateInfo.maxLod = VK_LOD_CLAMP_NONE;
	irradianceCubeMap_samplerCreateInfo.minLod = 0;
	irradianceCubeMap_samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	irradianceCubeMap_samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	irradianceCubeMap_samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	_cubemapSampler = _vkContext.create_sampler(irradianceCubeMap_samplerCreateInfo);

	//Descriptors Set
	VkDescriptorPool hdrCubeMap_descriptorPool; //For all sets to use
	VkDescriptorSetLayout hdrCubeMap_descriptorSetLayout; //Used by HDR Image Sampling and Irradiance Cube Map generating descriptor set
	VkDescriptorSet hdrImageSample_descriptorSet;
	VkDescriptorSet irradianceCubeMap_descriptorSet;
	VkDescriptorSetLayout specularCubeMap_descriptorSetLayout; //Used by Specular Cube Map generationg descriptor set
	VkDescriptorSet	specularCubeMap_descriptorSet;
	VkDescriptorSetLayout specularLUT_descriptorSetLayout; //Used by LUT Descriptor set
	VkDescriptorSet specularLUT_descriptorSet;

	std::vector<VkDescriptorPoolSize> poolSizes = {
		{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 3 },
		{.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 3 + HDR_SPECULAR_CUBEMAP_MIP_LEVELS_COUNT}
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 4;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

	if (vkCreateDescriptorPool(_vkContext.device, &poolInfo, nullptr, &hdrCubeMap_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create HDR Resources Descriptor Pool");

	std::vector<VkDescriptorSetLayoutBinding> layout_bindings = {
		{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .pImmutableSamplers = nullptr },
		{.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .pImmutableSamplers = nullptr },
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(layout_bindings.size());
	layoutInfo.pBindings = layout_bindings.data();
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

	if (vkCreateDescriptorSetLayout(_vkContext.device, &layoutInfo, nullptr, &hdrCubeMap_descriptorSetLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to Create HDR Cubemap Descriptor Set Layout");

	std::vector<VkDescriptorSetLayoutBinding> layout_bindings2 = {
		{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .pImmutableSamplers = nullptr },
		{.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = HDR_SPECULAR_CUBEMAP_MIP_LEVELS_COUNT,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .pImmutableSamplers = nullptr },
	};

	std::vector<VkDescriptorBindingFlags> layout_bindings2_flags = {
		0, VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
	};

	VkDescriptorSetLayoutBindingFlagsCreateInfo layout_bindings2_flag_info{};
	layout_bindings2_flag_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	layout_bindings2_flag_info.bindingCount = layout_bindings2_flags.size();
	layout_bindings2_flag_info.pBindingFlags = layout_bindings2_flags.data();

	layoutInfo.pNext = &layout_bindings2_flag_info;
	layoutInfo.bindingCount = static_cast<uint32_t>(layout_bindings2.size());
	layoutInfo.pBindings = layout_bindings2.data();

	if (vkCreateDescriptorSetLayout(_vkContext.device, &layoutInfo, nullptr, &specularCubeMap_descriptorSetLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to Create HDR Specular Cube Map Descriptor Set Layout");

	std::vector<VkDescriptorSetLayoutBinding> layout_bindings3 = {
		{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .pImmutableSamplers = nullptr}
	};

	layoutInfo.pNext = nullptr;
	layoutInfo.bindingCount = static_cast<uint32_t>(layout_bindings3.size());
	layoutInfo.pBindings = layout_bindings3.data();

	if (vkCreateDescriptorSetLayout(_vkContext.device, &layoutInfo, nullptr, &specularLUT_descriptorSetLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to Create HDR Specular LUT Descriptor Set Layout");

	VkDescriptorSetAllocateInfo descriptorSetallocInfo{};
	descriptorSetallocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetallocInfo.descriptorPool = hdrCubeMap_descriptorPool;
	descriptorSetallocInfo.descriptorSetCount = 1;
	descriptorSetallocInfo.pSetLayouts = &hdrCubeMap_descriptorSetLayout;

	if (vkAllocateDescriptorSets(_vkContext.device, &descriptorSetallocInfo, &hdrImageSample_descriptorSet) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate HDR Image Sample Descriptor Set");

	if (vkAllocateDescriptorSets(_vkContext.device, &descriptorSetallocInfo, &irradianceCubeMap_descriptorSet) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate HDR Irradiance CubeMap Descriptor Set");

	VkDescriptorSetVariableDescriptorCountAllocateInfo varDescriptorCountInfo{};
	varDescriptorCountInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
	varDescriptorCountInfo.descriptorSetCount = 1;
	varDescriptorCountInfo.pDescriptorCounts = &HDR_SPECULAR_CUBEMAP_MIP_LEVELS_COUNT;

	descriptorSetallocInfo.pNext = &varDescriptorCountInfo;
	descriptorSetallocInfo.pSetLayouts = &specularCubeMap_descriptorSetLayout;

	if (vkAllocateDescriptorSets(_vkContext.device, &descriptorSetallocInfo, &specularCubeMap_descriptorSet) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate HDR Specular CubeMap Descriptor Set");
	
	descriptorSetallocInfo.pNext = nullptr;
	descriptorSetallocInfo.pSetLayouts = &specularLUT_descriptorSetLayout;

	if (vkAllocateDescriptorSets(_vkContext.device, &descriptorSetallocInfo, &specularLUT_descriptorSet) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate HDR Specular LUT Descriptor Set");

	//Descriptors
	VkWriteDescriptorSet descriptorWrites[7] = {};

	VkDescriptorImageInfo hdrImageSample_imageSampleInfo{};
	hdrImageSample_imageSampleInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	hdrImageSample_imageSampleInfo.imageView = hdrImage.imageView;
	hdrImageSample_imageSampleInfo.sampler = hdrImage_Sampler;

	VkDescriptorImageInfo hdrImageSample_targetCubemapInfo{};
	hdrImageSample_targetCubemapInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	hdrImageSample_targetCubemapInfo.imageView = _hdrCubeMap.imageView;

	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = hdrImageSample_descriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].pImageInfo = &hdrImageSample_imageSampleInfo;

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = hdrImageSample_descriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	descriptorWrites[1].descriptorCount = 1;
	descriptorWrites[1].pImageInfo = &hdrImageSample_targetCubemapInfo;

	VkDescriptorImageInfo irradianceCubeMap_imageSampleInfo{};
	irradianceCubeMap_imageSampleInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	irradianceCubeMap_imageSampleInfo.imageView = _hdrCubeMap.imageView;
	irradianceCubeMap_imageSampleInfo.sampler = _cubemapSampler;

	VkDescriptorImageInfo irradianceCubeMap_targetCubemapInfo{};
	irradianceCubeMap_targetCubemapInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	irradianceCubeMap_targetCubemapInfo.imageView = _hdrIrradianceCubeMap.imageView;

	descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[2].dstSet = irradianceCubeMap_descriptorSet;
	descriptorWrites[2].dstBinding = 0;
	descriptorWrites[2].dstArrayElement = 0;
	descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[2].descriptorCount = 1;
	descriptorWrites[2].pImageInfo = &irradianceCubeMap_imageSampleInfo;

	descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[3].dstSet = irradianceCubeMap_descriptorSet;
	descriptorWrites[3].dstBinding = 1;
	descriptorWrites[3].dstArrayElement = 0;
	descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	descriptorWrites[3].descriptorCount = 1;
	descriptorWrites[3].pImageInfo = &irradianceCubeMap_targetCubemapInfo;

	VkDescriptorImageInfo specularCubeMap_imageSampleInfo{};
	specularCubeMap_imageSampleInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	specularCubeMap_imageSampleInfo.imageView = _hdrCubeMap.imageView;
	specularCubeMap_imageSampleInfo.sampler = _cubemapSampler;

	VkDescriptorImageInfo specularCubeMap_targetCubemapInfos[HDR_SPECULAR_CUBEMAP_MIP_LEVELS_COUNT]; //!!!NOTE NEED TO DRASTICALLY CHANGE specularPrefilteredMap shader to target and evaluate the correct values for multiple imageviews
	for (int i = 0; i < HDR_SPECULAR_CUBEMAP_MIP_LEVELS_COUNT; i++) {
		specularCubeMap_targetCubemapInfos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		specularCubeMap_targetCubemapInfos[i].imageView = hdrSpecularCubeMap_ImageViews[i];
	}

	descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[4].dstSet = specularCubeMap_descriptorSet;
	descriptorWrites[4].dstBinding = 0;
	descriptorWrites[4].dstArrayElement = 0;
	descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[4].descriptorCount = 1;
	descriptorWrites[4].pImageInfo = &specularCubeMap_imageSampleInfo;

	descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[5].dstSet = specularCubeMap_descriptorSet;
	descriptorWrites[5].dstBinding = 1;
	descriptorWrites[5].dstArrayElement = 0;
	descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	descriptorWrites[5].descriptorCount = HDR_SPECULAR_CUBEMAP_MIP_LEVELS_COUNT;
	descriptorWrites[5].pImageInfo = specularCubeMap_targetCubemapInfos;

	VkDescriptorImageInfo specularLUT_targetImageInfo{};
	specularLUT_targetImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	specularLUT_targetImageInfo.imageView = _hdrSpecularLUT.imageView;

	descriptorWrites[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[6].dstSet = specularLUT_descriptorSet;
	descriptorWrites[6].dstBinding = 0;
	descriptorWrites[6].dstArrayElement = 0;
	descriptorWrites[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	descriptorWrites[6].descriptorCount = 1;
	descriptorWrites[6].pImageInfo = &specularLUT_targetImageInfo;

	vkUpdateDescriptorSets(_vkContext.device, 7, descriptorWrites, 0, nullptr);

	//Pipeline
	VkPipelineLayout hdrCubemap_pipelineLayout;
	VkPipeline hdrImageSample_pipeline; //Constructs HDR Cubemap from Equirectangular Loaded Image
	VkPipeline irradianceCubeMap_pipeline; //Constructs Convoluted HDR Cubemap from Convoluting HDR Cubemap
	VkPipelineLayout specularCubeMap_pipelineLayout;
	VkPipeline specularCubeMap_pipeline;
	VkPipelineLayout specularLUT_pipelineLayout;
	VkPipeline specularLUT_pipeline;

	VkPipelineLayoutCreateInfo pipeline_layout_info = vkutil::pipeline_layout_create_info();
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &hdrCubeMap_descriptorSetLayout;
	pipeline_layout_info.pushConstantRangeCount = 0;
	pipeline_layout_info.pPushConstantRanges = nullptr;

	VK_CHECK(vkCreatePipelineLayout(_vkContext.device, &pipeline_layout_info, nullptr, &hdrCubemap_pipelineLayout));

	VkPushConstantRange specularCubeMap_PCRange{};
	specularCubeMap_PCRange.offset = 0;
	specularCubeMap_PCRange.size = sizeof(SpecularCubemapShader::PushConstants);
	specularCubeMap_PCRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	pipeline_layout_info.pSetLayouts = &specularCubeMap_descriptorSetLayout;
	pipeline_layout_info.pushConstantRangeCount = 1;
	pipeline_layout_info.pPushConstantRanges = &specularCubeMap_PCRange;

	VK_CHECK(vkCreatePipelineLayout(_vkContext.device, &pipeline_layout_info, nullptr, &specularCubeMap_pipelineLayout));

	pipeline_layout_info.pSetLayouts = &specularLUT_descriptorSetLayout;
	pipeline_layout_info.pushConstantRangeCount = 0;
	pipeline_layout_info.pPushConstantRanges = nullptr;

	VK_CHECK(vkCreatePipelineLayout(_vkContext.device, &pipeline_layout_info, nullptr, &specularLUT_pipelineLayout));

	VkShaderModule hdrImageSampleShader;
	if (!vkutil::load_shader_module("shaders/hdrImageSample_comp.spv", _vkContext.device, &hdrImageSampleShader))
		throw std::runtime_error("Error trying to create HDR Image Sample Shader Module");
	else
		std::cout << "HDR Image Sample Shader successfully loaded" << std::endl;

	VkShaderModule irradianceCubemapShader;
	if (!vkutil::load_shader_module("shaders/diffuseIrradianceImage_comp.spv", _vkContext.device, &irradianceCubemapShader))
		throw std::runtime_error("Error trying to create Irradiance Cubemap Shader Module");
	else
		std::cout << "Irradiance Cubemap Shader successfully loaded" << std::endl;

	VkShaderModule specularCubemapShader;
	if (!vkutil::load_shader_module("shaders/specularPrefilteredMap_comp.spv", _vkContext.device, &specularCubemapShader))
		throw std::runtime_error("Error trying to create Specular Cubemap Shader Module");
	else
		std::cout << "Specular Cubemap Shader successfully loaded" << std::endl;

	VkShaderModule specularLUTShader;
	if (!vkutil::load_shader_module("shaders/specularBRDFIntegrationLUT_comp.spv", _vkContext.device, &specularLUTShader))
		throw std::runtime_error("Error trying to create Specular LUT Shader Module");
	else
		std::cout << "Specular LUT Shader successfully loaded" << std::endl;

	VkPipelineShaderStageCreateInfo shaderInfo{};
	shaderInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderInfo.module = hdrImageSampleShader;
	shaderInfo.pName = "main";

	VkComputePipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage = shaderInfo;
	pipelineInfo.layout = hdrCubemap_pipelineLayout;

	if (vkCreateComputePipelines(_vkContext.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &hdrImageSample_pipeline) != VK_SUCCESS)
		std::cout << "Failed to create HDR Image Sample Pipeline" << std::endl;

	pipelineInfo.stage.module = irradianceCubemapShader;

	if (vkCreateComputePipelines(_vkContext.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &irradianceCubeMap_pipeline) != VK_SUCCESS)
		std::cout << "Failed to create Irradiance Cubemap Cubemap Pipeline" << std::endl;

	pipelineInfo.stage.module = specularCubemapShader;
	pipelineInfo.layout = specularCubeMap_pipelineLayout;

	if (vkCreateComputePipelines(_vkContext.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &specularCubeMap_pipeline) != VK_SUCCESS)
		std::cout << "Failed to create Specular Cubemap Pipeline" << std::endl;

	pipelineInfo.stage.module = specularLUTShader;
	pipelineInfo.layout = specularLUT_pipelineLayout;

	if (vkCreateComputePipelines(_vkContext.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &specularLUT_pipeline) != VK_SUCCESS)
		std::cout << "Failed to create Specular LUT Pipeline" << std::endl;

	vkDestroyShaderModule(_vkContext.device, hdrImageSampleShader, nullptr);
	vkDestroyShaderModule(_vkContext.device, irradianceCubemapShader, nullptr);
	vkDestroyShaderModule(_vkContext.device, specularCubemapShader, nullptr);
	vkDestroyShaderModule(_vkContext.device, specularLUTShader, nullptr);

	//Setup Commands and Sync
	VkCommandPool hdr_commandPool;
	VkCommandBuffer hdr_commandBuffer;
	VkFence cubeMap_computeFence; //Indicates that a compute has finished

	VkCommandPoolCreateInfo cmdPoolInfo{};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.pNext = nullptr;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cmdPoolInfo.queueFamilyIndex = _vkContext.primaryQueueFamily;

	VkFenceCreateInfo fenceCreateInfo{};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;

	VK_CHECK(vkCreateCommandPool(_vkContext.device, &cmdPoolInfo, nullptr, &hdr_commandPool));

	VkCommandBufferAllocateInfo cmdAllocInfo{};
	cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAllocInfo.pNext = nullptr;
	cmdAllocInfo.commandPool = hdr_commandPool;
	cmdAllocInfo.commandBufferCount = 1;
	cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VK_CHECK(vkAllocateCommandBuffers(_vkContext.device, &cmdAllocInfo, &hdr_commandBuffer));

	VK_CHECK(vkCreateFence(_vkContext.device, &fenceCreateInfo, nullptr, &cubeMap_computeFence));

	//Compute
	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.pInheritanceInfo = nullptr;

	//-Make sure first that Cubemap resolutions are divisble by number of invocations
	if (_hdrCubeMap.extent.width % 16 != 0 && _hdrCubeMap.extent.height % 16 != 0)
		throw std::runtime_error("HDR Cubemap Extent is not divisble with number of Compute Shaders Invocations");
	if (_hdrIrradianceCubeMap.extent.width % 16 != 0 && _hdrIrradianceCubeMap.extent.height % 16 != 0)
		throw std::runtime_error("HDR Irradiance Cubemap Extent is not divisble with number of Compute Shaders Invocations");
	if (_hdrSpecularCubeMap.extent.width % 8 != 0 && _hdrSpecularCubeMap.extent.height % 8 != 0)
		throw std::runtime_error("HDR Specular Cubemap Extent is not divisble with number of Compute Shaders Invocations");
	if (_hdrSpecularLUT.extent.width % 8 != 0 && _hdrSpecularLUT.extent.height % 8 != 0)
		throw std::runtime_error("HDR Specular LUT Extent is not divisble with number of Compute Shaders Invocations");

	VK_CHECK(vkBeginCommandBuffer(hdr_commandBuffer, &cmdBeginInfo));

	//-HDR Equirrectangule Image Sample
	_vkContext.transition_image(hdr_commandBuffer, hdrImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	_vkContext.transition_image(hdr_commandBuffer, _hdrCubeMap, VK_IMAGE_LAYOUT_GENERAL);
	_vkContext.transition_image(hdr_commandBuffer, _hdrIrradianceCubeMap, VK_IMAGE_LAYOUT_GENERAL);
	_vkContext.transition_image(hdr_commandBuffer, _hdrSpecularCubeMap, VK_IMAGE_LAYOUT_GENERAL);
	_vkContext.transition_image(hdr_commandBuffer, _hdrSpecularLUT, VK_IMAGE_LAYOUT_GENERAL);

	vkCmdBindPipeline(hdr_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, hdrImageSample_pipeline);
	vkCmdBindDescriptorSets(hdr_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, hdrCubemap_pipelineLayout, 0, 1, &hdrImageSample_descriptorSet, 0, nullptr);

	vkCmdDispatch(hdr_commandBuffer, _hdrCubeMap.extent.width / 16, _hdrCubeMap.extent.height / 16, 6);

	//-Generate Mipmap Images for HDR Cube Map
	_vkContext.generate_mipmaps(hdr_commandBuffer, _hdrCubeMap, 5, 6);

	//-Irradiace Cubemap
	_vkContext.transition_image(hdr_commandBuffer, _hdrCubeMap, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	vkCmdBindPipeline(hdr_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, irradianceCubeMap_pipeline);
	vkCmdBindDescriptorSets(hdr_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, hdrCubemap_pipelineLayout, 0, 1, &irradianceCubeMap_descriptorSet, 0, nullptr);

	vkCmdDispatch(hdr_commandBuffer, _hdrIrradianceCubeMap.extent.width / 16, _hdrIrradianceCubeMap.extent.height / 16, 6);

	//-Specular Cubemap
	vkCmdBindPipeline(hdr_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, specularCubeMap_pipeline);
	vkCmdBindDescriptorSets(hdr_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, specularCubeMap_pipelineLayout, 0, 1, &specularCubeMap_descriptorSet, 0, nullptr);

	SpecularCubemapShader::PushConstants specularCubemap_PC;

	for (int mip = 0; mip < HDR_SPECULAR_CUBEMAP_MIP_LEVELS_COUNT; mip++) {
		uint32_t mipWidth = _hdrSpecularCubeMap.extent.width * std::pow(0.5, mip);
		uint32_t mipHeight = _hdrSpecularCubeMap.extent.height * std::pow(0.5, mip);

		specularCubemap_PC.mipLevel = mip;
		specularCubemap_PC.width = mipWidth;
		specularCubemap_PC.height = mipHeight;
		specularCubemap_PC.roughness = (float)mip / (float)(HDR_SPECULAR_CUBEMAP_MIP_LEVELS_COUNT - 1);
		vkCmdPushConstants(hdr_commandBuffer, specularCubeMap_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SpecularCubemapShader::PushConstants), &specularCubemap_PC);
		vkCmdDispatch(hdr_commandBuffer, mipWidth / 8, mipHeight / 8, 6);
	}

	//Specular LUT
	vkCmdBindPipeline(hdr_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, specularLUT_pipeline);
	vkCmdBindDescriptorSets(hdr_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, specularLUT_pipelineLayout, 0, 1, &specularLUT_descriptorSet, 0, nullptr);
	
	vkCmdDispatch(hdr_commandBuffer, _hdrSpecularLUT.extent.width / 8, _hdrSpecularLUT.extent.height / 8, 1);

	VK_CHECK(vkEndCommandBuffer(hdr_commandBuffer));

	//Command Submission
	VkCommandBufferSubmitInfo cmdInfo = vkutil::command_buffer_submit_info(hdr_commandBuffer);
	VkSubmitInfo2 submit = vkutil::submit_info(&cmdInfo, nullptr, nullptr);

	VK_CHECK(vkQueueSubmit2(_vkContext.primaryQueue, 1, &submit, cubeMap_computeFence));

	//-Wait for Compute commands for both to finish
	VK_CHECK(vkWaitForFences(_vkContext.device, 1, &cubeMap_computeFence, true, 1000000000));

	//Delete Resources
	vkDestroyFence(_vkContext.device, cubeMap_computeFence, nullptr);
	vkDestroyCommandPool(_vkContext.device, hdr_commandPool, nullptr);
	vkDestroyPipeline(_vkContext.device, specularLUT_pipeline, nullptr);
	vkDestroyPipeline(_vkContext.device, specularCubeMap_pipeline, nullptr);
	vkDestroyPipeline(_vkContext.device, irradianceCubeMap_pipeline, nullptr);
	vkDestroyPipeline(_vkContext.device, hdrImageSample_pipeline, nullptr);
	vkDestroyPipelineLayout(_vkContext.device, specularLUT_pipelineLayout, nullptr);
	vkDestroyPipelineLayout(_vkContext.device, specularCubeMap_pipelineLayout, nullptr);
	vkDestroyPipelineLayout(_vkContext.device, hdrCubemap_pipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(_vkContext.device, specularLUT_descriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(_vkContext.device, specularCubeMap_descriptorSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(_vkContext.device, hdrCubeMap_descriptorSetLayout, nullptr);
	vkDestroyDescriptorPool(_vkContext.device, hdrCubeMap_descriptorPool, nullptr);

	for (VkImageView& imgView : hdrSpecularCubeMap_ImageViews) {
		vkDestroyImageView(_vkContext.device, imgView, nullptr);
	}

	_vkContext.destroy_sampler(hdrImage_Sampler);
	_vkContext.destroy_image(hdrImage);
}

//Sets up the Resources needed to render a skybox
void RenderSystem::setup_skybox() {
	//Setup Shaders and Pipeline for Skybox Rendering
	//-Vertex Input Bindings
	std::vector<VkVertexInputBindingDescription> skybox_vertexInputBindings = { {} };

	skybox_vertexInputBindings[0].binding = 0;
	skybox_vertexInputBindings[0].stride = sizeof(glm::vec3);
	skybox_vertexInputBindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	//-Vertex Input Attribute
	std::vector<VkVertexInputAttributeDescription> skybox_vertexInputAttributes = { {} };

	//--Position
	skybox_vertexInputAttributes[0].binding = 0;
	skybox_vertexInputAttributes[0].location = 0;
	skybox_vertexInputAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	skybox_vertexInputAttributes[0].offset = 0;

	//-Create Descriptor Pool
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 },
		{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 }
	};

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = 1;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

	if (vkCreateDescriptorPool(_vkContext.device, &poolInfo, nullptr, &_skyboxDescriptorPool) != VK_SUCCESS)
		throw std::runtime_error("Failed to create Skybox Descriptor Pool");

	//-Set Binding Flags
	std::vector<VkDescriptorBindingFlags> binding_flags = {
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT,
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
	};

	VkDescriptorSetLayoutBindingFlagsCreateInfo set_binding_flags{};
	set_binding_flags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	set_binding_flags.bindingCount = static_cast<uint32_t>(binding_flags.size());
	set_binding_flags.pBindingFlags = binding_flags.data();

	//-Set Layout Bindings
	std::vector<VkDescriptorSetLayoutBinding> layout_bindings = {
		{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT, .pImmutableSamplers = nullptr },
		{.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr }
	};

	//-Now Create Descriptor Set Layout
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext = &set_binding_flags;
	layoutInfo.bindingCount = static_cast<uint32_t>(layout_bindings.size());
	layoutInfo.pBindings = layout_bindings.data();
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

	if (vkCreateDescriptorSetLayout(_vkContext.device, &layoutInfo, nullptr, &_skyboxDescriptorSetLayout) != VK_SUCCESS)
		throw std::runtime_error("Failed to Create Skybox Descriptor Set Layout");

	//-Create Descriptor Set
	VkDescriptorSetAllocateInfo descriptorSetallocInfo{};
	descriptorSetallocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorSetallocInfo.descriptorPool = _skyboxDescriptorPool;
	descriptorSetallocInfo.descriptorSetCount = 1;
	descriptorSetallocInfo.pSetLayouts = &_skyboxDescriptorSetLayout;

	if (vkAllocateDescriptorSets(_vkContext.device, &descriptorSetallocInfo, &_skyboxDescriptorSet) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate Skybox Descriptor Set");

	//-Pipeline
	//--Load SHaders
	VkShaderModule vertexShader;
	if (!vkutil::load_shader_module("shaders/skybox_vert.spv", _vkContext.device, &vertexShader))
		throw std::runtime_error("Error trying to create Skybox Vertex Shader Module");
	else
		std::cout << "Cube Map Vertex Shader successfully loaded" << std::endl;

	VkShaderModule fragShader;
	if (!vkutil::load_shader_module("shaders/skybox_frag.spv", _vkContext.device, &fragShader))
		throw std::runtime_error("error trying to create Skybox Frag Shader Module");
	else
		std::cout << "Cube Map Fragment Shader successfully loaded" << std::endl;

	//--Set Pipeline Layout - Descriptor Set
	VkPipelineLayoutCreateInfo pipeline_layout_info = vkutil::pipeline_layout_create_info();
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &_skyboxDescriptorSetLayout;
	pipeline_layout_info.pushConstantRangeCount = 0;
	pipeline_layout_info.pPushConstantRanges = nullptr;

	VK_CHECK(vkCreatePipelineLayout(_vkContext.device, &pipeline_layout_info, nullptr, &_skyboxPipelineLayout));

	//--Build Pipeline
	PipelineBuilder pipelineBuilder;
	pipelineBuilder._pipelineLayout = _skyboxPipelineLayout;
	pipelineBuilder.set_shaders(vertexShader, fragShader);
	pipelineBuilder.set_vertex_input(skybox_vertexInputBindings, skybox_vertexInputAttributes);
	pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	pipelineBuilder.set_multisampling_none();
	pipelineBuilder.disable_blending();
	pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
	pipelineBuilder.set_color_attachment_format(VK_FORMAT_B8G8R8A8_UNORM);
	pipelineBuilder.set_depth_format(VK_FORMAT_D32_SFLOAT);

	_skyboxPipeline = pipelineBuilder.build_pipeline(_vkContext.device);

	vkDestroyShaderModule(_vkContext.device, vertexShader, nullptr);
	vkDestroyShaderModule(_vkContext.device, fragShader, nullptr);

	//Setup Neccesary Buffers for the Rendering CubeMap

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
	_skyboxVertexBuffer = _vkContext.create_buffer("Skybox Vertex Buffer", unitCube_vertices.size() * sizeof(glm::vec3), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);
	_skyboxIndexBuffer = _vkContext.create_buffer("Skybox Index Buffer", unitCube_indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, allocFlags);

	VkBufferCopy bufferCpy = { .srcOffset = 0, .dstOffset = 0, .size = unitCube_vertices.size() * sizeof(glm::vec3) };
	_vkContext.update_buffer(_skyboxVertexBuffer, unitCube_vertices.data(), unitCube_vertices.size() * sizeof(glm::vec3), bufferCpy);
	bufferCpy.size = unitCube_indices.size() * sizeof(uint32_t);
	_vkContext.update_buffer(_skyboxIndexBuffer, unitCube_indices.data(), unitCube_indices.size() * sizeof(uint32_t), bufferCpy);
}

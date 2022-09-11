#include "VKRenderer.h"

#include <stdexcept>
#include <cmath>
#include <sstream>
#include <fstream>
#include <iostream>

VKRenderer::VKRenderer(const std::string& windowName, int32_t windowWidth, int32_t windowHeight)
	: width(windowWidth), height(windowHeight)
{
	if (!glfwInit())
	{
		throw std::runtime_error("Failed to initialize glfw!");
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	window = glfwCreateWindow(width, height, windowName.c_str(), nullptr, nullptr);

	if (!window)
	{
		glfwTerminate();
		throw std::runtime_error("Failed to create a window!");
	}

	InitVulkan(windowName);
	InitSwapchain();
	InitCommands();
	InitDefaultRenderpass();
	InitFramebuffers();
	InitSyncStructures();
	InitPipelines();
}

void VKRenderer::InitVulkan(const std::string& windowName)
{
	vkb::InstanceBuilder builder;
	vkbInstance = builder.set_app_name(windowName.c_str())
		.request_validation_layers(true)
		.require_api_version(1, 1, 0)
		.use_default_debug_messenger()
		.build()
		.value();

	instance = vkbInstance.instance;
	debugMessenger = vkbInstance.debug_messenger;

	VkResult error = glfwCreateWindowSurface(instance, window, nullptr, &surface);
	CheckVkError(error);

	vkb::PhysicalDeviceSelector selector{ vkbInstance };
	vkb::PhysicalDevice physicalDevice = selector
		.set_minimum_version(1, 1)
		.set_surface(surface)
		.select()
		.value();

	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	vkbDevice = deviceBuilder.build().value();
	device = vkbDevice.device;
	chosenGPU = physicalDevice.physical_device;

	graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
}

void VKRenderer::InitSwapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{ vkbDevice, surface };
	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(static_cast<uint32_t>(width), static_cast<uint32_t>(height))
		.build()
		.value();

	swapchain = vkbSwapchain.swapchain;
	swapchainImages = vkbSwapchain.get_images().value();
	swapchainImageViews = vkbSwapchain.get_image_views().value();
	swapchainImageFormat = vkbSwapchain.image_format;

	swapchainDeletionList.push_back([=]() {
		vkDestroySwapchainKHR(device, swapchain, nullptr);
		});
}

void VKRenderer::RecreateSwapchain()
{
	// If the window is minimized, wait.
	int32_t frameWidth;
	int32_t frameHeight;

	do
	{
		glfwGetFramebufferSize(window, &frameWidth, &frameHeight);
		glfwWaitEvents();
	} while (frameWidth == 0 || frameHeight == 0);

	vkDeviceWaitIdle(device);

	glfwGetWindowSize(window, &width, &height);

	FlushDeletionList(swapchainDeletionList);

	InitSwapchain();
	InitFramebuffers();
}

void VKRenderer::InitCommands()
{
	VkCommandPoolCreateInfo commandPoolInfo = CommandPoolCreateInfo(graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	VkResult err = vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool);
	CheckVkError(err);

	VkCommandBufferAllocateInfo commandAllocInfo = CommandBufferAllocateInfo(commandPool, 1);
	err = vkAllocateCommandBuffers(device, &commandAllocInfo, &mainCommandBuffer);
	CheckVkError(err);

	deletionList.push_back([=]() {
		vkDestroyCommandPool(device, commandPool, nullptr);
		});
}

void VKRenderer::InitDefaultRenderpass()
{
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = swapchainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	VkResult err = vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass);
	CheckVkError(err);

	deletionList.push_back([=]() {
		vkDestroyRenderPass(device, renderPass, nullptr);
		});
}

void VKRenderer::InitFramebuffers()
{
	VkFramebufferCreateInfo fbInfo = {};
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.pNext = nullptr;
	fbInfo.renderPass = renderPass;
	fbInfo.attachmentCount = 1;
	fbInfo.width = static_cast<uint32_t>(width);
	fbInfo.height = static_cast<uint32_t>(height);
	fbInfo.layers = 1;

	const uint32_t swapchainImageCount = swapchainImages.size();
	framebuffers = std::vector<VkFramebuffer>(swapchainImageCount);

	for (int i = 0; i < swapchainImageCount; ++i)
	{
		fbInfo.pAttachments = &swapchainImageViews[i];
		VkResult err = vkCreateFramebuffer(device, &fbInfo, nullptr, &framebuffers[i]);
		CheckVkError(err);

		swapchainDeletionList.push_back([=]() {
			vkDestroyFramebuffer(device, framebuffers[i], nullptr);
			vkDestroyImageView(device, swapchainImageViews[i], nullptr);
			});
	}
}

void VKRenderer::InitSyncStructures()
{
	VkFenceCreateInfo fenceCreateInfo = FenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
	VkResult err = vkCreateFence(device, &fenceCreateInfo, nullptr, &renderFence);
	CheckVkError(err);

	VkSemaphoreCreateInfo semaphoreCreateInfo = SemaphoreCreateInfo(0);
	err = vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &presentSemaphore);
	CheckVkError(err);
	err = vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &renderSemaphore);
	CheckVkError(err);

	deletionList.push_back([=]() {
		vkDestroySemaphore(device, presentSemaphore, nullptr);
		vkDestroySemaphore(device, renderSemaphore, nullptr);
		});
}

VkCommandPoolCreateInfo VKRenderer::CommandPoolCreateInfo(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags)
{
	VkCommandPoolCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	info.pNext = nullptr;

	info.queueFamilyIndex = queueFamilyIndex;
	info.flags = flags;

	return info;
}

VkCommandBufferAllocateInfo VKRenderer::CommandBufferAllocateInfo(VkCommandPool pool, uint32_t count, VkCommandBufferLevel level)
{
	VkCommandBufferAllocateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	info.pNext = nullptr;

	info.commandPool = pool;
	info.commandBufferCount = count;
	info.level = level;

	return info;
}

VkPipelineShaderStageCreateInfo VKRenderer::PipelineShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule shaderModule)
{
	VkPipelineShaderStageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	info.pNext = nullptr;
	info.stage = stage;
	info.module = shaderModule;
	info.pName = "main";

	return info;
}

VkPipelineVertexInputStateCreateInfo VKRenderer::VertexInputStateCreateInfo()
{
	VkPipelineVertexInputStateCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	info.pNext = nullptr;
	info.vertexBindingDescriptionCount = 0;
	info.vertexAttributeDescriptionCount = 0;

	return info;
}

VkPipelineInputAssemblyStateCreateInfo VKRenderer::InputAssemblyCreateInfo(VkPrimitiveTopology topology)
{
	VkPipelineInputAssemblyStateCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	info.pNext = nullptr;
	info.topology = topology;
	info.primitiveRestartEnable = VK_FALSE;

	return info;
}

VkPipelineRasterizationStateCreateInfo VKRenderer::RasterizationStateCreateInfo(VkPolygonMode polygonMode)
{
	VkPipelineRasterizationStateCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	info.pNext = nullptr;
	info.depthClampEnable = VK_FALSE;
	info.rasterizerDiscardEnable = VK_FALSE;
	info.polygonMode = polygonMode;
	info.lineWidth = 1.0f;
	info.cullMode = VK_CULL_MODE_NONE;
	info.frontFace = VK_FRONT_FACE_CLOCKWISE;
	info.depthBiasEnable = VK_FALSE;
	info.depthBiasConstantFactor = 0.0f;
	info.depthBiasClamp = 0.0f;
	info.depthBiasSlopeFactor = 0.0f;

	return info;
}

VkPipelineMultisampleStateCreateInfo VKRenderer::MulitsamplingStateCreateInfo()
{
	VkPipelineMultisampleStateCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	info.pNext = nullptr;
	info.sampleShadingEnable = VK_FALSE;
	info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	info.minSampleShading = 1.0f;
	info.pSampleMask = nullptr;
	info.alphaToCoverageEnable = VK_FALSE;
	info.alphaToOneEnable = VK_FALSE;

	return info;
}

VkPipelineColorBlendAttachmentState VKRenderer::ColorBlendAttachmentState()
{
	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;

	return colorBlendAttachment;
}

VkPipelineLayoutCreateInfo VKRenderer::PipelineLayoutCreateInfo()
{
	VkPipelineLayoutCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	info.pNext = nullptr;
	info.flags = 0;
	info.setLayoutCount = 0;
	info.pSetLayouts = nullptr;
	info.pushConstantRangeCount = 0;
	info.pPushConstantRanges = nullptr;

	return info;
}

VkFenceCreateInfo VKRenderer::FenceCreateInfo(VkFenceCreateFlags flags)
{
	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;
	fenceCreateInfo.flags = flags;

	return fenceCreateInfo;
}

VkSemaphoreCreateInfo VKRenderer::SemaphoreCreateInfo(VkSemaphoreCreateFlags flags)
{
	VkSemaphoreCreateInfo semCreateInfo = {};
	semCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semCreateInfo.pNext = nullptr;
	semCreateInfo.flags = flags;

	return semCreateInfo;
}

VkViewport VKRenderer::DefaultViewport()
{
	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(width);
	viewport.height = static_cast<float>(height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	return viewport;
}

VkRect2D VKRenderer::DefaultScissor()
{
	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = VkExtent2D{
		static_cast<uint32_t>(width),
		static_cast<uint32_t>(height),
	};

	return scissor;
}

VkPipeline PipelineBuilder::BuildPipeline(VkDevice device, VkRenderPass pass)
{
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.pNext = nullptr;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.pNext = nullptr;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
	dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateInfo.pNext = nullptr;
	dynamicStateInfo.pDynamicStates = dynamicStates;
	dynamicStateInfo.dynamicStateCount = 2;

	pipelineInfo.stageCount = shaderStages.size();
	pipelineInfo.pStages = shaderStages.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicStateInfo;
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = pass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	VkPipeline newPipeline;

	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create pipeline");
	}
	else
	{
		return newPipeline;
	}
}

void VKRenderer::FlushDeletionList(std::vector<std::function<void()>>& list)
{
	for (auto it : list)
	{
		it();
	}

	list.clear();
}

void VKRenderer::CleanupVulkan()
{
	vkDeviceWaitIdle(device);
	vkDestroyFence(device, renderFence, nullptr);

	FlushDeletionList(swapchainDeletionList);
	FlushDeletionList(deletionList);

	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkb::destroy_device(vkbDevice);
	vkb::destroy_instance(vkbInstance);
}

void VKRenderer::CloseWindow()
{
	CleanupVulkan();
	glfwTerminate();
}

void VKRenderer::ResizeWindow(int32_t width, int32_t height)
{

}

GLFWwindow* VKRenderer::GetWindowPtr()
{
	return window;
}

void VKRenderer::SetClearColor(float r, float g, float b, float a)
{
}

void VKRenderer::BeginDrawing()
{
	VkResult err = vkWaitForFences(device, 1, &renderFence, true, 1'000'000'000);
	CheckVkError(err);

	uint32_t swapchainImageIndex;
	err = vkAcquireNextImageKHR(device, swapchain, 1'000'000'000, presentSemaphore, nullptr, &swapchainImageIndex);

	if (err == VK_ERROR_OUT_OF_DATE_KHR)
	{
		RecreateSwapchain();
		return;
	}
	else
	{
		CheckVkError(err);
	}

	// Only reset if we are submitting work.
	err = vkResetFences(device, 1, &renderFence);
	CheckVkError(err);

	err = vkResetCommandBuffer(mainCommandBuffer, 0);
	CheckVkError(err);

	VkCommandBuffer cmd = mainCommandBuffer;
	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	err = vkBeginCommandBuffer(cmd, &cmdBeginInfo);
	CheckVkError(err);

	VkClearValue clearValue;
	clearValue.color = { { 0.0f, 0.0f, 0.5f, 1.0f } };

	VkRenderPassBeginInfo rpInfo = {};
	rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpInfo.pNext = nullptr;
	rpInfo.renderPass = renderPass;
	rpInfo.renderArea.offset.x = 0;
	rpInfo.renderArea.offset.y = 0;
	rpInfo.renderArea.extent = VkExtent2D{
		static_cast<uint32_t>(width),
		static_cast<uint32_t>(height)
	};
	rpInfo.framebuffer = framebuffers[swapchainImageIndex];
	rpInfo.clearValueCount = 1;
	rpInfo.pClearValues = &clearValue;

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);

		VkViewport viewport = DefaultViewport();
		VkRect2D scissor = DefaultScissor();
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdDraw(cmd, 3, 1, 0, 0);
	}
	vkCmdEndRenderPass(cmd);
	CheckVkError(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	submit.pWaitDstStageMask = &waitStage;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &presentSemaphore;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &renderSemaphore;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	err = vkQueueSubmit(graphicsQueue, 1, &submit, renderFence);
	CheckVkError(err);

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pImageIndices = &swapchainImageIndex;

	err = vkQueuePresentKHR(graphicsQueue, &presentInfo);

	if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
	{
		RecreateSwapchain();
	}
	else
	{
		CheckVkError(err);
	}
}

void VKRenderer::InitPipelines()
{
	VkShaderModule triangleFragShader;
	VkShaderModule triangleVertexShader;

	if (!LoadShaderModule("shaders/triangle.frag.spv", &triangleFragShader))
	{
		throw std::runtime_error("Error when building the triangle fragment shader module");
	}
	else
	{
		std::cout << "Triangle fragment shader successfully loaded!\n";
	}

	if (!LoadShaderModule("shaders/triangle.vert.spv", &triangleVertexShader))
	{
		throw std::runtime_error("Error when building the triangle vertex shader module");
	}
	else
	{
		std::cout << "Triangle vertex shader successfully loaded!\n";
	}

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = PipelineLayoutCreateInfo();
	VkResult err = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &trianglePipelineLayout);
	CheckVkError(err);

	PipelineBuilder pipelineBuilder;

	pipelineBuilder.shaderStages.push_back(
		PipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, triangleVertexShader));

	pipelineBuilder.shaderStages.push_back(
		PipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader));

	pipelineBuilder.vertexInputInfo = VertexInputStateCreateInfo();
	pipelineBuilder.inputAssembly = InputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.viewport = DefaultViewport();
	pipelineBuilder.scissor = DefaultScissor();

	pipelineBuilder.rasterizer = RasterizationStateCreateInfo(VK_POLYGON_MODE_FILL);
	pipelineBuilder.multisampling = MulitsamplingStateCreateInfo();
	pipelineBuilder.colorBlendAttachment = ColorBlendAttachmentState();
	pipelineBuilder.pipelineLayout = trianglePipelineLayout;

	trianglePipeline = pipelineBuilder.BuildPipeline(device, renderPass);

	vkDestroyShaderModule(device, triangleFragShader, nullptr);
	vkDestroyShaderModule(device, triangleVertexShader, nullptr);

	deletionList.push_back([=]() {
		vkDestroyPipeline(device, trianglePipeline, nullptr);
		vkDestroyPipelineLayout(device, trianglePipelineLayout, nullptr);
		});
}

void VKRenderer::EndDrawing()
{
	glfwSwapBuffers(window);
	glfwPollEvents();
}

void VKRenderer::DrawModel(const Model* model, const TextureArray* textureArray, const Instances* instances)
{
}

void VKRenderer::DrawSprite(const Model* model, const TextureArray* textureArray, const Instances* instances)
{
}

Model VKRenderer::CreateModel(const std::vector<float>& vertices, const std::vector<uint32_t>& indices)
{
	return Model{};
}

void VKRenderer::UpdateModel(Model* model, const std::vector<float>& vertices, const std::vector<uint32_t>& indices)
{
}

void VKRenderer::DestroyModel(Model* model)
{
}

TextureArray VKRenderer::CreateTextureArray(const std::vector<std::string>& images)
{
	return TextureArray{};
}

void VKRenderer::DestroyTextureArray(TextureArray* textureArray)
{
}

void VKRenderer::UpdateCamera()
{
}

void VKRenderer::SetCameraPosition(glm::vec3 position)
{
}

void VKRenderer::SetCameraRotation(float yRot, float xRot)
{
}

void VKRenderer::ConfigureCamera(float fov)
{
}

bool VKRenderer::LoadShaderModule(const char* filePath, VkShaderModule* outShaderModule)
{
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		return false;
	}

	size_t fileSize = static_cast<size_t>(file.tellg());
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
	file.seekg(0);
	file.read((char*)buffer.data(), fileSize);
	file.close();

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;
	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	VkShaderModule shaderModule;

	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return false;
	}

	*outShaderModule = shaderModule;

	return true;
}

void VKRenderer::CheckVkError(VkResult err)
{
	if (err)
	{
		std::stringstream errStream;
		errStream << "Detected Vulkan error: " << err << "\n";
		throw std::runtime_error(errStream.str());
	}
}

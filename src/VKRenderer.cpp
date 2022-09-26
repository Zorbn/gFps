#include "VKRenderer.h"

#include <stdexcept>
#include <cmath>
#include <sstream>
#include <fstream>
#include <iostream>
#include <sstream>

#include <glm/gtx/transform.hpp>

#define VMA_IMPLEMENTATION
#include "../deps/vk_mem_alloc.h"

#include "../deps/stb_image.h"

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
	InitDescriptors();
	InitPipelines();

	LoadMeshes();
	LoadImages();

	VkSamplerCreateInfo samplerInfo = SamplerCreateInfo(VK_FILTER_NEAREST);
	VkSampler sampler = {};
	vkCreateSampler(device, &samplerInfo, nullptr, &sampler);

	deletionList.push_back([=]() {
		vkDestroySampler(device, sampler, nullptr);
		});

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.pNext = nullptr;
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &singleTextureSetLayout;

	vkAllocateDescriptorSets(device, &allocInfo, &testTextureSet);

	VkDescriptorImageInfo imageBufferInfo;
	imageBufferInfo.sampler = sampler;
	imageBufferInfo.imageView = loadedTextures["test"].imageView;
	imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet texture1 = WriteDescriptorImage(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, testTextureSet, &imageBufferInfo, 0);

	vkUpdateDescriptorSets(device, 1, &texture1, 0, nullptr);
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

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = chosenGPU;
	allocatorInfo.device = device;
	allocatorInfo.instance = instance;
	vmaCreateAllocator(&allocatorInfo, &allocator);

	gpuProperties = vkbDevice.physical_device.properties;
}

void VKRenderer::InitSwapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{ vkbDevice, surface };
	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		.set_desired_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR)
		.set_desired_extent(static_cast<uint32_t>(width), static_cast<uint32_t>(height))
		.build()
		.value();

	swapchain = vkbSwapchain.swapchain;
	swapchainImages = vkbSwapchain.get_images().value();
	swapchainImageViews = vkbSwapchain.get_image_views().value();
	swapchainImageFormat = vkbSwapchain.image_format;

	VkExtent3D depthImageExtent = {
		static_cast<uint32_t>(width),
		static_cast<uint32_t>(height),
		1,
	};

	depthFormat = VK_FORMAT_D32_SFLOAT;

	VkImageCreateInfo depthImgInfo = ImageCreateInfo(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImageExtent);

	VmaAllocationCreateInfo depthImgAllocInfo = {};
	depthImgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	depthImgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(allocator, &depthImgInfo, &depthImgAllocInfo, &depthImage.image, &depthImage.allocation, nullptr);
	VkImageViewCreateInfo depthViewInfo = ImageViewCreateInfo(depthFormat, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);
	VkResult err = vkCreateImageView(device, &depthViewInfo, nullptr, &depthImageView);

	swapchainDeletionList.push_back([=]() {
		vkDestroyImageView(device, depthImageView, nullptr);
		vmaDestroyImage(allocator, depthImage.image, depthImage.allocation);
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

	for (int i = 0; i < frameOverlap; ++i)
	{
		VkResult err = vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frames[i].commandPool);
		CheckVkError(err);

		VkCommandBufferAllocateInfo commandAllocInfo = CommandBufferAllocateInfo(frames[i].commandPool, 1);
		err = vkAllocateCommandBuffers(device, &commandAllocInfo, &frames[i].mainCommandBuffer);
		CheckVkError(err);

		deletionList.push_back([=]() {
			vkDestroyCommandPool(device, frames[i].commandPool, nullptr);
			});
	}

	VkCommandPoolCreateInfo uploadCommandPoolInfo = CommandPoolCreateInfo(graphicsQueueFamily, 0);
	VkResult err = vkCreateCommandPool(device, &uploadCommandPoolInfo, nullptr, &uploadContext.commandPool);
	CheckVkError(err);

	deletionList.push_back([=]() {
		vkDestroyCommandPool(device, uploadContext.commandPool, nullptr);
		});

	VkCommandBufferAllocateInfo cmdAllocInfo = CommandBufferAllocateInfo(uploadContext.commandPool, 1);
	VkCommandBuffer cmd = {};
	err = vkAllocateCommandBuffers(device, &cmdAllocInfo, &uploadContext.commandBuffer);
	CheckVkError(err);
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

	VkAttachmentDescription depthAttachment = {};
	depthAttachment.flags = 0;
	depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency depthDependency = {};
	depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	depthDependency.dstSubpass = 0;
	depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.srcAccessMask = 0;
	depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency dependencies[2] = { dependency, depthDependency };

	VkAttachmentDescription attachments[2] = { colorAttachment, depthAttachment };

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = &attachments[0];
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 2;
	renderPassInfo.pDependencies = &dependencies[0];

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
		VkImageView attachments[2];
		attachments[0] = swapchainImageViews[i];
		attachments[1] = depthImageView;

		fbInfo.pAttachments = attachments;
		fbInfo.attachmentCount = 2;

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
	VkSemaphoreCreateInfo semaphoreCreateInfo = SemaphoreCreateInfo(0);

	for (int i = 0; i < frameOverlap; ++i)
	{
		VkResult err = vkCreateFence(device, &fenceCreateInfo, nullptr, &frames[i].renderFence);
		CheckVkError(err);

		err = vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frames[i].presentSemaphore);
		CheckVkError(err);
		err = vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frames[i].renderSemaphore);
		CheckVkError(err);

		deletionList.push_back([=]() {
			vkDestroyFence(device, frames[i].renderFence, nullptr);
			vkDestroySemaphore(device, frames[i].presentSemaphore, nullptr);
			vkDestroySemaphore(device, frames[i].renderSemaphore, nullptr);
			});
	}

	VkFenceCreateInfo uploadFenceCreateInfo = FenceCreateInfo(0);
	VkResult err = vkCreateFence(device, &uploadFenceCreateInfo, nullptr, &uploadContext.uploadFence);
	CheckVkError(err);

	deletionList.push_back([=]() {
		vkDestroyFence(device, uploadContext.uploadFence, nullptr);
		});
}

void VKRenderer::InitDescriptors()
{
	std::vector<VkDescriptorPoolSize> sizes = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 },
	};

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = 0;
	poolInfo.maxSets = 10;
	poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
	poolInfo.pPoolSizes = sizes.data();

	vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);

	VkDescriptorSetLayoutBinding camBufferBinding = {};
	camBufferBinding.binding = 0;
	camBufferBinding.descriptorCount = 1;
	camBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	camBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayoutCreateInfo camSetInfo = {};
	camSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	camSetInfo.pNext = nullptr;
	camSetInfo.bindingCount = 1;
	camSetInfo.flags = 0;
	camSetInfo.pBindings = &camBufferBinding;

	vkCreateDescriptorSetLayout(device, &camSetInfo, nullptr, &globalSetLayout);

	VkDescriptorSetLayoutBinding textureBinding = {};
	textureBinding.binding = 0;
	textureBinding.descriptorCount = 1;
	textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo textureSetInfo = {};
	textureSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	textureSetInfo.pNext = nullptr;
	textureSetInfo.bindingCount = 1;
	textureSetInfo.flags = 0;
	textureSetInfo.pBindings = &textureBinding;

	vkCreateDescriptorSetLayout(device, &textureSetInfo, nullptr, &singleTextureSetLayout);

	for (int i = 0; i < frameOverlap; ++i)
	{
		frames[i].cameraBuffer = CreateBuffer(sizeof(GPUCameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.pNext = nullptr;
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &globalSetLayout;

		vkAllocateDescriptorSets(device, &allocInfo, &frames[i].globalDescriptor);

		VkDescriptorBufferInfo bufferInfo = {};
		bufferInfo.buffer = frames[i].cameraBuffer.buffer;
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(GPUCameraData);

		VkWriteDescriptorSet setWrite = {};
		setWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		setWrite.pNext = nullptr;
		setWrite.dstBinding = 0;
		setWrite.dstSet = frames[i].globalDescriptor;
		setWrite.descriptorCount = 1;
		setWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		setWrite.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(device, 1, &setWrite, 0, nullptr);
	}

	for (int i = 0; i < frameOverlap; ++i)
	{
		deletionList.push_back([=]() {
			vmaDestroyBuffer(allocator, frames[i].cameraBuffer.buffer, frames[i].cameraBuffer.allocation);
			});
	}

	deletionList.push_back([&]() {
		vkDestroyDescriptorSetLayout(device, globalSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, singleTextureSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
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

VkImageCreateInfo VKRenderer::ImageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent)
{
	VkImageCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	info.pNext = nullptr;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.format = format;
	info.extent = extent;
	info.mipLevels = 1;
	info.arrayLayers = 1;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_OPTIMAL;
	info.usage = usageFlags;

	return info;
}

VkImageViewCreateInfo VKRenderer::ImageViewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags)
{
	VkImageViewCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.pNext = nullptr;
	info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info.image = image;
	info.format = format;
	info.subresourceRange.baseMipLevel = 0;
	info.subresourceRange.levelCount = 1;
	info.subresourceRange.baseArrayLayer = 0;
	info.subresourceRange.layerCount = 1;
	info.subresourceRange.aspectMask = aspectFlags;

	return info;
}

VkPipelineDepthStencilStateCreateInfo VKRenderer::DepthStencilCreateInfo(bool depthTest, bool depthWrite, VkCompareOp compareOp)
{
	VkPipelineDepthStencilStateCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    info.pNext = nullptr;

    info.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
    info.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
    info.depthCompareOp = depthTest ? compareOp : VK_COMPARE_OP_ALWAYS;
    info.depthBoundsTestEnable = VK_FALSE;
    info.minDepthBounds = 0.0f;
    info.maxDepthBounds = 1.0f;
    info.stencilTestEnable = VK_FALSE;

    return info;
}

VkCommandBufferBeginInfo VKRenderer::CommandBufferBeginInfo(VkCommandBufferUsageFlags flags)
{
	VkCommandBufferBeginInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	info.pNext = nullptr;
	info.pInheritanceInfo = nullptr;
	info.flags = flags;

	return info;
}

VkSubmitInfo VKRenderer::SubmitInfo(VkCommandBuffer* cmd)
{
	VkSubmitInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	info.pNext = nullptr;
	info.waitSemaphoreCount = 0;
	info.pWaitSemaphores = nullptr;
	info.pWaitDstStageMask = nullptr;
	info.commandBufferCount = 1;
	info.pCommandBuffers = cmd;
	info.signalSemaphoreCount = 0;
	info.pSignalSemaphores = nullptr;

	return info;
}

VkSamplerCreateInfo VKRenderer::SamplerCreateInfo(VkFilter filters, VkSamplerAddressMode samplerAddressMode)
{
	VkSamplerCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	info.pNext = nullptr;
	info.magFilter = filters;
	info.minFilter = filters;
	info.addressModeU = samplerAddressMode;
	info.addressModeV = samplerAddressMode;
	info.addressModeW = samplerAddressMode;

	return info;
}

VkWriteDescriptorSet VKRenderer::WriteDescriptorImage(VkDescriptorType type, VkDescriptorSet dstSet, VkDescriptorImageInfo* imageInfo, uint32_t binding)
{
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.pNext = nullptr;
	write.dstBinding = binding;
	write.dstSet = dstSet;
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pImageInfo = imageInfo;

	return write;
}

AllocatedBuffer VKRenderer::CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.usage = memoryUsage;

	AllocatedBuffer newBuffer = {};

	VkResult err = vmaCreateBuffer(allocator, &bufferInfo, &vmaAllocInfo,
		&newBuffer.buffer,
		&newBuffer.allocation,
		nullptr);
	CheckVkError(err);

	return newBuffer;
}

void VKRenderer::ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VkCommandBuffer cmd = uploadContext.commandBuffer;
	VkCommandBufferBeginInfo cmdBeginInfo = CommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	VkResult err = vkBeginCommandBuffer(cmd, &cmdBeginInfo);
	CheckVkError(err);

	function(cmd);

	err = vkEndCommandBuffer(cmd);
	CheckVkError(err);

	VkSubmitInfo submit = SubmitInfo(&cmd);
	
	err = vkQueueSubmit(graphicsQueue, 1, &submit, uploadContext.uploadFence);
	CheckVkError(err);

	vkWaitForFences(device, 1, &uploadContext.uploadFence, true, 9999999999);
	vkResetFences(device, 1, &uploadContext.uploadFence);

	vkResetCommandPool(device, uploadContext.commandPool, 0);
}

void VKRenderer::LoadImageFromFile(const char* file, AllocatedImage& outImage)
{
	int32_t texWidth, texHeight, texChannels;

	stbi_uc* pixels = stbi_load(file, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

	if (!pixels)
	{
		std::stringstream stream;
		stream << "Failed to load texture file: ";
		stream << file;
		throw std::runtime_error(stream.str());
	}

	void* pixelPtr = pixels;
	VkDeviceSize imageSize = texWidth * texHeight * 4;
	VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;

	AllocatedBuffer stagingBuffer = CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

	void* data;
	vmaMapMemory(allocator, stagingBuffer.allocation, &data);
	memcpy(data, pixelPtr, static_cast<size_t>(imageSize));
	vmaUnmapMemory(allocator, stagingBuffer.allocation);

	stbi_image_free(pixels);

	VkExtent3D imageExtent;
	imageExtent.width = static_cast<uint32_t>(texWidth);
	imageExtent.height = static_cast<uint32_t>(texHeight);
	imageExtent.depth = 1;

	VkImageCreateInfo imageInfo = ImageCreateInfo(imageFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, imageExtent);
	AllocatedImage newImage;
	VmaAllocationCreateInfo imageAllocInfo = {};
	imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	imageAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	vmaCreateImage(allocator, &imageInfo, &imageAllocInfo, &newImage.image, &newImage.allocation, nullptr);

	ImmediateSubmit([&](VkCommandBuffer cmd) {
		VkImageSubresourceRange range;
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = 0;
		range.levelCount = 1;
		range.baseArrayLayer = 0;
		range.layerCount = 1;

		VkImageMemoryBarrier imageBarrierToTransfer = {};
		imageBarrierToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageBarrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageBarrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrierToTransfer.image = newImage.image;
		imageBarrierToTransfer.subresourceRange = range;
		imageBarrierToTransfer.srcAccessMask = 0;
		imageBarrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &imageBarrierToTransfer);

		VkBufferImageCopy copyRegion = {};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;

		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = imageExtent;

		vkCmdCopyBufferToImage(cmd, stagingBuffer.buffer, newImage.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		VkImageMemoryBarrier imageBarrierToReadable = imageBarrierToTransfer;

		imageBarrierToReadable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrierToReadable.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		imageBarrierToReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageBarrierToReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &imageBarrierToReadable);
		});

	deletionList.push_back([=]() {
		vmaDestroyImage(allocator, newImage.image, newImage.allocation);
		});
	vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);

	outImage = newImage;
}

void VKRenderer::LoadImages()
{
	Texture test;
	LoadImageFromFile("res/test.png", test.image);
	VkImageViewCreateInfo imageInfo = ImageViewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, test.image.image, VK_IMAGE_ASPECT_COLOR_BIT);
	vkCreateImageView(device, &imageInfo, nullptr, &test.imageView);

	deletionList.push_back([=]() {
		vkDestroyImageView(device, test.imageView, nullptr);
		});

	loadedTextures["test"] = test;
}

void VKRenderer::LoadMeshes()
{
	triangleMesh.vertices.resize(3);

	triangleMesh.vertices[0].pos = { 1.0f, 1.0f, 0.0f };
	triangleMesh.vertices[1].pos = {-1.0f, 1.0f, 0.0f };
	triangleMesh.vertices[2].pos = { 0.0f,-1.0f, 0.0f };

	triangleMesh.vertices[0].color = { 0.0f, 1.0f, 0.0f };
	triangleMesh.vertices[1].color = { 0.0f, 1.0f, 0.0f };
	triangleMesh.vertices[2].color = { 0.0f, 1.0f, 0.0f };

	triangleMesh.vertices[0].uv = { 0.0f, 0.0f };
	triangleMesh.vertices[1].uv = { 0.0f, 1.0f };
	triangleMesh.vertices[2].uv = { 1.0f, 1.0f };

	UploadMesh(triangleMesh);
}

void VKRenderer::UploadMesh(Mesh& mesh)
{
	const size_t bufferSize = mesh.vertices.size() * sizeof(Vertex);

	VkBufferCreateInfo stagingBufferInfo = {};
	stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingBufferInfo.size = bufferSize;
	stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;


	VmaAllocationCreateInfo vmaAllocInfo = {};
	vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		VMA_ALLOCATION_CREATE_MAPPED_BIT;
	vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;

	AllocatedBuffer stagingBuffer;

	VkResult err = vmaCreateBuffer(allocator, &stagingBufferInfo, &vmaAllocInfo,
		&stagingBuffer.buffer, &stagingBuffer.allocation, nullptr);
	CheckVkError(err);

	deletionList.push_back([=]() {
        vmaDestroyBuffer(allocator, mesh.vertexBuffer.buffer, mesh.vertexBuffer.allocation);
		});

	void* data;
	vmaMapMemory(allocator, stagingBuffer.allocation, &data);
	memcpy(data, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
	vmaUnmapMemory(allocator, stagingBuffer.allocation);

	VkBufferCreateInfo vertexBufferInfo = {};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.pNext = nullptr;
	vertexBufferInfo.size = bufferSize;
	vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	err = vmaCreateBuffer(allocator, &vertexBufferInfo, &vmaAllocInfo,
		&mesh.vertexBuffer.buffer,
		&mesh.vertexBuffer.allocation,
		nullptr);
	CheckVkError(err);

	ImmediateSubmit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = bufferSize;
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, mesh.vertexBuffer.buffer, 1, &copy);
		});

	deletionList.push_back([=]() {
		vmaDestroyBuffer(allocator, mesh.vertexBuffer.buffer, mesh.vertexBuffer.allocation);
		});

	vmaDestroyBuffer(allocator, stagingBuffer.buffer, stagingBuffer.allocation);
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
	pipelineInfo.pDepthStencilState = &depthStencil;
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

VertexInputDescription Vertex::GetVertexDescription()
{
	VertexInputDescription description;

	VkVertexInputBindingDescription mainBinding = {};
	mainBinding.binding = 0;
	mainBinding.stride = sizeof(Vertex);
	mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	description.bindings.push_back(mainBinding);

	VkVertexInputAttributeDescription positionAttribute = {};
	positionAttribute.binding = 0;
	positionAttribute.location = 0;
	positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	positionAttribute.offset = offsetof(Vertex, pos);

	VkVertexInputAttributeDescription normalAttribute = {};
	normalAttribute.binding = 0;
	normalAttribute.location = 1;
	normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	normalAttribute.offset = offsetof(Vertex, normal);

	VkVertexInputAttributeDescription colorAttribute = {};
	colorAttribute.binding = 0;
	colorAttribute.location = 2;
	colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	colorAttribute.offset = offsetof(Vertex, color);

	VkVertexInputAttributeDescription uvAttribute = {};
	uvAttribute.binding = 0;
	uvAttribute.location = 3;
	uvAttribute.format = VK_FORMAT_R32G32_SFLOAT;
	uvAttribute.offset = offsetof(Vertex, uv);

	description.attributes.push_back(positionAttribute);
	description.attributes.push_back(normalAttribute);
	description.attributes.push_back(colorAttribute);
	description.attributes.push_back(uvAttribute);

	return description;
}

FrameData& VKRenderer::GetCurrentFrame()
{
	return frames[frameNumber % frameOverlap];
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

	FlushDeletionList(swapchainDeletionList);
	FlushDeletionList(deletionList);

	vmaDestroyAllocator(allocator);

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
	++frameNumber;

	FrameData& currentFrame = GetCurrentFrame();

	VkResult err = vkWaitForFences(device, 1, &currentFrame.renderFence, true, 1'000'000'000);
	CheckVkError(err);

	uint32_t swapchainImageIndex;
	err = vkAcquireNextImageKHR(device, swapchain, 1'000'000'000, currentFrame.presentSemaphore, nullptr, &swapchainImageIndex);

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
	err = vkResetFences(device, 1, &currentFrame.renderFence);
	CheckVkError(err);

	err = vkResetCommandBuffer(GetCurrentFrame().mainCommandBuffer, 0);
	CheckVkError(err);

	VkCommandBuffer cmd = currentFrame.mainCommandBuffer;
	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	err = vkBeginCommandBuffer(cmd, &cmdBeginInfo);
	CheckVkError(err);

	VkClearValue clearValue;
	clearValue.color = { { 0.0f, 0.0f, 0.5f, 1.0f } };

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.0f;

	VkClearValue clearValues[] = { clearValue, depthClear };

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
	rpInfo.clearValueCount = 2;
	rpInfo.pClearValues = &clearValues[0];

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
	{
		/*
		 * Push constant calculations:
		 */
		glm::vec3 camPos = { 0.0f, 0.0f, -2.0f };
		glm::mat4 view = glm::translate(glm::mat4(1.0f), camPos);
		glm::mat4 projection = glm::perspective(glm::radians(70.0f), 1700.0f / 900.0f, 0.1f, 200.0f);
		projection[1][1] *= -1;
		glm::mat4 model = glm::rotate(glm::mat4{ 1.0f }, glm::radians(frameNumber * 0.4f), glm::vec3(0, 1, 0));
		glm::mat4 meshMatrix = projection * view * model;

		GPUCameraData camData = {};
		camData.proj = projection;
		camData.view = view;
		camData.viewProj = projection * view;

		void* data;
		vmaMapMemory(allocator, currentFrame.cameraBuffer.allocation, &data);
		memcpy(data, &camData, sizeof(GPUCameraData));
		vmaUnmapMemory(allocator, currentFrame.cameraBuffer.allocation);

		MeshPushConstants constants;
		constants.renderMatrix = meshMatrix;

		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipelineLayout, 0, 1, &currentFrame.globalDescriptor, 0, nullptr);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipelineLayout, 1, 1, &testTextureSet, 0, nullptr);

		VkViewport viewport = DefaultViewport();
		VkRect2D scissor = DefaultScissor();
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &triangleMesh.vertexBuffer.buffer, &offset);

		vkCmdPushConstants(cmd, trianglePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

		vkCmdDraw(cmd, triangleMesh.vertices.size(), 1, 0, 0);
	}
	vkCmdEndRenderPass(cmd);
	CheckVkError(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

	submit.pWaitDstStageMask = &waitStage;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &currentFrame.presentSemaphore;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &currentFrame.renderSemaphore;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	err = vkQueueSubmit(graphicsQueue, 1, &submit, currentFrame.renderFence);
	CheckVkError(err);

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.swapchainCount = 1;
	presentInfo.pWaitSemaphores = &currentFrame.renderSemaphore;
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

	VkPushConstantRange pushConstant = {};
	pushConstant.offset = 0;
	pushConstant.size = sizeof(MeshPushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
	pipelineLayoutInfo.pushConstantRangeCount = 1;

	VkDescriptorSetLayout setLayouts[] = { globalSetLayout, singleTextureSetLayout };

	pipelineLayoutInfo.setLayoutCount = 2;
	pipelineLayoutInfo.pSetLayouts = &setLayouts[0];

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

	VertexInputDescription vertexDescription = Vertex::GetVertexDescription();
	pipelineBuilder.vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder.vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();
	pipelineBuilder.vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder.vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();

	pipelineBuilder.depthStencil = DepthStencilCreateInfo(true, true, VK_COMPARE_OP_LESS_OR_EQUAL);

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

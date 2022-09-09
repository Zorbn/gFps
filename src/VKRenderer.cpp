#include "VKRenderer.h"

#include <stdexcept>
#include <cmath>
#include <sstream>

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
	InitCommands();
	InitDefaultRenderpass();
	InitFramebuffers();
	InitSyncStructures();
}

void VKRenderer::InitVulkan(const std::string& windowName)
{
	vkb::InstanceBuilder builder;
	vkb::Instance vkbInstance = builder.set_app_name(windowName.c_str())
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
	vkb::Device vkbDevice = deviceBuilder.build().value();
	device = vkbDevice.device;
	chosenGPU = physicalDevice.physical_device;

	graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

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
}

void VKRenderer::InitCommands()
{
	VkCommandPoolCreateInfo commandPoolInfo = CommandPoolCreateInfo(graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	VkResult err = vkCreateCommandPool(device, &commandPoolInfo, nullptr, &commandPool);
	CheckVkError(err);

	VkCommandBufferAllocateInfo commandAllocInfo = CommandBufferAllocateInfo(commandPool, 1);
	err = vkAllocateCommandBuffers(device, &commandAllocInfo, &mainCommandBuffer);
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
	}
}

void VKRenderer::InitSyncStructures()
{
	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.pNext = nullptr;

	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	VkResult err = vkCreateFence(device, &fenceCreateInfo, nullptr, &renderFence);
	CheckVkError(err);

	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = nullptr;
	semaphoreCreateInfo.flags = 0;

	err = vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &presentSemaphore);
	CheckVkError(err);
	err = vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &renderSemaphore);
	CheckVkError(err);
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

void VKRenderer::CloseWindow()
{
	vkDestroyCommandPool(device, commandPool, nullptr);

	vkDestroySwapchainKHR(device, swapchain, nullptr);
	vkDestroyRenderPass(device, renderPass, nullptr);

	for (int i = 0; i < framebuffers.size(); ++i)
	{
		vkDestroyFramebuffer(device, framebuffers[i], nullptr);
		vkDestroyImageView(device, swapchainImageViews[i], nullptr);
	}

	vkDestroyDevice(device, nullptr);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkDestroyInstance(instance, nullptr);

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
	err = vkResetFences(device, 1, &renderFence);
	CheckVkError(err);

	uint32_t swapchainImageIndex;
	err = vkAcquireNextImageKHR(device, swapchain, 1'000'000'000, presentSemaphore, nullptr, &swapchainImageIndex);
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
	CheckVkError(err);
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

void VKRenderer::CheckVkError(VkResult err)
{
	if (err)
	{
		std::stringstream errStream;
		errStream << "Detected Vulkan error: " << err << "\n";
		throw std::runtime_error(errStream.str());
	}
}

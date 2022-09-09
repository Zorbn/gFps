#pragma once

#define GLFW_INCLUDE_VULKAN
#include "Renderer.h"

#include <VkBootstrap.h>

// TODO: https://vkguide.dev/docs/chapter-1/vulkan_mainloop_code/
class VKRenderer : public Renderer
{
public:
	VKRenderer(const std::string& windowName, int32_t windowWidth, int32_t windowHeight);

	void CloseWindow() override;
	void ResizeWindow(int32_t width, int32_t height) override;
	GLFWwindow* GetWindowPtr() override;

	void SetClearColor(float r, float g, float b, float a) override;
	void BeginDrawing() override;
	void EndDrawing() override;

	void DrawModel(const Model* model, const TextureArray* textureArray, const Instances* instances) override;
	void DrawSprite(const Model* model, const TextureArray* textureArray, const Instances* instances) override;

	Model CreateModel(const std::vector<float>& vertices, const std::vector<uint32_t>& indices) override;
	void UpdateModel(Model* model, const std::vector<float>& vertices, const std::vector<uint32_t>& indices) override;
	void DestroyModel(Model* model) override;

	TextureArray CreateTextureArray(const std::vector<std::string>& images) override;
	void DestroyTextureArray(TextureArray* textureArray) override;

	void UpdateCamera() override;
	void SetCameraPosition(glm::vec3 position) override;
	void SetCameraRotation(float yRot, float xRot) override;
	void ConfigureCamera(float fov) override;

private:
	void InitVulkan(const std::string& windowName);
	void InitCommands();
	void InitDefaultRenderpass();
	void InitFramebuffers();
	void InitSyncStructures();

	VkCommandPoolCreateInfo CommandPoolCreateInfo(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags);
	VkCommandBufferAllocateInfo CommandBufferAllocateInfo(VkCommandPool pool, uint32_t count = 1, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	void CheckVkError(VkResult err);

	GLFWwindow* window;
	int32_t width;
	int32_t height;
	Camera camera;

	VkInstance instance;
	VkDebugUtilsMessengerEXT debugMessenger;
	VkPhysicalDevice chosenGPU;
	VkDevice device;
	VkSurfaceKHR surface;
	VkSwapchainKHR swapchain;
	VkFormat swapchainImageFormat;
	std::vector<VkImage> swapchainImages;
	std::vector<VkImageView> swapchainImageViews;
	VkQueue graphicsQueue;
	uint32_t graphicsQueueFamily;
	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;
	VkRenderPass renderPass;
	std::vector<VkFramebuffer> framebuffers;
	VkSemaphore presentSemaphore;
	VkSemaphore renderSemaphore;
	VkFence renderFence;
};

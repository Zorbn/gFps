#pragma once

#define GLFW_INCLUDE_VULKAN
#include "Renderer.h"

#include <functional>
#include <unordered_map>
#include <VkBootstrap.h>

#include "../deps/vk_mem_alloc.h"

constexpr uint32_t frameOverlap = 2;

// TODO:
// https://vkguide.dev/docs/chapter_5 (check comments, VMA_MEMORY_USAGE depric)
struct AllocatedBuffer
{
	VkBuffer buffer;
	VmaAllocation allocation;
};

struct AllocatedImage
{
	VkImage image;
	VmaAllocation allocation;
};

struct VertexInputDescription
{
	std::vector<VkVertexInputBindingDescription> bindings;
	std::vector<VkVertexInputAttributeDescription> attributes;

	VkPipelineVertexInputStateCreateFlags flags = 0;
};

struct Vertex
{
	static VertexInputDescription GetVertexDescription();

	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec3 color;
	glm::vec2 uv;
};

struct Mesh
{
	std::vector<Vertex> vertices;
	AllocatedBuffer vertexBuffer = {};
};

struct MeshPushConstants
{
	glm::vec4 data;
	glm::mat4 renderMatrix;
};

struct Texture
{
	AllocatedImage image;
	VkImageView imageView;
};

struct GPUCameraData
{
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewProj;
};

struct FrameData
{
	VkSemaphore presentSemaphore;
	VkSemaphore renderSemaphore;
	VkFence renderFence;

	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;

	AllocatedBuffer cameraBuffer;
	VkDescriptorSet globalDescriptor;
};

struct UploadContext
{
	VkFence uploadFence;
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
};

class PipelineBuilder
{
public:
	VkPipeline BuildPipeline(VkDevice device, VkRenderPass pass);

	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	VkViewport viewport = {};
	VkRect2D scissor = {};
	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	VkPipelineMultisampleStateCreateInfo multisampling = {};
	VkPipelineLayout pipelineLayout = {};
	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
};

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
	void InitSwapchain();
	void InitCommands();
	void InitDefaultRenderpass();
	void InitFramebuffers();
	void InitSyncStructures();
	void InitDescriptors();
	void InitPipelines();
	void LoadImages();

	void LoadMeshes();
	void UploadMesh(Mesh& mesh);

	void FlushDeletionList(std::vector<std::function<void()>>& list);
	void RecreateSwapchain();
	void CleanupVulkan();
	bool LoadShaderModule(const char* filePath, VkShaderModule* outShaderModule);
	FrameData& GetCurrentFrame();
	void CheckVkError(VkResult err);
	AllocatedBuffer CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);
	void LoadImageFromFile(const char* file, AllocatedImage& outImage);

	VkCommandPoolCreateInfo CommandPoolCreateInfo(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags);
	VkCommandBufferAllocateInfo CommandBufferAllocateInfo(VkCommandPool pool, uint32_t count = 1, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	VkPipelineShaderStageCreateInfo PipelineShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule shaderModule);
	VkPipelineVertexInputStateCreateInfo VertexInputStateCreateInfo();
	VkPipelineInputAssemblyStateCreateInfo InputAssemblyCreateInfo(VkPrimitiveTopology topology);
	VkPipelineRasterizationStateCreateInfo RasterizationStateCreateInfo(VkPolygonMode polygonMode);
	VkPipelineMultisampleStateCreateInfo MulitsamplingStateCreateInfo();
	VkPipelineColorBlendAttachmentState ColorBlendAttachmentState();
	VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo();
	VkFenceCreateInfo FenceCreateInfo(VkFenceCreateFlags flags);
	VkSemaphoreCreateInfo SemaphoreCreateInfo(VkSemaphoreCreateFlags flags);
	VkViewport DefaultViewport();
	VkRect2D DefaultScissor();
	VkImageCreateInfo ImageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);
	VkImageViewCreateInfo ImageViewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);
	VkPipelineDepthStencilStateCreateInfo DepthStencilCreateInfo(bool depthTest, bool depthWrite, VkCompareOp compareOp);
	VkCommandBufferBeginInfo CommandBufferBeginInfo(VkCommandBufferUsageFlags flags);
	VkSubmitInfo SubmitInfo(VkCommandBuffer* cmd);
	VkSamplerCreateInfo SamplerCreateInfo(VkFilter filters, VkSamplerAddressMode samplerAddressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);
	VkWriteDescriptorSet WriteDescriptorImage(VkDescriptorType type, VkDescriptorSet dstSet, VkDescriptorImageInfo* imageInfo, uint32_t binding);

	GLFWwindow* window;
	int32_t width;
	int32_t height;
	Camera camera;

	std::vector<std::function<void()>> deletionList;
	std::vector<std::function<void()>> swapchainDeletionList;

	vkb::Instance vkbInstance;
	vkb::Device vkbDevice;

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

	VkRenderPass renderPass;
	std::vector<VkFramebuffer> framebuffers;

	FrameData frames[frameOverlap];
	int32_t frameNumber;

	VkPipelineLayout trianglePipelineLayout;
	VkPipeline trianglePipeline;

	VmaAllocator allocator;

	//VkPipeline meshPipeline;
	Mesh triangleMesh;

	VkImageView depthImageView;
	AllocatedImage depthImage;
	VkFormat depthFormat;

	VkDescriptorSetLayout globalSetLayout;
	VkDescriptorPool descriptorPool;

	UploadContext uploadContext;

	std::unordered_map<std::string, Texture> loadedTextures;

	VkDescriptorSetLayout singleTextureSetLayout;
	VkDescriptorSet testTextureSet = { VK_NULL_HANDLE };

	VkPhysicalDeviceProperties gpuProperties;
};

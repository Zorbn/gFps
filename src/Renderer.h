#pragma once

#include <string>
#include <cinttypes>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

// TODO: The structs are specific to GL, move them there are replace the user facing
// part with hashes into a map of resources.
struct Camera
{
	float fov;
	float zNear;
	float zFar;
	glm::vec3 pos;
	glm::vec3 dir;
	glm::vec3 up;
	int32_t projLoc;
	int32_t orthoProjLoc;
	int32_t viewLoc;
	int32_t is2DLoc;
};

struct Model
{
	uint32_t vao;
	uint32_t vbo;
	uint32_t ebo;
	uint32_t instanceOffsetVbo;
	uint32_t instanceRotationVbo;
	uint32_t instanceScaleVbo;
	uint32_t instanceTextureIndexVbo;
	size_t indexCount;
};

struct TextureArray
{
	uint32_t texture;
};

struct Instances
{
	std::vector<glm::vec3> offsets;
	std::vector<float> rotations;
	std::vector<float> scales;
	std::vector<uint32_t> textureIndices;
};

class Renderer
{
public:
	virtual void CloseWindow() = 0;
	virtual void ResizeWindow(int32_t width, int32_t height) = 0;
	virtual GLFWwindow* GetWindowPtr() = 0;

	virtual void SetClearColor(float r, float g, float b, float a) = 0;
	virtual void BeginDrawing() = 0;
	virtual void EndDrawing() = 0;

	virtual void DrawModel(const Model* model, const TextureArray* textureArray, const Instances* instances) = 0;
	virtual void DrawSprite(const Model* model, const TextureArray* textureArray, const Instances* instances) = 0;

	virtual Model CreateModel(const std::vector<float>& vertices, const std::vector<uint32_t>& indices) = 0;
	virtual void UpdateModel(Model* model, const std::vector<float>& vertices, const std::vector<uint32_t>& indices) = 0;
	virtual void DestroyModel(Model* model) = 0;

	virtual TextureArray CreateTextureArray(const std::vector<std::string>& images) = 0;
	virtual void DestroyTextureArray(TextureArray* textureArray) = 0;

	virtual void UpdateCamera() = 0;
	virtual void SetCameraPosition(glm::vec3 position) = 0;
	virtual void SetCameraRotation(float yRot, float xRot) = 0;
	virtual void ConfigureCamera(float fov) = 0;
};

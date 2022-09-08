#pragma once

#include <string>
#include <cinttypes>
#include <vector>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

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

struct GraphicsCtx
{
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

class GLRenderer
{
public:
	GLRenderer(const std::string &windowName, int32_t windowWidth, int32_t windowHeight);

	void CloseWindow();
	void ResizeWindow(int32_t width, int32_t height);
	GLFWwindow *GetWindowPtr();

	void SetClearColor(float r, float g, float b, float a);
	void BeginDrawing();
	void EndDrawing();

	void DrawModel(const Model *model, const TextureArray *textureArray, const Instances *instances);
	void DrawSprite(const Model *model, const TextureArray *textureArray, const Instances *instances);

	Model CreateModel(const std::vector<float> &vertices, const std::vector<uint32_t> &indices);
	void UpdateModel(Model *model, const std::vector<float>& vertices, const std::vector<uint32_t>& indices);
	void DestroyModel(Model *model);

	TextureArray CreateTextureArray(const std::vector<std::string>& images);
	void DestroyTextureArray(TextureArray *textureArray);

	void UpdateCamera();
	void SetCameraPosition(glm::vec3 position);
	void SetCameraRotation(float yRot, float xRot);
	void ConfigureCamera(float fov);

private:
	void GLRenderer::CheckShaderLinkError(uint32_t program);
	void GLRenderer::CheckShaderCompileError(uint32_t shader);

	GLFWwindow *window;
	int32_t width;
	int32_t height;
	uint32_t vertexShader;
	uint32_t fragmentShader;
	uint32_t shaderProgram;
	Camera camera;
};

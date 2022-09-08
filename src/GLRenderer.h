#pragma once

#include "Renderer.h"

class GLRenderer : public Renderer
{
public:
	GLRenderer(const std::string &windowName, int32_t windowWidth, int32_t windowHeight);

	void CloseWindow() override;
	void ResizeWindow(int32_t width, int32_t height) override;
	GLFWwindow *GetWindowPtr() override;

	void SetClearColor(float r, float g, float b, float a) override;
	void BeginDrawing() override;
	void EndDrawing() override;

	void DrawModel(const Model *model, const TextureArray *textureArray, const Instances *instances) override;
	void DrawSprite(const Model *model, const TextureArray *textureArray, const Instances *instances) override;

	Model CreateModel(const std::vector<float> &vertices, const std::vector<uint32_t> &indices) override;
	void UpdateModel(Model *model, const std::vector<float>& vertices, const std::vector<uint32_t>& indices) override;
	void DestroyModel(Model *model) override;

	TextureArray CreateTextureArray(const std::vector<std::string>& images) override;
	void DestroyTextureArray(TextureArray *textureArray) override;

	void UpdateCamera() override;
	void SetCameraPosition(glm::vec3 position) override;
	void SetCameraRotation(float yRot, float xRot) override;
	void ConfigureCamera(float fov) override;

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

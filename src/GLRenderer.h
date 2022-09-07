#pragma once

#include <string>
#include <cinttypes>
#include <vector>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

struct GraphicsCtx
{
	GLFWwindow* window;
	uint32_t vertexShader;
	uint32_t fragmentShader;
	uint32_t shaderProgram;
};

struct Model
{
	uint32_t vao;
	uint32_t vbo;
	uint32_t ebo;
	uint32_t instanceOffsetVbo;
    uint32_t instanceRotationVbo;
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
	std::vector<uint32_t> textureIndices;
};

// TODO: Const correctness.
GraphicsCtx OpenWindow(std::string name, int32_t width, int32_t height);
void CloseWindow(GraphicsCtx *ctx);
void SetClearColor(GraphicsCtx *ctx, float r, float g, float b, float a);
void BeginDrawing(GraphicsCtx* ctx);
void EndDrawing(GraphicsCtx* ctx);
void DrawModel(GraphicsCtx* ctx, Model* model, TextureArray* textureArray, Instances *instances);
Model CreateModel(GraphicsCtx* ctx, const std::vector<float> &vertices, const std::vector<uint32_t> &indices);
void DestroyModel(GraphicsCtx* ctx, Model* model);
TextureArray CreateTextureArray(std::vector<std::string> images);
void DestroyTextureArray(TextureArray *textureArray);
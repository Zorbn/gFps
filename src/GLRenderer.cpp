#include "GLRenderer.h"

#include <stdexcept>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "../deps/stb_image.h"

const char* vertexShaderSource =
"#version 330 core\n"

"layout (location = 0) in vec3 aPos;\n"
"layout (location = 1) in vec2 aTexCoord;\n"
"layout (location = 2) in vec3 aOffset;\n"
"layout (location = 3) in float aRotation;\n"
"layout (location = 4) in float aScale;\n"
"layout (location = 5) in uint aTextureIndex;\n"

"out vec2 TexCoord;\n"
"flat out uint TextureIndex;\n"

"uniform mat4 Proj;\n"
"uniform mat4 OrthoProj;\n"
"uniform mat4 View;\n"
"uniform bool Is2D;\n"

"void main()\n"
"{\n"
"   float theta = radians(aRotation);\n"
"   mat4 yRotation = mat4(\n"
"       cos(theta),  0, sin(theta), 0,\n"
"       0,           1, 0,          0,\n"
"       -sin(theta), 0, cos(theta), 0,\n"
"       0,           0, 0,          1);\n"
"   mat4 zRotation = mat4(\n"
"       cos(theta), -sin(theta), 0, 0,\n"
"       sin(theta), cos(theta),  0, 0,\n"
"       0,          0,           0, 0,\n"
"       0,          0,           0, 1);\n"

"   vec4 pos = vec4(aPos * aScale, 1.0);\n"

"   if (Is2D)\n"
"   {\n"
"       pos = OrthoProj * zRotation * pos + vec4(aOffset, 0.0);\n"
"   }\n"
"   else\n"
"   {\n"
"       pos = Proj * View * (yRotation * pos + vec4(aOffset, 0.0));\n"
"   }\n"

"   gl_Position = pos;\n"
"   TexCoord = aTexCoord;\n"
"   TextureIndex = aTextureIndex;\n"
"}\0";

const char* fragmentShaderSource =
"#version 330 core\n"

"out vec4 FragColor;\n"

"in vec2 TexCoord;\n"
"flat in uint TextureIndex;\n"

"uniform sampler2DArray textureArray;\n"

"void main()\n"
"{\n"
"   vec4 texColor = texture(textureArray, vec3(TexCoord, float(TextureIndex)));\n"

"   // Treat #660066 as transparency.\n"
"   if (texColor.r == 0.4 && texColor.g == 0.0 && texColor.b == 0.4)\n"
"   {\n"
"       discard;\n"
"   }\n"

"   FragColor = texColor;\n"
"}\0";

const int32_t maxShaderErrorLen = 512;

GLRenderer::GLRenderer(const std::string& windowName, int32_t windowWidth, int32_t windowHeight)
	: width(windowWidth), height(windowHeight)
{

	if (!glfwInit())
	{
		throw std::runtime_error("Failed to initialize glfw!");
	}

	window = glfwCreateWindow(width, height, windowName.c_str(), nullptr, nullptr);

	if (!window)
	{
		glfwTerminate();
		throw std::runtime_error("Failed to create a window!");
	}

	glfwMakeContextCurrent(window);
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

	vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
	glCompileShader(vertexShader);
	CheckShaderCompileError(vertexShader);

	fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
	glCompileShader(fragmentShader);
	CheckShaderCompileError(fragmentShader);

	shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);
	CheckShaderLinkError(shaderProgram);

	glUseProgram(shaderProgram);
	int32_t projLoc = glGetUniformLocation(shaderProgram, "Proj");
	int32_t orthoProjLoc = glGetUniformLocation(shaderProgram, "OrthoProj");
	int32_t viewLoc = glGetUniformLocation(shaderProgram, "View");
	int32_t is2DLoc = glGetUniformLocation(shaderProgram, "Is2D");
	glUniform1i(is2DLoc, false);

	glViewport(0, 0, width, height);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	stbi_set_flip_vertically_on_load(true);

	camera = Camera{
		45.0f,
		0.1f,
		100.0f,
		glm::vec3(0.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 0.0f, -1.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		projLoc,
		orthoProjLoc,
		viewLoc,
		is2DLoc,
	};
}

void GLRenderer::CloseWindow()
{
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	glDeleteProgram(shaderProgram);

	glfwTerminate();
}

void GLRenderer::ResizeWindow(int32_t width, int32_t height)
{
	this->width = width;
	this->height = height;
	glViewport(0, 0, width, height);
}

GLFWwindow* GLRenderer::GetWindowPtr()
{
	return window;
}

void GLRenderer::SetClearColor(float r, float g, float b, float a)
{
	glClearColor(r, g, b, a);
}

void GLRenderer::BeginDrawing()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GLRenderer::EndDrawing()
{
	glfwSwapBuffers(window);
	glfwPollEvents();
}

void GLRenderer::DrawModel(const Model* model, const TextureArray* textureArray, const Instances* instances)
{
	glBindTexture(GL_TEXTURE_2D, textureArray->texture);
	glBindVertexArray(model->vao);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model->ebo);

	glBindBuffer(GL_ARRAY_BUFFER, model->instanceOffsetVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * instances->offsets.size(),
		&instances->offsets[0], GL_DYNAMIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, model->instanceTextureIndexVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * instances->textureIndices.size(),
		&instances->textureIndices[0], GL_DYNAMIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, model->instanceRotationVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * instances->rotations.size(),
		&instances->rotations[0], GL_DYNAMIC_DRAW);

	glBindBuffer(GL_ARRAY_BUFFER, model->instanceScaleVbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(float) * instances->scales.size(),
		&instances->scales[0], GL_DYNAMIC_DRAW);

	glDrawElementsInstanced(GL_TRIANGLES, model->indexCount, GL_UNSIGNED_INT, 0,
		instances->offsets.size());
}

void GLRenderer::DrawSprite(const Model* model, const TextureArray* textureArray, const Instances* instances)
{
	glUniform1i(camera.is2DLoc, true);
	glDisable(GL_DEPTH_TEST);
	DrawModel(model, textureArray, instances);
	glEnable(GL_DEPTH_TEST);
	glUniform1i(camera.is2DLoc, false);
}

Model GLRenderer::CreateModel(const std::vector<float>& vertices,
	const std::vector<uint32_t>& indices)
{
	uint32_t vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	uint32_t vbo;
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), &vertices[0], GL_STATIC_DRAW);

	uint32_t ebo;
	glGenBuffers(1, &ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), &indices[0],
		GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

	uint32_t instanceOffsetVbo;
	glGenBuffers(1, &instanceOffsetVbo);
	glEnableVertexAttribArray(2);
	glBindBuffer(GL_ARRAY_BUFFER, instanceOffsetVbo);
	glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	glVertexAttribDivisor(2, 1);

	uint32_t instanceRotationVbo;
	glGenBuffers(1, &instanceRotationVbo);
	glEnableVertexAttribArray(3);
	glBindBuffer(GL_ARRAY_BUFFER, instanceRotationVbo);
	glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
	glVertexAttribDivisor(3, 1);

	uint32_t instanceScaleVbo;
	glGenBuffers(1, &instanceScaleVbo);
	glEnableVertexAttribArray(4);
	glBindBuffer(GL_ARRAY_BUFFER, instanceScaleVbo);
	glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(float), (void*)0);
	glVertexAttribDivisor(4, 1);

	uint32_t instanceTextureIndexVbo;
	glGenBuffers(1, &instanceTextureIndexVbo);
	glEnableVertexAttribArray(5);
	glBindBuffer(GL_ARRAY_BUFFER, instanceTextureIndexVbo);
	glVertexAttribPointer(5, 1, GL_UNSIGNED_INT, GL_FALSE, sizeof(uint32_t), (void*)0);
	glVertexAttribDivisor(5, 1);

	return Model{
		vao, vbo, ebo,
		instanceOffsetVbo,
		instanceRotationVbo,
		instanceScaleVbo,
		instanceTextureIndexVbo,
		indices.size(),
	};
}

void GLRenderer::UpdateModel(Model* model, const std::vector<float>& vertices, const std::vector<uint32_t>& indices)
{
	glGenBuffers(1, &model->vbo);
	glBindBuffer(GL_ARRAY_BUFFER, model->vbo);
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), &vertices[0], GL_STATIC_DRAW);

	glGenBuffers(1, &model->ebo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model->ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), &indices[0],
		GL_STATIC_DRAW);
}

void GLRenderer::DestroyModel(Model* model)
{
	glDeleteBuffers(1, &model->vao);
	glDeleteBuffers(1, &model->vbo);
	glDeleteBuffers(1, &model->ebo);
	glDeleteBuffers(1, &model->instanceOffsetVbo);
	glDeleteBuffers(1, &model->instanceRotationVbo);
	glDeleteBuffers(1, &model->instanceTextureIndexVbo);
}

TextureArray GLRenderer::CreateTextureArray(const std::vector<std::string>& images)
{
	size_t imageCount = images.size();

	if (imageCount < 1)
	{
		throw std::runtime_error("No images supplied when creating texture array!");
	}

	int32_t width;
	int32_t height;
	uint8_t* data;

	uint32_t texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D_ARRAY, texture);

	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	for (int32_t i = 0; i < images.size(); ++i)
	{
		int32_t iWidth;
		int32_t iHeight;
		int32_t iChannelCount;
		data = stbi_load(images[i].c_str(), &iWidth, &iHeight, &iChannelCount, 0);

		if (!data)
		{
			throw std::runtime_error(std::string("Failed to load: ") + images[0]);
		}

		if (iChannelCount != 3)
		{
			throw std::runtime_error("Failed to load non RGB image, wrong channel count!");
		}

		if (i == 0)
		{
			width = iWidth;
			height = iHeight;

			glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB, width, height, images.size(), 0,
				GL_RGB, GL_UNSIGNED_BYTE, nullptr);
		}
		else if (iWidth != width || iHeight != height)
		{
			throw std::runtime_error("Can't create array of different sized textures!");
		}

		glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, width, height, 1, GL_RGB,
			GL_UNSIGNED_BYTE, data);

		stbi_image_free(data);
	}

	glGenerateMipmap(GL_TEXTURE_2D_ARRAY);

	return TextureArray{ texture };
}

void GLRenderer::DestroyTextureArray(TextureArray* textureArray)
{
	glDeleteTextures(1, &textureArray->texture);
}

void GLRenderer::UpdateCamera()
{
	glm::mat4 view = glm::lookAt(camera.pos, camera.pos + camera.dir, camera.up);
	glm::mat4 proj = glm::perspective(glm::radians(camera.fov),
		static_cast<float>(width) / static_cast<float>(height),
		camera.zNear, camera.zFar);
	glm::mat4 orthoProj = glm::ortho<float>(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);
	glUniformMatrix4fv(camera.orthoProjLoc, 1, GL_FALSE, glm::value_ptr(orthoProj));
	glUniformMatrix4fv(camera.projLoc, 1, GL_FALSE, glm::value_ptr(proj));
	glUniformMatrix4fv(camera.viewLoc, 1, GL_FALSE, glm::value_ptr(view));
}

void GLRenderer::SetCameraPosition(glm::vec3 position)
{
	camera.pos = position;
}

void GLRenderer::SetCameraRotation(float yRot, float xRot)
{
	float xTheta = glm::radians(xRot);
	float yTheta = glm::radians(yRot + 270.0f);
	camera.dir.x = cos(yTheta) * cos(xTheta);
	camera.dir.y = sin(xTheta);
	camera.dir.z = sin(yTheta) * cos(xTheta);
}

void GLRenderer::ConfigureCamera(float fov)
{
	camera.fov = fov;
}

void GLRenderer::CheckShaderCompileError(uint32_t shader)
{
	int32_t success;

	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

	if (!success)
	{
		char infoLog[maxShaderErrorLen];
		glGetShaderInfoLog(shader, maxShaderErrorLen, nullptr, infoLog);
		throw std::runtime_error(infoLog);
	}
}

void GLRenderer::CheckShaderLinkError(uint32_t program)
{
	int32_t success;

	glGetProgramiv(program, GL_LINK_STATUS, &success);

	if (!success)
	{
		char infoLog[maxShaderErrorLen];
		glGetProgramInfoLog(program, maxShaderErrorLen, nullptr, infoLog);
		throw std::runtime_error(infoLog);
	}
}

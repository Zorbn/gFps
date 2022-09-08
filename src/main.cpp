#include <stdio.h>
#include <cinttypes>
#include <vector>
#include <iostream>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "GLRenderer.h"

/*
 * To implement:
 * >> OpenWindow(...);
 * >> CloseWindow(...);
 * >> ResizeWindow(int32_t width, int32_t height);
 * >> CreateTextureArray(images[]); // Returns a handle/hash to a texture array.
 * >> BeginDrawing();
 * >> EndDrawing();
 * >> DrawModel(textureArray, instanceData[]); // Instance data specifies instance positions, rotations, and texture indices.
 * >> DrawSprite(...); // Same as draw model except 2D (for UI).
 * >> CreateModel(vertices, indices); // Returns model
 * >> UpdateModel()
 * >> DestroyModel(model);
 * >> SetClearColor(color);
 * >> ConfigureCamera(fov);
 * >> UpdateCamera();
 * >> SetCameraPosition(pos);
 * >> SetCameraRotation(rot);
 */

void ResizeCallback(GLFWwindow* window, int32_t width, int32_t height);

int main(void)
{
	GLRenderer rend("gFps", 640, 480);
	GLFWwindow* window = rend.GetWindowPtr();
	glfwSetWindowUserPointer(window, &rend);
	glfwSetFramebufferSizeCallback(window, ResizeCallback);

	rend.SetClearColor(0, 0, 0.2f, 1);

	std::vector<float> vertices = {
		 0.5f,  0.5f, 0.0f, 1.0f, 1.0f, // top right
		 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, // bottom right
		-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, // bottom left
		-0.5f,  0.5f, 0.0f, 0.0f, 1.0f  // top left 
	};

	std::vector<uint32_t> indices = {
		0, 3, 1,
		1, 3, 2
	};

	Model model = rend.CreateModel(vertices, indices);
	std::vector<std::string> images = { "res/test.png", "res/test2.png" };
	TextureArray textureArray = rend.CreateTextureArray(images);
	Instances instances = {
		{ glm::vec3{ -0.5f, 0.0f, 0.0f }, glm::vec3{ 0.1f, 0.1f, -5.1f } },
		{ 0, 45.0f },
		{ 1, 1 },
		{ 1, 0 },
	};

	Instances spriteInstances = {
		{ glm::vec3{ 0.5f, 0.5f, 0.0f }, glm::vec3{ 0.0f, -0.5f, 0.0f } },
		{ 0.0f, 0.0f },
		{ 0.1f, 0.1f },
		{ 1, 0 },
	};

	rend.SetCameraPosition(glm::vec3(0.0f, 0.5f, 5.0f));
	rend.SetCameraRotation(0.0f, 0.0f);

	// double time = glfwGetTime();
	// glfwSwapInterval(0);

	while (!glfwWindowShouldClose(window))
	{
		// double newTime = glfwGetTime();
		// std::cout << 1000.0 * (newTime - time) << "\n";
		// time = newTime;

		rend.UpdateCamera();
		rend.BeginDrawing();
		rend.DrawModel(&model, &textureArray, &instances);
		rend.DrawSprite(&model, &textureArray, &spriteInstances);
		rend.EndDrawing();
	}

	rend.DestroyModel(&model);
	rend.DestroyTextureArray(&textureArray);
	rend.CloseWindow();

	return 0;
}

void ResizeCallback(GLFWwindow* window, int32_t width, int32_t height)
{
	static_cast<Renderer*>(glfwGetWindowUserPointer(window))->ResizeWindow(width, height);
}

#include <stdio.h>
#include <cinttypes>
#include <vector>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "GLRenderer.h"

/*
 * To implement:
 * >> OpenWindow(...);
 * >> CloseWindow(...);
 * >> CreateTextureArray(images[]); // Returns a handle/hash to a texture array.
 * >> BeginDrawing();
 * >> EndDrawing();
 * >> DrawModel(textureArray, instanceData[]); // Instance data specifies instance positions, rotations, and texture indices.
 * DrawSprite(...); // Same as draw model except 2D (for UI).
 * >> CreateModel(vertices, indices); // Returns model
 * >> DestroyModel(model);
 * InitCamera(fov);
 * >> SetClearColor(color);
 * SetCameraPosition(pos);
 * SetCameraRotation(rot);
 */

int main(void)
{
    GraphicsCtx ctx = OpenWindow("gFps", 640, 480);
    SetClearColor(&ctx, 0, 0, 0.2f, 1);

	std::vector<float> vertices = {
		0.5f,  0.5f, 0.0f, 1.0f, 1.0f,  // top right
		0.5f, -0.5f, 0.0f, 1.0f, 0.0f,  // bottom right
		-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, // bottom left
		-0.5f, 0.5f, 0.0f, 0.0f, 1.0f   // top left 
	}; 

    std::vector<uint32_t> indices = {
        0, 3, 1,
        1, 3, 2
    };

    Model model = CreateModel(&ctx, vertices, indices);
    TextureArray textureArray = CreateTextureArray({ "res/test.png", "res/test2.png" });
    Instances instances = {
        { glm::vec3{ -0.5f, 0.0f, 0.0f }, glm::vec3{ 0.1f, 0.1f, -0.1f } },
        { 0, 45.0f },
        { 1, 0 },
    };

    while (!glfwWindowShouldClose(ctx.window))
    {
        BeginDrawing(&ctx);
        DrawModel(&ctx, &model, &textureArray, &instances);
        EndDrawing(&ctx);
    }

    DestroyModel(&ctx, &model);
    DestroyTextureArray(&textureArray);
    CloseWindow(&ctx);

    return 0;
}

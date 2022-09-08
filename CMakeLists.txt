include(FetchContent)
cmake_minimum_required(VERSION 3.17)

project(gFps)

set(CMAKE_CXX_STANDARD 17)
FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.8
)
FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 0.9.9.8
)

FetchContent_MakeAvailable(glfw glm)
add_subdirectory(deps/glad)

add_executable(game deps/stb_image.h src/main.cpp src/Renderer.h src/GLRenderer.cpp src/GLRenderer.h)

target_link_libraries(
        game
        glm
        glfw
        glad
)


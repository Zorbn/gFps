#version 450

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vColor;
layout(location = 3) in vec2 vTexCoord;

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec2 texCoord;

layout(set = 0, binding = 0) uniform CameraBuffer {
	mat4 view;
	mat4 proj;
	mat4 viewProj;
} cameraData;

layout(push_constant) uniform constants
{
    vec4 data;
    mat4 renderMatrix;
} pushConstants;

void main()
{
    mat4 transformMatrix = cameraData.viewProj;
	gl_Position = transformMatrix * vec4(vPos, 1.0f);
	outColor = vColor;
	texCoord = vTexCoord;
}

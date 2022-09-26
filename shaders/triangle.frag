#version 450

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 texCoord;

layout(set = 1, binding = 0) uniform sampler2D tex;

layout(location = 0) out vec4 outFragColor;

void main()
{
	vec3 color = texture(tex, texCoord).xyz;
    outFragColor = vec4(color, 1.0f);
}

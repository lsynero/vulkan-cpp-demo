#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 coord;
layout(location = 2) in vec3 inColor;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
} mvp;

void main() {
    gl_Position = mvp.model*vec4(inPosition, 1.0);	
}

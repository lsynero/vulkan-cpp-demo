#version 450
layout(location = 0) in vec3 position;
//layout(location = 1) in vec4 color;
layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
} mvp;

layout (set = 0, binding = 1) buffer PointBuffer
{
    vec3 color[];
} point_buffer;

void main() {
    gl_Position = mvp.model * vec4(position, 1.0);
    fragColor = vec4(point_buffer.color[gl_VertexIndex],1.0);
}

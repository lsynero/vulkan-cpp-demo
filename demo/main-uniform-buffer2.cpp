#include <iostream>
#include <chrono>
#include <glm/mat4x4.hpp>
#include <glm/gtx/transform.hpp>
#include "VK_UniformBuffer.h"
#include "VK_DescriptorSetLayoutBindingGroup.h"
#include "VK_Context.h"

using namespace std;

const std::vector<float> vertices = {
    0.0f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f,
    0.5f, 0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 0.5f,
    -0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.5f
};

const std::vector<uint16_t> indices = {
    0, 1, 2
};

VK_Context* context = nullptr;

uint32_t updateUniformBufferData(char* & data, uint32_t size)
{
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), time * glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    memcpy(data, &model[0][0], size);
    return sizeof(model);
}

uint32_t updateUniformColor(char* & data, uint32_t size)
{
    glm::vec4 color(0.2f, 0.0f, 0.0f, 0.5f);
    memcpy(data, &color, size);
    return sizeof(glm::vec4);
}

void onFrameSizeChanged(int width, int height)
{
    auto vp = VK_Viewports::createViewport(width, height);
    VK_Viewports vps;
    vps.addViewport(vp);
    context->setViewports(vps);
}

int main()
{
    VK_ContextConfig config;
    config.debug = true;
    config.name = "Uniform Demo";

    context = createVkContext(config);
    context->createWindow(640, 480, true);
    context->setOnFrameSizeChanged(onFrameSizeChanged);

    VK_Context::VK_Config vkConfig;
    context->initVulkanDevice(vkConfig);

    auto shaderSet = context->createShaderSet();
    shaderSet->addShader("shader/uniform/vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    shaderSet->addShader("shader/uniform/frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

    shaderSet->appendAttributeDescription(0, sizeof (float) * 3);
    shaderSet->appendAttributeDescription(1, sizeof (float) * 4);

    if(!shaderSet->isValid()) {
        std::cerr << "invalid shaderSet" << std::endl;
        shaderSet->release();
        context->release();
        return -1;
    }

    {
        VK_DescriptorSetLayoutBindingGroup bindingGroup;
        VkDescriptorSetLayoutBinding uniformBinding = VK_DescriptorSetLayoutBindingGroup::createDescriptorSetLayoutBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
        bindingGroup.addDescriptorSetLayoutBinding(uniformBinding);
        uniformBinding = VK_DescriptorSetLayoutBindingGroup::createDescriptorSetLayoutBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM);
        bindingGroup.addDescriptorSetLayoutBinding(uniformBinding);
        context->setDescriptorSetLayoutBindingGroup(bindingGroup);
    }

    {
        auto desciptorPoolSizeGroup = context->getDescriptorPoolSizeGroup();
        desciptorPoolSizeGroup.addDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        desciptorPoolSizeGroup.addDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        context->setDescriptorPoolSizeGroup(desciptorPoolSizeGroup);
    }

    auto buffer = context->createVertexBuffer(vertices, 3 + 4, indices);
    context->addBuffer(buffer);

    auto ubo = context->createUniformBuffer(0, sizeof(GLfloat) * 16);
    ubo->setWriteDataCallback(updateUniformBufferData);
    context->addUniformBuffer(ubo);

    ubo = context->createUniformBuffer(1, sizeof(GLfloat) * 4);
    ubo->setWriteDataCallback(updateUniformColor);
    context->addUniformBuffer(ubo);

    context->initVulkanContext();
    context->initPipeline(shaderSet);
    context->createCommandBuffers();

    context->run();
    context->release();

    return 0;
}

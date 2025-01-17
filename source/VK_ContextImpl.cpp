#include <iostream>
#include <cstring>
#include <sstream>
#include <set>
#include <cmath>
#include "VK_ContextImpl.h"
#include "VK_ShaderSetImpl.h"
#include "VK_Buffer.h"
#include "VK_VertexBuffer.h"
#include "VK_ObjLoader.h"
#include "VK_UniformBufferImpl.h"
#include "VK_InstanceBuffer.h"
#include "VK_IndirectBuffer.h"
#include "VK_ImageImpl.h"
#include "VK_ImageViewImpl.h"
#include "VK_SamplerImpl.h"
#include "VK_DescriptorSets.h"
#include "VK_SecondaryCommandBuffer.h"
#include "VK_QueryPoolImpl.h"
#include "VK_Util.h"
#include <fstream>

VK_Context *createVkContext(const VK_ContextConfig &config)
{
    return new VK_ContextImpl(config);
}

VK_ContextImpl::VK_ContextImpl(const VK_ContextConfig &config):
    appConfig(config)
{
    glfwInit();
    vkAllocator = new VK_Allocator();

    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
}

VK_ContextImpl::~VK_ContextImpl()
{
    glfwTerminate();
}

VkAllocationCallbacks *VK_ContextImpl::getAllocation()
{
    return vkAllocator->getAllocator();
}

void VK_ContextImpl::release()
{
    delete vkAllocator;
    delete this;
}

bool VK_ContextImpl::createWindow(int width, int height, bool resize)
{
    if (window)
        return false;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, resize);

    window = glfwCreateWindow(width, height, appConfig.name.data(), nullptr, nullptr);
    if (!window) {
        std::cerr << "create glfw window failed\n";
        return false;
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    return window != nullptr;
}

void VK_ContextImpl::setOnFrameSizeChanged(std::function<void (int, int)> cb)
{
    if (cb)
        windowSizeChangedCallback = cb;
}

bool VK_ContextImpl::initVulkanDevice(const VK_Config &config)
{
    vkConfig = config;

    vkValidationLayer = new VK_ValidationLayer(this, appConfig.debug);

    return createInstance() &&
           createSurface() &&
           pickPhysicalDevice() &&
           createLogicalDevice() &&
           createCommandPool() &&
           createSwapChain() &&
           createSwapChainImageViews() &&
           createPipelineCache();
}

VkDevice VK_ContextImpl::getDevice() const
{
    return device;
}

VkPhysicalDeviceProperties VK_ContextImpl::getPhysicalDeviceProperties() const
{
    return deviceProperties;
}

VK_PipelineCache *VK_ContextImpl::getPipelineCache() const
{
    return vkPipelineCache;
}

VkPhysicalDevice VK_ContextImpl::getPhysicalDevice() const
{
    return physicalDevice;
}

size_t VK_ContextImpl::getSwapImageCount() const
{
    return swapChainImages.size();
}

VkSampleCountFlagBits VK_ContextImpl::getSampleCountFlagBits() const
{
    return msaaSamples;
}

bool VK_ContextImpl::initVulkanContext()
{
    createRenderPass();

    createColorResources();
    createDepthResources();
    createFramebuffers();

    createSyncObjects();
    return true;
}

VkRenderPass VK_ContextImpl::getRenderPass() const
{
    return renderPass->getRenderPass();
}

VkQueue VK_ContextImpl::getGraphicQueue() const
{
    return graphicsQueue;
}

VK_Pipeline *VK_ContextImpl::createPipeline(VK_ShaderSet *shaderSet)
{
    auto pipeline = new VK_PipelineImpl(this, shaderSet);
    pipeline->prepare();
    pipelines.push_back(pipeline);
    return pipeline;
}

void VK_ContextImpl::addPipeline(VK_PipelineImpl *pipeline)
{
    if (pipeline)
        pipelines.push_back(pipeline);
}

bool VK_ContextImpl::run()
{
    //static double time = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (!drawFrame())
            break;
        //std::cout << "fps:" << 1 / (glfwGetTime() - time) << std::endl;
        //time = glfwGetTime();
    }

    vkDeviceWaitIdle(device);
    cleanup();
    return true;
}

VkExtent2D VK_ContextImpl::getSwapChainExtent() const
{
    return vkSwapChainExtent;
}

VkFormat VK_ContextImpl::getSwapChainFormat() const
{
    return swapChainImageFormat;
}

void VK_ContextImpl::setClearColor(float r, float g, float b, float a)
{
    vkClearValue.color.float32[0] = std::clamp<float>(r, 0.0f, 1.0f);
    vkClearValue.color.float32[1] = std::clamp<float>(g, 0.0f, 1.0f);
    vkClearValue.color.float32[2] = std::clamp<float>(b, 0.0f, 1.0f);
    vkClearValue.color.float32[3] = std::clamp<float>(a, 0.0f, 1.0f);

    needUpdateSwapChain = true;
}

void VK_ContextImpl::setClearDepthStencil(float depth, uint32_t stencil)
{
    vkClearValue.depthStencil.depth = std::clamp<float>(depth, 0.0f, 1.0f);
    vkClearValue.depthStencil.stencil = stencil;

    needUpdateSwapChain = true;
}

void VK_ContextImpl::setLogicalDeviceFeatures(const VkPhysicalDeviceFeatures &features)
{
    logicalFeatures = features;
}

void VK_ContextImpl::captureScreenShot()
{
    captureScreen = true;
}

void VK_ContextImpl::framebufferResizeCallback(GLFWwindow *window, int width, int height)
{
    auto context = reinterpret_cast<VK_ContextImpl *>(glfwGetWindowUserPointer(window));
    if (context->windowSizeChangedCallback)
        context->windowSizeChangedCallback(width, height);
    context->framebufferResized = true;
}

void VK_ContextImpl::mouseButtonCallback(GLFWwindow *window, int button, int state, int mods)
{
    auto context = reinterpret_cast<VK_ContextImpl *>(glfwGetWindowUserPointer(window));
    if (context->appConfig.mouseCallback)
        context->appConfig.mouseCallback(button, state, mods);
}

void VK_ContextImpl::cleanupSwapChain()
{
    vkDepthImageView->release();
    vkDepthImage->release();

    vkColorImageView->release();
    vkColorImage->release();

    for (auto comandBuffer : secondaryCommandBuffers)
        comandBuffer->release();
    secondaryCommandBuffers.clear();
    commandBuffers.clear();

    for (auto framebuffer : swapChainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, getAllocation());
    }

    for (auto pipeline : pipelines)
        pipeline->release();

    renderPass->release();

    std::for_each(swapChainImageViews.begin(), swapChainImageViews.end(), [](VK_ImageView * view) {
        view->release();
    });

    vkDestroySwapchainKHR(device, swapChain, getAllocation());

    for (auto shaderSet : vkShaders) {
        shaderSet->clearUniformBuffer();
    }
}

void VK_ContextImpl::cleanup()
{
    if (queryPool)
        queryPool->release();
    queryPool = nullptr;

    cleanVulkanObjectContainer(vkBuffers);

    while (true) {
        auto itr = vkBuffers.begin();
        if (itr == vkBuffers.end())
            break;
        (*itr)->release();
    }

    vkBuffers.clear();

    vkPipelineCache->saveGraphicsPiplineCache(vkConfig.pipelineCacheFile);
    vkPipelineCache->release();

    cleanupSwapChain();

    for (auto shaderSet : vkShaders) {
        shaderSet->release();
    }

    cleanVulkanObjectContainer(vkSamplerList);
    cleanVulkanObjectContainer(vkImageViewList);
    cleanVulkanObjectContainer(vkImageList);

    for (int i = 0; i < vkConfig.maxFramsInFlight; i++) {
        vkDestroySemaphore(device, renderFinishedSemaphores[i], getAllocation());
        vkDestroySemaphore(device, imageAvailableSemaphores[i], getAllocation());
        vkDestroyFence(device, inFlightFences[i], getAllocation());
    }

    commandPool->release();

    vkDestroyDevice(device, getAllocation());

    vkValidationLayer->cleanup(instance);
    vkValidationLayer->release();

    vkDestroySurfaceKHR(instance, surface, getAllocation());
    vkDestroyInstance(instance, getAllocation());

    glfwDestroyWindow(window);

    glfwTerminate();
}

void VK_ContextImpl::recreateSwapChain()
{
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(device);

    cleanupSwapChain();

    for (auto shaderSet : vkShaders)
        shaderSet->initUniformBuffer();

    createSwapChain();
    createSwapChainImageViews();
    createRenderPass();

    createGraphicsPipeline();

    createColorResources();
    createDepthResources();
    createFramebuffers();

    createSecondaryCommandBuffer(secondaryCommandBufferCount, secondaryCommandBufferCaller);
    createCommandBuffers();

    imagesInFlight.resize(swapChainImages.size(), VK_NULL_HANDLE);
}

bool VK_ContextImpl::createInstance()
{
    if (!vkValidationLayer->appendValidationLayerSupport()) {
        std::cerr << "validation layers requested, but not available!" << std::endl;
        return false;
    }

    VkApplicationInfo appInfo;
    appInfo.pNext = nullptr;
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = appConfig.name.data();
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Vulkan Framework";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    vkValidationLayer->adjustVkInstanceCreateInfo(createInfo);

    if (vkCreateInstance(&createInfo, getAllocation(), &instance) != VK_SUCCESS) {
        std::cerr << "failed to create instance!" << std::endl;
        return false;
    }

    vkValidationLayer->setupDebugMessenger(instance);
    return true;
}

bool VK_ContextImpl::createSurface()
{
    if (glfwCreateWindowSurface(instance, window, getAllocation(), &surface) != VK_SUCCESS) {
        std::cerr << "failed to create window surface!" << std::endl;
        return false;
    }
    return true;
}

bool VK_ContextImpl::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        std::cerr << "failed to find GPUs with Vulkan support!" << std::endl;
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    std::vector<VkPhysicalDevice> deviceList;

    for (const auto &device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice = device;
            msaaSamples = getMaxUsableSampleCount(physicalDevice);
            deviceList.push_back(device);
            break;
        }
    }

    if (deviceList.empty()) {
        std::cerr << "failed to find a suitable GPU!" << std::endl;
        return false;
    }

    for (auto device : deviceList) {
        vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
        vkGetPhysicalDeviceProperties(device, &deviceProperties);
        std::cout << "device name:" << deviceProperties.deviceName << std::endl;
    }

    physicalDevice = deviceList[0];

    vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);

    return true;
}

bool VK_ContextImpl::createLogicalDevice()
{
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();

    createInfo.pEnabledFeatures = &logicalFeatures;

    auto iter = std::back_inserter(deviceExtensions);
    std::copy(vkConfig.enabledExtensions.begin(), vkConfig.enabledExtensions.end(), iter);

    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    vkValidationLayer->adjustVkDeviceCreateInfo(createInfo);

    if (vkCreateDevice(physicalDevice, &createInfo, getAllocation(), &device) != VK_SUCCESS) {
        std::cerr << "failed to create logical device!" << std::endl;
        return false;
    }

    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
    return true;
}

bool VK_ContextImpl::createSwapChain()
{
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0
            && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;

    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                            VK_IMAGE_USAGE_SAMPLED_BIT;

    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &createInfo, getAllocation(), &swapChain) != VK_SUCCESS) {
        std::cerr << "failed to create swap chain" << std::endl;
        return false;
    }

    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount, VK_NULL_HANDLE);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());

    swapChainImageFormat = surfaceFormat.format;
    vkSwapChainExtent = extent;
    return true;
}

bool VK_ContextImpl::createSwapChainImageViews()
{
    swapChainImageViews.resize(swapChainImages.size());

    for (size_t i = 0; i < swapChainImages.size(); i++) {
        VkImageViewCreateInfo viewCreateInfo = VK_ImageView::createImageViewCreateInfo(swapChainImages[i],
                                                                                       swapChainImageFormat);
        viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        VK_ImageViewImpl *view = new VK_ImageViewImpl(this);
        view->setRemoveFromContainerWhenRelease(false);
        if (!view->create(viewCreateInfo)) {
            std::cerr << "failed to create image views" << std::endl;
            return false;
        }

        swapChainImageViews[i] = view;
    }
    return true;
}

bool VK_ContextImpl::createPipelineCache()
{
    vkPipelineCache = new VK_PipelineCacheImpl(device, vkAllocator->getAllocator(), deviceProperties);
    return vkPipelineCache->create(vkConfig.pipelineCacheFile, appConfig.debug);
}

void VK_ContextImpl::createRenderPass()
{
    renderPass = new VK_RenderPass(this);
    renderPass->create(swapChainImageFormat, msaaSamples);
}

VK_ShaderSet *VK_ContextImpl::createShaderSet()
{
    auto shaderSet = new VK_ShaderSetImpl(this);
    vkShaders.push_back(shaderSet);
    return shaderSet;
}

VK_Buffer *VK_ContextImpl::createVertexBuffer(const std::vector<float> &vertices, uint32_t count,
                                              const std::vector<uint32_t> &indices, bool indirectDraw)
{
    auto vertexBuffer = new VK_VertexBuffer(this);
    vertexBuffer->create(vertices, count, indices, indirectDraw);
    vkBuffers.push_back(vertexBuffer);
    return vertexBuffer;
}

VK_Buffer *VK_ContextImpl::createVertexBuffer(const std::vector<VK_Vertex> &vertices,
                                              const std::vector<uint32_t> &indices, bool indirectDraw)
{
    auto vertexBuffer = new VK_VertexBuffer(this);
    vertexBuffer->create(vertices, indices, indirectDraw);
    vkBuffers.push_back(vertexBuffer);
    return vertexBuffer;
}

VK_Buffer *VK_ContextImpl::createVertexBuffer(const std::string &filename, bool zero,
                                              bool indirectDraw)
{
    VK_OBJLoader *loader = new VK_OBJLoader(this);
    if (!loader->load(filename, zero)) {
        loader->release();
        return nullptr;
    }

    auto data = loader->getData();
    if (!data.empty()) {
        loader->create(data[0], 8, std::vector<uint32_t>(), indirectDraw);
    }
    vkBuffers.push_back(loader);
    return loader;
}

VK_Buffer *VK_ContextImpl::createIndirectBuffer(uint32_t instanceCount, uint32_t oneInstanceSize,
                                                uint32_t vertexCount)
{
    VK_IndirectBuffer *buffer = new VK_IndirectBuffer(this);
    buffer->create(instanceCount, oneInstanceSize, vertexCount);
    vkBuffers.push_back(buffer);
    return buffer;
}

VK_Buffer *VK_ContextImpl::createInstanceBuffer(uint32_t count, uint32_t itemSize, const char *data,
                                                uint32_t bind)
{
    VK_InstanceBuffer *buffer = new VK_InstanceBuffer(this);
    buffer->create(count, itemSize, data, bind);
    vkBuffers.push_back(buffer);
    return buffer;
}

void VK_ContextImpl::removeBuffer(VK_Buffer *buffer)
{
    vkBuffers.remove(buffer);
}

void VK_ContextImpl::addBuffer(VK_Buffer *buffer)
{
    if (buffer)
        vkBuffers.push_back(buffer);
}

VK_Image *VK_ContextImpl::createImage(const std::string &image)
{
    auto ptr = new VK_ImageImpl(this);
    if (!ptr->load(image)) {
        delete ptr;
        return nullptr;
    }

    vkImageList.push_back(ptr);
    return ptr;
}

void VK_ContextImpl::onReleaseImage(VK_Image *image)
{
    if (image)
        vkImageList.remove(image);
}

VK_Sampler *VK_ContextImpl::createSampler(const VkSamplerCreateInfo &samplerInfo)
{
    auto sampler = new VK_SamplerImpl(device, this);
    if (!sampler->create(samplerInfo)) {
        delete sampler;
        return nullptr;
    }
    vkSamplerList.push_back(sampler);
    return sampler;
}

void VK_ContextImpl::removeSampler(VK_Sampler *sampler)
{
    vkSamplerList.remove(sampler);
}

VK_ImageView *VK_ContextImpl::createImageView(const VkImageViewCreateInfo &viewCreateInfo,
                                              uint32_t mipLevels)
{
    auto imageView = new VK_ImageViewImpl(this);
    if (!imageView->create(viewCreateInfo)) {
        delete imageView;
        return nullptr;
    }
    vkImageViewList.push_back(imageView);
    return imageView;
}

void VK_ContextImpl::removeImageView(VK_ImageView *imageView)
{
    vkImageViewList.remove(imageView);
}

bool VK_ContextImpl::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags properties,
                                  VkBuffer &buffer, VkDeviceMemory &bufferMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.pNext = nullptr;

    auto allocation = getAllocation();
    if (vkCreateBuffer(device, &bufferInfo, allocation,
                       &buffer) != VK_SUCCESS) {
        std::cerr << "failed to create buffer!" << std::endl;
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits,
                                               properties);
    allocInfo.pNext = nullptr;

    if (vkAllocateMemory(device, &allocInfo, getAllocation(), &bufferMemory) != VK_SUCCESS) {
        std::cerr << "failed to allocate buffer memory!" << std::endl;
        return false;
    }

    vkBindBufferMemory(device, buffer, bufferMemory, 0);
    return true;
}

void VK_ContextImpl::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandBuffer commandBuffer = getCommandPool()->beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    getCommandPool()->endSingleTimeCommands(commandBuffer, graphicsQueue);
}

VK_QueryPool *VK_ContextImpl::createQueryPool(uint32_t count, VkQueryPipelineStatisticFlags flag,
                                              std::function<void (const std::vector<uint64_t> &)> callback)
{
    if (queryPool)
        return queryPool;

    queryPool = new VK_QueryPoolImpl(this, count, flag);
    queryPool->setQueryCallback(callback);
    return queryPool;
}

VK_CommandPool *VK_ContextImpl::getCommandPool() const
{
    return commandPool;
}

bool VK_ContextImpl::createGraphicsPipeline()
{
    for (auto pipeline : pipelines)
        pipeline->create();
    return true;
}

void VK_ContextImpl::createFramebuffers()
{
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        std::array<VkImageView, 3> attachments = {
            vkColorImageView->getImageView(),
            vkDepthImageView->getImageView(),
            swapChainImageViews[i]->getImageView()
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass->getRenderPass();
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = vkSwapChainExtent.width;
        framebufferInfo.height = vkSwapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, getAllocation(),
                                &swapChainFramebuffers[i]) != VK_SUCCESS) {
            std::cerr << "failed to create framebuffer!" << std::endl;
        }
    }
}

bool VK_ContextImpl::createCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);
    commandPool = new VK_CommandPool(this, queueFamilyIndices);
    return true;
}

void VK_ContextImpl::createColorResources()
{
    vkColorImage = new VK_ImageImpl(this);
    VkFormat colorFormat = swapChainImageFormat;
    bool ok = vkColorImage->createImage(vkSwapChainExtent.width, vkSwapChainExtent.height, msaaSamples,
                                        colorFormat,
                                        VK_IMAGE_TILING_OPTIMAL,
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!ok)
        std::cerr << "create color image failed\n";

    auto createInfo = VK_ImageView::createImageViewCreateInfo(vkColorImage->getImage(), colorFormat);
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vkColorImageView = new VK_ImageViewImpl(this);
    vkColorImageView->create(createInfo);
}

void VK_ContextImpl::createDepthResources()
{
    VkFormat depthFormat = findDepthFormat(physicalDevice);

    vkDepthImage = new VK_ImageImpl(this);
    bool ok = vkDepthImage->createImage(vkSwapChainExtent.width, vkSwapChainExtent.height, msaaSamples,
                                        depthFormat,
                                        VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!ok)
        std::cerr << "create depth image failed\n";

    auto createInfo = VK_ImageView::createImageViewCreateInfo(vkDepthImage->getImage(), depthFormat);
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vkDepthImageView = new VK_ImageViewImpl(this);
    vkDepthImageView->create(createInfo);
}

bool VK_ContextImpl::createCommandBuffers()
{
    commandBuffers.resize(swapChainFramebuffers.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool->getCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t) commandBuffers.size();
    allocInfo.pNext = nullptr;

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        std::cerr << "failed to allocate command buffers!" << std::endl;
        return false;
    }

    for (size_t i = 0; i < commandBuffers.size(); i++) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        if (vkBeginCommandBuffer(commandBuffers[i], &beginInfo) != VK_SUCCESS) {
            std::cerr << "failed to begin recording command buffer!" << std::endl;
        }

        if (!secondaryCommandBuffers.empty()) {
            auto current = secondaryCommandBuffers.at(i);
            current->executeCommandBuffer(commandBuffers[i], swapChainFramebuffers.at(i));
        } else {
            VkRenderPassBeginInfo renderPassInfo{};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = renderPass->getRenderPass();
            renderPassInfo.framebuffer = swapChainFramebuffers[i];
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = vkSwapChainExtent;

            std::array<VkClearValue, 2> clearValues{};
            memcpy((char *)&clearValues[0], &vkClearValue, sizeof(vkClearValue));
            clearValues[1].depthStencil = {1.0f, 0};

            renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            renderPassInfo.pClearValues = clearValues.data();

            if (queryPool)
                queryPool->reset(commandBuffers[i]);

            vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

            if (queryPool) {
                queryPool->startQeury(commandBuffers[i]);
            }

            for (auto pipeline : pipelines) {
                pipeline->render(commandBuffers[i], i);
            }

            if (queryPool)
                queryPool->endQuery(commandBuffers[i]);

            vkCmdEndRenderPass(commandBuffers[i]);
        }

        if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
            std::cout << "failed to record command buffer!" << std::endl;
        }
    }
    return true;
}

bool VK_ContextImpl::createSecondaryCommandBuffer(uint32_t secondaryCommandBufferCount,
                                                  std::shared_ptr<VK_SecondaryCommandBufferCallback> caller)
{
    if (secondaryCommandBufferCount == 0)
        return false;

    this->secondaryCommandBufferCount = secondaryCommandBufferCount;
    secondaryCommandBufferCaller = caller;

    secondaryCommandBuffers.resize(swapChainFramebuffers.size());
    for (uint32_t i = 0; i < swapChainFramebuffers.size(); i++) {
        secondaryCommandBuffers[i] = new VK_SecondaryCommandBuffer(this,
                                                                   getCommandPool()->getCommandPool());
        secondaryCommandBuffers[i]->create(secondaryCommandBufferCount);

        VkCommandBufferInheritanceInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        info.pNext = NULL;
        info.renderPass = getRenderPass();
        info.subpass = 0;
        info.framebuffer = swapChainFramebuffers[i];
        info.occlusionQueryEnable = VK_FALSE;
        info.queryFlags = 0;
        info.pipelineStatistics = 0;

        VkCommandBufferBeginInfo secondaryCommandBufferBeginInfo = {};
        secondaryCommandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        secondaryCommandBufferBeginInfo.pNext = NULL;
        secondaryCommandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
                                                VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
        secondaryCommandBufferBeginInfo.pInheritanceInfo = &info;

        for (uint32_t j = 0; j < secondaryCommandBufferCount; j++) {
            auto commandBuffer = secondaryCommandBuffers[i]->at(j);
            vkBeginCommandBuffer(commandBuffer, &secondaryCommandBufferBeginInfo);

            for (auto pipeline : pipelines) {
                pipeline->render(commandBuffer, i, caller, j, secondaryCommandBufferCount);
            }

            vkEndCommandBuffer(commandBuffer);
        }
    }
    return true;
}

void VK_ContextImpl::createSyncObjects()
{
    auto count = vkConfig.maxFramsInFlight;
    imageAvailableSemaphores.resize(count);
    renderFinishedSemaphores.resize(count);
    inFlightFences.resize(count);
    imagesInFlight.resize(swapChainImages.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < vkConfig.maxFramsInFlight; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, getAllocation(),
                              &imageAvailableSemaphores[i]) != VK_SUCCESS
                ||
                vkCreateSemaphore(device, &semaphoreInfo, getAllocation(),
                                  &renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(device, &fenceInfo, getAllocation(), &inFlightFences[i]) != VK_SUCCESS) {
            std::cerr << "failed to create synchronization objects for a frame!" << std::endl;
        }
    }
}

bool VK_ContextImpl::drawFrame()
{
    for (auto pipeline : pipelines) {
        if (pipeline->needRecreate() || needUpdateSwapChain) {
            recreateSwapChain();
            needUpdateSwapChain = false;
        }
    }

    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX,
                                            imageAvailableSemaphores[currentFrame],
                                            VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return true;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        std::cerr << "failed to acquire swap chain image!" << std::endl;
        return false;
    }

    if (queryPool) {
        queryPool->query();
    }

    currentFrameIndex ++;

    captureImage(imageIndex);

    for (auto shaderSet : vkShaders)
        shaderSet->update(imageIndex);

    if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
        vkWaitForFences(device, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }

    imagesInFlight[imageIndex] = inFlightFences[currentFrame];

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkResetFences(device, 1, &inFlightFences[currentFrame]);

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        std::cerr << "failed to submit draw command buffer!" << std::endl;
        return false;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(presentQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR ||
            result == VK_SUBOPTIMAL_KHR ||
            framebufferResized) {
        framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        std::cerr << "failed to present swap chain image!" << std::endl;
        return false;
    }

    currentFrame = (currentFrame + 1) % vkConfig.maxFramsInFlight;
    return true;
}

VkSurfaceFormatKHR VK_ContextImpl::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>
                                                           &availableFormats)
{
    for (const auto &availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB
                && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR VK_ContextImpl::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>
                                                       &availablePresentModes)
{
    for (const auto &availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VK_ContextImpl::chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities)
{
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width,
                                        capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height,
                                         capabilities.maxImageExtent.height);

        return actualExtent;
    }
}

SwapChainSupportDetails VK_ContextImpl::querySwapChainSupport(VkPhysicalDevice device)
{
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount,
                                                  details.presentModes.data());
    }

    return details;
}

bool VK_ContextImpl::isDeviceSuitable(VkPhysicalDevice device)
{
    QueueFamilyIndices list = findQueueFamilies(device);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
    return list.isComplete() && extensionsSupported && swapChainAdequate
           && deviceFeatures.samplerAnisotropy;
}

bool VK_ContextImpl::checkDeviceExtensionSupport(VkPhysicalDevice device)
{
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto &extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

QueueFamilyIndices VK_ContextImpl::findQueueFamilies(VkPhysicalDevice device)
{
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto &queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

        if (presentSupport)
            indices.presentFamily = i;

        if (indices.isComplete())
            break;
        i++;
    }

    return indices;
}

std::vector<const char *> VK_ContextImpl::getRequiredExtensions()
{
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = nullptr;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char *> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (appConfig.debug)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    return extensions;
}

void VK_ContextImpl::captureImage(uint32_t imageIndex)
{
    if (captureScreen) {
        captureScreen = false;
        std::stringstream stream;
        stream << "capture-";
        stream << currentFrameIndex;
        stream << ".tif";

        VkImage image = swapChainImages[imageIndex];

        auto cmd = getCommandPool()->beginSingleTimeCommands();
        adjustImageLayout(cmd, image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        getCommandPool()->endSingleTimeCommands(cmd, graphicsQueue);
        writeFile(this, stream.str(), image, getSwapChainExtent().width,
                  getSwapChainExtent().height);

        cmd = getCommandPool()->beginSingleTimeCommands();
        adjustImageLayout(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        getCommandPool()->endSingleTimeCommands(cmd, graphicsQueue);
    }
}

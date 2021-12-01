#ifndef VK_CONTEXTIMPL_H
#define VK_CONTEXTIMPL_H
#include <vector>
#include <optional>
#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>
#include <list>
#include <array>
#include <iostream>
#include "VK_Context.h"
#include "VK_UniformBuffer.h"
#include "VK_ImageImpl.h"
#include "VK_ImageViewImpl.h"

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete()
    {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

template<class C>
void cleanVulkanObjectContainer(C& container)
{
    while(true) {
        auto itr = container.begin();
        if (itr == container.end())
            break;
        else
            (*itr)->release();
    }
}

class VK_ContextImpl : public VK_Context
{
    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void mouseButtonCallback(GLFWwindow* window, int button, int state, int mods);
public:
    VK_ContextImpl(const VK_ContextConfig &config);
    ~VK_ContextImpl();
public:
    void release()override;
public:
    bool createWindow(int width, int height, bool resize)override;
    void setOnFrameSizeChanged(std::function<void(int, int)> cb) override;

    bool initVulkanDevice(const VK_Config& config)override;
    bool initVulkanContext()override;

    bool initPipeline(VK_ShaderSet* shaderSet)override;
    bool createCommandBuffers()override;
    bool run()override;
public:
    VkExtent2D getSwapChainExtent()const override;

    VK_Viewports getViewports()const override;
    void setViewports(const VK_Viewports& viewport)override;
public:
    void setClearColor(float r, float g, float b, float a)override;
    void setClearDepthStencil(float depth, uint32_t stencil)override;

    void setDescriptorSetLayoutBindingGroup(const VK_DescriptorSetLayoutBindingGroup& group)override;
    VK_DescriptorSetLayoutBindingGroup getDescriptorSetLayoutBindingGroup()const override;

    void setDescriptorPoolSizeGroup(const VK_VkDescriptorPoolSizeGroup& group)override;
    VK_VkDescriptorPoolSizeGroup getDescriptorPoolSizeGroup()const override;

    VkPipelineColorBlendAttachmentState getColorBlendAttachmentState()override;
    void setColorBlendAttachmentState(const VkPipelineColorBlendAttachmentState& state)override;
public:
    void setDynamicState(VkDynamicState dynamicState)override;
    VkPipelineDynamicStateCreateInfo createDynamicStateCreateInfo(
        VkPipelineDynamicStateCreateFlags flags = 0);
public:
    VK_ShaderSet* createShaderSet()override;

    VK_Buffer* createVertexBuffer(const std::vector<float>& vertices, uint32_t count, const std::vector<uint16_t>& indices = std::vector<uint16_t>())override;
    VK_Buffer* createVertexBuffer(const std::vector<VK_Vertex>& vertices, const std::vector<uint16_t>& indices = std::vector<uint16_t>())override;
    void removeBuffer(VK_Buffer* buffer)override;
    void addBuffer(VK_Buffer* buffer)override;
public:
    VK_Image* createImage(const std::string& image)override;
    void onReleaseImage(VK_Image* image);

    VK_Sampler* createSampler(const VkSamplerCreateInfo& samplerInfo)override;
    void removeSampler(VK_Sampler* sampler);

    VK_ImageView* createImageView(const VkImageViewCreateInfo& viewCreateInfo)override;
    void addImageView(VK_ImageView* imageView)override;
    void removeImageView(VK_ImageView* imageView);

    VK_UniformBuffer* createUniformBuffer(uint32_t binding, uint32_t bufferSize)override;
    void addUniformBuffer(VK_UniformBuffer* uniformBuffer)override;
    void removeUniformBuffer(VK_UniformBuffer* uniforBuffer);
public:
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory)override;
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)override;
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)override;

    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
private:
    void recreateSwapChain();

    void cleanupSwapChain();
    void cleanup();

    bool createInstance();
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
    bool setupDebugMessenger();
    bool createSurface();
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createSwapChain();
    bool createSwapChainImageViews();
    void createRenderPass();
    bool createDescriptorSetLayout();

    bool isValidPipelineCacheData(const std::string& filename, const char* buffer, uint32_t size);
    void createGraphicsPiplelineCache();
    bool saveGraphicsPiplineCache();
    bool createGraphicsPipeline();

    void createFramebuffers();

    bool createCommandPool();

    void createDepthResources();

    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

    VkFormat findDepthFormat();

    bool hasStencilComponent(VkFormat format);

    void createDescriptorPool();

    void createDescriptorSets();

    void createSyncObjects();

    void drawFrame();

    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);

    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);

    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

    bool isDeviceSuitable(VkPhysicalDevice device);

    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    std::vector<const char*> getRequiredExtensions();
    bool checkValidationLayerSupport();

    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
private:
    void initClearColorAndDepthStencil();
    void initColorBlendAttachmentState();
    void initViewport();
private:
    VK_ContextConfig appConfig;
    VK_Config vkConfig;

    GLFWwindow* window = nullptr;
    std::function<void(int, int)> windowSizeChangedCallback;

    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkSurfaceKHR surface;

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceFeatures deviceFeatures;
    VkPhysicalDeviceProperties deviceProperties;
    VkDevice device = nullptr;

    VkQueue graphicsQueue;
    VkQueue presentQueue;

    VkSwapchainKHR swapChain;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D vkSwapChainExtent;

    std::vector<VK_ImageView*> swapChainImageViews;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    VkRenderPass renderPass;
    VK_DescriptorSetLayoutBindingGroup vkDescriptorSetLayoutBindingGroup;
    VkDescriptorSetLayout descriptorSetLayout = 0;

    VK_VkDescriptorPoolSizeGroup vkDescriptorPoolSizeGroup;

    std::vector<VkDynamicState> vkDynamicStates;

    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline = 0;
    VkPipelineCache pipelineCache = 0;

    VkCommandPool commandPool = 0;
    std::vector<VkCommandBuffer> commandBuffers;

    VK_ImageImpl* vkDepthImage = nullptr;
    VK_ImageViewImpl* vkDepthImageView = nullptr;

    VkDescriptorPool descriptorPool = 0;
    std::vector<VkDescriptorSet> descriptorSets;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;
    size_t currentFrame = 0;

    bool framebufferResized = false;

    VK_ShaderSet* vkShaderSet = nullptr;
    std::list<VK_Buffer*> vkBuffers;

    std::list<VK_UniformBuffer*> vkUniformBuffers;

    bool vkNeedUpdateSwapChain = false;
    VK_Viewports vkViewports;

    VkClearValue vkClearValue;
    VkPipelineColorBlendAttachmentState vkColorBlendAttachment{};

    std::list<VK_Image*> vkImageList;
    std::list<VK_Sampler*> vkSamplerList;
    std::list<VK_ImageView*> vkImageViewList;
    std::set<VK_ImageView*> vkUnusedImageViewList;
};


#endif // VK_CONTEXTIMPL_H

#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <array>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;

    static VkVertexInputBindingDescription getBindingDesc() {
        VkVertexInputBindingDescription d{};
        d.binding = 0;
        d.stride = sizeof(Vertex);
        d.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return d;
    }
    static std::array<VkVertexInputAttributeDescription, 3> getAttrDescs() {
        std::array<VkVertexInputAttributeDescription, 3> a{};
        a[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)};
        a[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
        a[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord)};
        return a;
    }
};

struct TextureHandle { int id = -1; bool valid() const { return id >= 0; } };
struct MeshHandle { int id = -1; bool valid() const { return id >= 0; } };

struct SubMesh {
    MeshHandle mesh;
    TextureHandle texture;
    std::vector<TextureHandle> animTextures;
};

struct SceneObject {
    std::vector<SubMesh> submeshes;
    glm::mat4 transform = glm::mat4(1.0f);
    bool animatable = false;
    int animFrame = 0;
    bool unlit = false;
    glm::vec4 unlitColor = {1,1,1,1};

    void nextAnimFrame() {
        if (!animatable) return;
        ++animFrame;
        for (auto& sm : submeshes)
            if (!sm.animTextures.empty())
                sm.texture = sm.animTextures[animFrame % (int)sm.animTextures.size()];
    }
};

struct FrameContext {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint32_t imageIndex = 0;
    int frameIndex = 0;
    bool valid = false;
};

class Engine {
public:
    static constexpr int MAX_FRAMES = 2;

    void init(GLFWwindow* window);
    void cleanup();
    FrameContext beginFrame();
    void endFrame(const FrameContext& ctx);
    void recreateSwapchain();

    TextureHandle loadTexture(const std::string& path);
    TextureHandle createWhiteTexture();
    MeshHandle createMesh(const std::vector<Vertex>& verts, const std::vector<uint32_t>& indices);

    VkDevice getDevice() const { return device; }
    VkPhysicalDevice getPhysDevice() const { return physDevice; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    VkCommandPool getCommandPool() const { return commandPool; }
    uint32_t getGraphicsFamily() const { return graphicsFamily; }

    VkExtent2D getSwapExtent() const { return swapExtent; }
    VkFormat getSwapFormat() const { return swapFormat; }
    const std::vector<VkImageView>& getSwapImageViews() const { return swapImageViews; }
    size_t getSwapImageCount() const { return swapImages.size(); }

    VkDescriptorSetLayout getMaterialLayout() const { return materialLayout; }
    VkDescriptorSet getTextureSet(TextureHandle h) const { return (h.valid()) ? textures[h.id].set : VK_NULL_HANDLE; }

    void bindAndDrawMesh_(VkCommandBuffer cmd, MeshHandle h) const {
        if (!h.valid()) return;
        const auto& m = meshes[h.id];
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m.vb, &offset);
        vkCmdBindIndexBuffer(cmd, m.ib, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, m.indexCount, 1, 0, 0, 0);
    }

    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags flags) const;
    VkFormat findDepthFormat() const;
    std::vector<char> readFile(const std::string& path) const;

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buf, VkDeviceMemory& mem);
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);

    VkCommandBuffer beginSingleTime();
    void endSingleTime(VkCommandBuffer cmd);

    void createImage(uint32_t w, uint32_t h, uint32_t layers, VkFormat fmt, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags props, VkImage& img, VkDeviceMemory& mem);
    void transitionLayout(VkImage img, uint32_t layers, VkFormat fmt, VkImageLayout from, VkImageLayout to);
    void copyBufToImage(VkBuffer buf, VkImage img, uint32_t w, uint32_t h);

    VkImageView createImageView(VkImage img, VkFormat fmt, VkImageAspectFlags aspect, uint32_t baseLayer, uint32_t layerCount, VkImageViewType viewType) const;
    VkShaderModule createShaderModule(const std::vector<char>& code) const;

private:
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    uint32_t graphicsFamily = 0;
    uint32_t presentFamily = 0;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapImageViews;
    VkFormat swapFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapExtent = {};

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkSemaphore> imageAvailable;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;
    int currentFrame = 0;

    struct TextureRes {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    struct MeshRes {
        VkBuffer vb = VK_NULL_HANDLE, ib = VK_NULL_HANDLE;
        VkDeviceMemory vm = VK_NULL_HANDLE, im = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
    };
    std::vector<TextureRes> textures;
    std::vector<MeshRes> meshes;
    TextureHandle cachedWhiteTex;

    VkDescriptorPool materialPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialLayout = VK_NULL_HANDLE;

    void createInstance_();
    void createSurface_(GLFWwindow* w);
    void pickPhysDevice_();
    void createDevice_();
    void createSwapchain_();
    void createCommandPool_();
    void createCommandBuffers_();
    void createSyncObjects_();
    void createMaterialLayout_();
    void createMaterialPool_();
    void cleanupSwapchain_();

    TextureHandle registerTexture_(uint32_t w, uint32_t h, const unsigned char* pixels, VkDeviceSize size);
};

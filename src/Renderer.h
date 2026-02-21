#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "Types.h"
#include "Window.h"
#include <vector>
#include <array>
#include <optional>
#include <string>

class Renderer {
public:
    explicit Renderer(Window& window);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void uploadMesh(const MeshData& mesh);
    void clearMeshes();

    bool beginFrame();
    void draw(int meshIdx, const glm::mat4& model);
    void endFrame();

    void setCamera(glm::vec3 eye, glm::vec3 target, glm::vec3 up);
    void setLight(glm::vec3 pos, glm::vec3 color, float specPow = 64.f);
    void setUV(glm::vec2 offset, glm::vec2 scale);
    void setAnim(int mode, float time);

    int meshCount() const { return (int)m_meshes.size(); }

private:
    struct QFI {
        std::optional<uint32_t> gfx, present;
        bool ok() const { return gfx && present; }
    };
    struct SCSupport {
        VkSurfaceCapabilitiesKHR caps{};
        std::vector<VkSurfaceFormatKHR> fmts;
        std::vector<VkPresentModeKHR>   modes;
    };
    struct GpuMesh {
        VkBuffer vb = VK_NULL_HANDLE, ib = VK_NULL_HANDLE;
        VkDeviceMemory vm = VK_NULL_HANDLE, im = VK_NULL_HANDLE;
        uint32_t count = 0;

        VkImage        texImg  = VK_NULL_HANDLE;
        VkDeviceMemory texMem  = VK_NULL_HANDLE;
        VkImageView    texView = VK_NULL_HANDLE;
        VkSampler      texSampler = VK_NULL_HANDLE;
        bool           hasTex  = false;

        std::array<VkDescriptorSet, 2> ds{};
    };

    static constexpr int FRAMES = 2;
    static constexpr int MAX_MESHES = 64;

    void initInstance();
    void initSurface();
    void initDevice();
    void initSwapchain();
    void initImageViews();
    void initRenderPass();
    void initDepth();
    void initFramebuffers();
    void initCmdPool();
    void initDescLayout();
    void initPipeline();
    void initWhiteTex();
    void initUBOs();
    void initDescPool();
    void initCmdBuffers();
    void initSync();

    void recreateSwapchain();
    void destroySwapchain();

    QFI       findQFI(VkPhysicalDevice);
    SCSupport querySC(VkPhysicalDevice);
    bool      deviceOk(VkPhysicalDevice);

    VkSurfaceFormatKHR pickFmt(const std::vector<VkSurfaceFormatKHR>&);
    VkPresentModeKHR   pickMode(const std::vector<VkPresentModeKHR>&);
    VkExtent2D         pickExtent(const VkSurfaceCapabilitiesKHR&);
    VkFormat           depthFmt();
    VkFormat           findFmt(const std::vector<VkFormat>&, VkImageTiling, VkFormatFeatureFlags);
    uint32_t           memType(uint32_t filter, VkMemoryPropertyFlags);
    VkShaderModule     makeShader(const std::vector<char>&);
    std::vector<char>  readFile(const std::string&);

    void mkBuf(VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags, VkBuffer&, VkDeviceMemory&);
    void cpBuf(VkBuffer, VkBuffer, VkDeviceSize);
    void mkImg(uint32_t, uint32_t, VkFormat, VkImageTiling, VkImageUsageFlags, VkMemoryPropertyFlags, VkImage&, VkDeviceMemory&);
    VkImageView mkView(VkImage, VkFormat, VkImageAspectFlags);
    void transitionImg(VkImage, VkFormat, VkImageLayout, VkImageLayout);
    void cpBufToImg(VkBuffer, VkImage, uint32_t, uint32_t);
    VkCommandBuffer beginOnce();
    void            endOnce(VkCommandBuffer);

    void allocMeshDescSets(GpuMesh& gm);
    void writeMeshDescSets(GpuMesh& gm);
    void uploadTextureToMesh(GpuMesh& gm, const std::string& path);
    void updateUBO(uint32_t frame, const glm::mat4& model);

    Window& m_win;

    VkInstance               m_inst   = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_dbg    = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surf   = VK_NULL_HANDLE;
    VkPhysicalDevice         m_gpu    = VK_NULL_HANDLE;
    VkDevice                 m_dev    = VK_NULL_HANDLE;
    VkQueue                  m_gfxQ   = VK_NULL_HANDLE;
    VkQueue                  m_presQ  = VK_NULL_HANDLE;
    uint32_t                 m_gfxFam = 0;
    uint32_t                 m_presFam= 0;

    VkSwapchainKHR           m_sc     = VK_NULL_HANDLE;
    std::vector<VkImage>     m_scImgs;
    VkFormat                 m_scFmt  = VK_FORMAT_UNDEFINED;
    VkExtent2D               m_scExt  {};
    std::vector<VkImageView> m_scViews;

    VkRenderPass               m_rp   = VK_NULL_HANDLE;
    VkImage                    m_di   = VK_NULL_HANDLE;
    VkDeviceMemory             m_dm   = VK_NULL_HANDLE;
    VkImageView                m_dv   = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_fbs;

    VkDescriptorSetLayout m_dsl  = VK_NULL_HANDLE;
    VkPipelineLayout      m_pl   = VK_NULL_HANDLE;
    VkPipeline            m_pipe = VK_NULL_HANDLE;

    VkCommandPool                m_cp = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_cmds;

    std::array<VkBuffer,       FRAMES> m_ub  {};
    std::array<VkDeviceMemory, FRAMES> m_ubm {};
    std::array<void*,          FRAMES> m_ubp {};

    VkDescriptorPool m_dp = VK_NULL_HANDLE;

    VkImage        m_wImg = VK_NULL_HANDLE;
    VkDeviceMemory m_wMem = VK_NULL_HANDLE;
    VkImageView    m_wV   = VK_NULL_HANDLE;
    VkSampler      m_wS   = VK_NULL_HANDLE;

    std::vector<GpuMesh> m_meshes;

    std::array<VkSemaphore, FRAMES> m_imgReady{};
    std::array<VkSemaphore, FRAMES> m_renDone{};
    std::array<VkFence,     FRAMES> m_fence{};

    UBO      m_ubo{};
    uint32_t m_frame  = 0;
    uint32_t m_imgIdx = 0;
    bool     m_recording = false;
    bool     m_validation = false;
};

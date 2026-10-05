#ifndef RENDER_VK_VULKANRENDERDEVICE_H
#define RENDER_VK_VULKANRENDERDEVICE_H

#include "render/rhi/RenderDevice.h"

#include <memory>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

// Vulkan 1.0 implementation of the RHI.
//
// This backend is why the interface looks the way it does: frames are explicitly acquired and
// presented, commands are recorded into a command buffer, constants are fed through a uniform buffer
// with dynamic offsets (one block per draw, per frame in flight), and clip space is handed to the app
// as [0,1] with +Y down.
//
// Deliberate v1 simplifications (all documented where they matter):
//   * one queue family that supports both graphics and present
//   * single-sample attachments (no MSAA yet)
//   * every buffer lives in host-visible coherent memory and stays mapped (no staging, no allocator)
//   * the swapchain is recreated wholesale on resize / OUT_OF_DATE, and the render pass is created
//     once from the first swapchain format
class VulkanRenderDevice : public IRenderDevice
{
public:
    VulkanRenderDevice();
    ~VulkanRenderDevice() override;

    VulkanRenderDevice(const VulkanRenderDevice &) = delete;
    VulkanRenderDevice &operator=(const VulkanRenderDevice &) = delete;

    // Instance, surface, device and queues; call it before createSwapchain()
    bool create(const NativeWindowHandle &window);
    void destroy();

    // IRenderDevice
    const char *backendName() const override { return backend_name.c_str(); }
    ShaderLanguage shaderLanguage() const override { return ShaderLanguage::SPIRV; }
    std::uint32_t uniformBufferAlignment() const override { return uniform_buffer_alignment; }
    std::uint32_t framesInFlight() const override { return kFramesInFlight; }
    ClipDepth clipDepth() const override { return ClipDepth::ZeroToOne; }
    bool flipY() const override { return true; }

    bool createSwapchain(const SwapchainDesc &desc) override;
    void destroySwapchain() override;
    bool resizeSwapchain(std::uint32_t width, std::uint32_t height) override;
    std::uint32_t swapchainWidth() const override { return extent.width; }
    std::uint32_t swapchainHeight() const override { return extent.height; }

    BufferHandle createBuffer(const BufferDesc &desc) override;
    void destroyBuffer(BufferHandle buffer) override;
    void updateBuffer(BufferHandle buffer, const void *data, std::uint32_t size,
                      std::uint32_t offset) override;

    ShaderHandle createShader(const ShaderDesc &desc) override;
    void destroyShader(ShaderHandle shader) override;

    PipelineHandle createPipeline(const PipelineDesc &desc) override;
    void destroyPipeline(PipelineHandle pipeline) override;

    ICommandList &getCommandList() override;
    void beginFrame() override;
    void endFrame() override;
    void waitIdle() override;

    const char *lastError() const override { return error; }

private:
    static constexpr std::uint32_t kFramesInFlight = 2;

    struct BufferSlot
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
        BufferUsage usage = BufferUsage::Vertex;
        void *mapped = nullptr;  // persistent mapping: updateBuffer is a memcpy
    };

    struct ShaderSlot
    {
        VkShaderModule module = VK_NULL_HANDLE;
        ShaderStage stage = ShaderStage::Vertex;
    };

    struct PipelineSlot
    {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        std::vector<VertexAttribute> attributes;
        std::uint32_t vertex_stride = 0;
    };

    // ---- setup steps ----
    bool createInstance();
    bool createSurface(const NativeWindowHandle &window);
    bool pickPhysicalDevice();
    bool createLogicalDevice();
    bool createRenderPass();
    bool createSwapchainObjects(std::uint32_t width, std::uint32_t height);
    void destroySwapchainObjects();
    bool createDepthResources();
    bool createFramebuffers();
    bool createCommandObjects();
    bool createSyncObjects();
    bool createDescriptorObjects();
    bool recreateSwapchain();

    // ---- helpers ----
    bool check(VkResult result, const char *what);  // logs and records lastError on failure
    std::uint32_t findMemoryType(std::uint32_t type_bits, VkMemoryPropertyFlags properties) const;
    bool createImage(std::uint32_t width, std::uint32_t height, VkFormat format,
                     VkImageUsageFlags usage, VkImage &image, VkDeviceMemory &memory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) const;
    VkCommandBuffer currentCommandBuffer() const { return command_buffers[current_frame]; }
    void updateUniformDescriptor(VkBuffer buffer, VkDeviceSize range);

    BufferSlot *bufferSlot(BufferHandle handle);
    ShaderSlot *shaderSlot(ShaderHandle handle);
    PipelineSlot *pipelineSlot(PipelineHandle handle);

    // ---- Vulkan objects ----
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties device_properties{};
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queue_family = 0;
    VkQueue queue = VK_NULL_HANDLE;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_views;
    std::vector<VkFramebuffer> framebuffers;

    // Created once from the first swapchain format and kept for the device's lifetime: pipelines
    // reference it, so recreating the swapchain must not destroy it
    VkRenderPass render_pass = VK_NULL_HANDLE;

    VkImage depth_image = VK_NULL_HANDLE;
    VkDeviceMemory depth_memory = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;

    std::vector<VkSemaphore> image_available;
    std::vector<VkSemaphore> render_finished;
    std::vector<VkFence> in_flight_fences;

    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptor_sets;
    std::vector<VkBuffer> descriptor_buffer;      // what each frame's set currently points at
    std::vector<VkDeviceSize> descriptor_range;   // ... and with which range

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool vsync = true;
    std::uint32_t current_frame = 0;
    std::uint32_t image_index = 0;
    bool frame_active = false;
    bool swapchain_ready = false;
    bool swapchain_out_of_date = false;
    std::uint32_t uniform_buffer_alignment = 64;
    std::string backend_name;
    const char *error = nullptr;

    std::vector<BufferSlot> buffers;
    std::vector<ShaderSlot> shaders;
    std::vector<PipelineSlot> pipelines;
    PipelineSlot *bound_pipeline = nullptr;

    class CommandList;
    std::unique_ptr<CommandList> command_list;
};

#endif  // RENDER_VK_VULKANRENDERDEVICE_H

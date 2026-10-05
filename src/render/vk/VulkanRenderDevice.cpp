// The Win32 surface extension has to be requested before <vulkan/vulkan.h> is seen, and
// VkWin32SurfaceCreateInfoKHR needs HWND/HINSTANCE. This is a platform-specific backend, so
// <windows.h> is fine here (NOMINMAX keeps std::min/std::max usable, unlike in core/engine).
#define VK_USE_PLATFORM_WIN32_KHR
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "render/vk/VulkanRenderDevice.h"

#include "core/log/LogManager.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace
{

const char *kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool validationLayerAvailable()
{
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const VkLayerProperties &layer : layers) {
        if (std::strcmp(layer.layerName, kValidationLayer) == 0) {
            return true;
        }
    }
    return false;
}

VkBufferUsageFlags bufferUsageFlags(BufferUsage usage)
{
    switch (usage) {
        case BufferUsage::Vertex:  return VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        case BufferUsage::Index:   return VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        case BufferUsage::Uniform: return VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    return 0;
}

VkFormat vertexAttributeFormat(VertexFormat format)
{
    switch (format) {
        case VertexFormat::Float32:   return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::Float32x2: return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float32x3: return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float32x4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

const char *resultName(VkResult result)
{
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        default: return "VkResult(?)";
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Command list
//
// The one shape difference to OpenGL that the RHI insists on: commands are recorded into a real
// command buffer. clear() begins the render pass (Vulkan clears as part of beginning a pass), so in
// this backend it has to be the first recorded command of a frame - exactly what Sandbox::render does.
// ---------------------------------------------------------------------------
class VulkanRenderDevice::CommandList : public ICommandList
{
public:
    explicit CommandList(VulkanRenderDevice &device) : device(device) {}

    void begin() override
    {
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!device.check(vkBeginCommandBuffer(device.currentCommandBuffer(), &info),
                          "vkBeginCommandBuffer")) {
            return;
        }
        recording = true;
        render_pass_active = false;
    }

    void end() override
    {
        if (!recording) {
            return;
        }
        if (render_pass_active) {
            vkCmdEndRenderPass(device.currentCommandBuffer());
            render_pass_active = false;
        }
        device.check(vkEndCommandBuffer(device.currentCommandBuffer()), "vkEndCommandBuffer");
        recording = false;
    }

    void setViewport(std::uint32_t width, std::uint32_t height) override
    {
        if (!recording) {
            return;
        }
        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(width);
        viewport.height = static_cast<float>(height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(device.currentCommandBuffer(), 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.offset = {0, 0};
        scissor.extent = {width, height};
        vkCmdSetScissor(device.currentCommandBuffer(), 0, 1, &scissor);
    }

    void clear(float red, float green, float blue, float alpha, float depth) override
    {
        if (!recording || render_pass_active) {
            LOG_WARNING() << "vulkan: clear() is only valid as the first command of a frame";
            return;
        }

        VkClearValue clear_values[2] = {};
        clear_values[0].color = {{red, green, blue, alpha}};
        clear_values[1].depthStencil = {depth, 0};

        VkRenderPassBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = device.render_pass;
        info.framebuffer = device.framebuffers[device.image_index];
        info.renderArea.offset = {0, 0};
        info.renderArea.extent = device.extent;
        info.clearValueCount = 2;
        info.pClearValues = clear_values;

        vkCmdBeginRenderPass(device.currentCommandBuffer(), &info, VK_SUBPASS_CONTENTS_INLINE);
        render_pass_active = true;

        // The pipeline uses dynamic viewport/scissor state, so it has to be set after the pass begins
        setViewport(device.extent.width, device.extent.height);
    }

    void bindPipeline(PipelineHandle pipeline) override
    {
        VulkanRenderDevice::PipelineSlot *slot = device.pipelineSlot(pipeline);
        if (slot == nullptr) {
            LOG_ERROR() << "vulkan: bindPipeline with an invalid handle " << pipeline;
            return;
        }
        vkCmdBindPipeline(device.currentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS, slot->pipeline);
        device.bound_pipeline = slot;
    }

    void bindVertexBuffer(BufferHandle buffer) override
    {
        VulkanRenderDevice::BufferSlot *slot = device.bufferSlot(buffer);
        if (slot == nullptr || slot->usage != BufferUsage::Vertex) {
            LOG_ERROR() << "vulkan: bindVertexBuffer with a non-vertex buffer " << buffer;
            return;
        }
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(device.currentCommandBuffer(), 0, 1, &slot->buffer, &offset);
    }

    void bindIndexBuffer(BufferHandle buffer, IndexFormat format) override
    {
        VulkanRenderDevice::BufferSlot *slot = device.bufferSlot(buffer);
        if (slot == nullptr || slot->usage != BufferUsage::Index) {
            LOG_ERROR() << "vulkan: bindIndexBuffer with a non-index buffer " << buffer;
            return;
        }
        const VkIndexType index_type = format == IndexFormat::UInt16 ? VK_INDEX_TYPE_UINT16
                                                                    : VK_INDEX_TYPE_UINT32;
        vkCmdBindIndexBuffer(device.currentCommandBuffer(), slot->buffer, 0, index_type);
    }

    void bindUniformBuffer(BufferHandle buffer, std::uint32_t slot, std::uint32_t offset,
                           std::uint32_t size) override
    {
        VulkanRenderDevice::BufferSlot *buffer_slot = device.bufferSlot(buffer);
        if (buffer_slot == nullptr || buffer_slot->usage != BufferUsage::Uniform) {
            LOG_ERROR() << "vulkan: bindUniformBuffer with a non-uniform buffer " << buffer;
            return;
        }
        if (device.bound_pipeline == nullptr) {
            LOG_ERROR() << "vulkan: bind a pipeline before binding its constants";
            return;
        }

        // The descriptor set of the current frame points at the buffer; the per-draw position inside
        // it travels as the dynamic offset. That is what makes one buffer serve every draw without
        // rewriting descriptors mid-frame.
        device.updateUniformDescriptor(buffer_slot->buffer, size);

        const VkDescriptorSet set = device.descriptor_sets[device.current_frame];
        const std::uint32_t dynamic_offset = offset;
        vkCmdBindDescriptorSets(device.currentCommandBuffer(), VK_PIPELINE_BIND_POINT_GRAPHICS,
                                device.bound_pipeline->layout, 0, 1, &set, 1, &dynamic_offset);
    }

    void drawIndexed(std::uint32_t index_count, std::uint32_t first_index) override
    {
        if (!recording || !render_pass_active) {
            LOG_ERROR() << "vulkan: drawIndexed outside of a frame (clear() must come first)";
            return;
        }
        vkCmdDrawIndexed(device.currentCommandBuffer(), index_count, 1, first_index, 0, 0);
    }

private:
    VulkanRenderDevice &device;
    bool recording = false;
    bool render_pass_active = false;
};

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

VulkanRenderDevice::VulkanRenderDevice() = default;

VulkanRenderDevice::~VulkanRenderDevice()
{
    destroy();
}

bool VulkanRenderDevice::check(VkResult result, const char *what)
{
    if (result == VK_SUCCESS) {
        return true;
    }
    LOG_ERROR() << "vulkan: " << what << " failed: " << resultName(result);
    error = what;
    return false;
}

bool VulkanRenderDevice::create(const NativeWindowHandle &window)
{
    error = nullptr;

    if (!createInstance()) {
        destroy();
        return false;
    }
    if (!createSurface(window)) {
        destroy();
        return false;
    }
    if (!pickPhysicalDevice()) {
        destroy();
        return false;
    }
    if (!createLogicalDevice()) {
        destroy();
        return false;
    }
    if (!createCommandObjects()) {
        destroy();
        return false;
    }
    if (!createSyncObjects()) {
        destroy();
        return false;
    }
    if (!createDescriptorObjects()) {
        destroy();
        return false;
    }

    command_list.reset(new CommandList(*this));
    return true;
}

bool VulkanRenderDevice::createInstance()
{
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "stv3d-lab";
    app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app_info.pEngineName = "stv3d-lab";
    app_info.apiVersion = VK_API_VERSION_1_0;

    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};

    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = 2;
    create_info.ppEnabledExtensionNames = extensions;

    // Validation layers are a development aid: use them when they are installed, otherwise carry on
    const char *layers[] = {kValidationLayer};
    if (validationLayerAvailable()) {
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = layers;
        LOG_INFO() << "vulkan: validation layer enabled";
    }

    return check(vkCreateInstance(&create_info, nullptr, &instance), "vkCreateInstance");
}

bool VulkanRenderDevice::createSurface(const NativeWindowHandle &window)
{
    if (!window.isValid()) {
        error = "no native window handle";
        return false;
    }

    VkWin32SurfaceCreateInfoKHR create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    create_info.hinstance = static_cast<HINSTANCE>(window.instance);
    create_info.hwnd = static_cast<HWND>(window.window);

    return check(vkCreateWin32SurfaceKHR(instance, &create_info, nullptr, &surface),
                 "vkCreateWin32SurfaceKHR");
}

bool VulkanRenderDevice::pickPhysicalDevice()
{
    std::uint32_t count = 0;
    if (!check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "vkEnumeratePhysicalDevices")
        || count == 0) {
        error = "no Vulkan device found";
        LOG_ERROR() << "vulkan: no device found";
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    std::uint32_t best_family = 0;
    int best_score = -1;

    for (const VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(candidate, &properties);

        // One family that can both render and present keeps the backend simple (true on any desktop)
        std::uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> family_properties(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, family_properties.data());

        std::uint32_t family = UINT32_MAX;
        for (std::uint32_t i = 0; i < families; ++i) {
            VkBool32 present_supported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present_supported);
            if ((family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && present_supported) {
                family = i;
                break;
            }
        }
        if (family == UINT32_MAX) {
            continue;
        }

        // Swapchain support: at least one format and one present mode
        std::uint32_t format_count = 0;
        std::uint32_t mode_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &format_count, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(candidate, surface, &mode_count, nullptr);
        if (format_count == 0 || mode_count == 0) {
            continue;
        }

        const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 100;
        if (score > best_score) {
            best_score = score;
            best = candidate;
            best_family = family;
        }
    }

    if (best == VK_NULL_HANDLE) {
        error = "no Vulkan device with a graphics+present queue and swapchain support";
        LOG_ERROR() << "vulkan: " << error;
        return false;
    }

    physical_device = best;
    queue_family = best_family;
    vkGetPhysicalDeviceProperties(physical_device, &device_properties);
    return true;
}

bool VulkanRenderDevice::createLogicalDevice()
{
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;

    const char *extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkPhysicalDeviceFeatures features = {};

    VkDeviceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = 1;
    create_info.pQueueCreateInfos = &queue_info;
    create_info.enabledExtensionCount = 1;
    create_info.ppEnabledExtensionNames = extensions;
    create_info.pEnabledFeatures = &features;

    if (!check(vkCreateDevice(physical_device, &create_info, nullptr, &device), "vkCreateDevice")) {
        return false;
    }

    vkGetDeviceQueue(device, queue_family, 0, &queue);
    uniform_buffer_alignment = static_cast<std::uint32_t>(
        std::max<VkDeviceSize>(16, device_properties.limits.minUniformBufferOffsetAlignment));

    backend_name = "Vulkan ";
    backend_name += std::to_string(VK_VERSION_MAJOR(device_properties.apiVersion));
    backend_name += ".";
    backend_name += std::to_string(VK_VERSION_MINOR(device_properties.apiVersion));
    backend_name += " | ";
    backend_name += device_properties.deviceName;
    return true;
}

bool VulkanRenderDevice::createCommandObjects()
{
    VkCommandPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = queue_family;

    if (!check(vkCreateCommandPool(device, &pool_info, nullptr, &command_pool), "vkCreateCommandPool")) {
        return false;
    }

    command_buffers.resize(kFramesInFlight);
    VkCommandBufferAllocateInfo allocate_info = {};
    allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.commandPool = command_pool;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = kFramesInFlight;
    return check(vkAllocateCommandBuffers(device, &allocate_info, command_buffers.data()),
                 "vkAllocateCommandBuffers");
}

bool VulkanRenderDevice::createSyncObjects()
{
    VkSemaphoreCreateInfo semaphore_info = {};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fence_info = {};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // so the first wait returns immediately

    image_available.resize(kFramesInFlight);
    render_finished.resize(kFramesInFlight);
    in_flight_fences.resize(kFramesInFlight);

    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (!check(vkCreateSemaphore(device, &semaphore_info, nullptr, &image_available[i]),
                   "vkCreateSemaphore")
            || !check(vkCreateSemaphore(device, &semaphore_info, nullptr, &render_finished[i]),
                      "vkCreateSemaphore")
            || !check(vkCreateFence(device, &fence_info, nullptr, &in_flight_fences[i]),
                      "vkCreateFence")) {
            return false;
        }
    }
    return true;
}

bool VulkanRenderDevice::createDescriptorObjects()
{
    // One dynamic uniform buffer per set: the buffer is bound once per frame, the per-draw position
    // comes from the dynamic offset
    VkDescriptorSetLayoutBinding binding = {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = 1;
    layout_info.pBindings = &binding;
    if (!check(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &descriptor_set_layout),
               "vkCreateDescriptorSetLayout")) {
        return false;
    }

    VkDescriptorPoolSize pool_size = {};
    pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    pool_size.descriptorCount = kFramesInFlight;

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.maxSets = kFramesInFlight;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = &pool_size;
    if (!check(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool),
               "vkCreateDescriptorPool")) {
        return false;
    }

    std::vector<VkDescriptorSetLayout> layouts(kFramesInFlight, descriptor_set_layout);
    VkDescriptorSetAllocateInfo allocate_info = {};
    allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate_info.descriptorPool = descriptor_pool;
    allocate_info.descriptorSetCount = kFramesInFlight;
    allocate_info.pSetLayouts = layouts.data();

    descriptor_sets.resize(kFramesInFlight);
    descriptor_buffer.assign(kFramesInFlight, VK_NULL_HANDLE);
    descriptor_range.assign(kFramesInFlight, 0);
    return check(vkAllocateDescriptorSets(device, &allocate_info, descriptor_sets.data()),
                 "vkAllocateDescriptorSets");
}

void VulkanRenderDevice::updateUniformDescriptor(VkBuffer buffer, VkDeviceSize range)
{
    const std::uint32_t frame = current_frame;
    if (descriptor_buffer[frame] == buffer && descriptor_range[frame] == range) {
        return;  // already points there; rewriting mid-frame would be pointless (and the last write wins)
    }

    VkDescriptorBufferInfo buffer_info = {};
    buffer_info.buffer = buffer;
    buffer_info.offset = 0;
    buffer_info.range = range;

    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_sets[frame];
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    write.pBufferInfo = &buffer_info;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    descriptor_buffer[frame] = buffer;
    descriptor_range[frame] = range;
}

std::uint32_t VulkanRenderDevice::findMemoryType(std::uint32_t type_bits,
                                                 VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memory_properties = {};
    vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

    for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
        const bool type_ok = (type_bits & (1u << i)) != 0;
        const bool properties_ok =
            (memory_properties.memoryTypes[i].propertyFlags & properties) == properties;
        if (type_ok && properties_ok) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool VulkanRenderDevice::createImage(std::uint32_t image_width, std::uint32_t image_height,
                                     VkFormat format, VkImageUsageFlags usage, VkImage &image,
                                     VkDeviceMemory &memory)
{
    VkImageCreateInfo image_info = {};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = {image_width, image_height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = format;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (!check(vkCreateImage(device, &image_info, nullptr, &image), "vkCreateImage")) {
        return false;
    }

    VkMemoryRequirements requirements = {};
    vkGetImageMemoryRequirements(device, image, &requirements);

    const std::uint32_t type_index =
        findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type_index == UINT32_MAX) {
        error = "no device-local memory type for the image";
        LOG_ERROR() << "vulkan: " << error;
        return false;
    }

    VkMemoryAllocateInfo allocate_info = {};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = type_index;
    if (!check(vkAllocateMemory(device, &allocate_info, nullptr, &memory), "vkAllocateMemory")) {
        return false;
    }
    return check(vkBindImageMemory(device, image, memory, 0), "vkBindImageMemory");
}

VkImageView VulkanRenderDevice::createImageView(VkImage image, VkFormat format,
                                                VkImageAspectFlags aspect) const
{
    VkImageViewCreateInfo view_info = {};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = format;
    view_info.subresourceRange.aspectMask = aspect;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(device, &view_info, nullptr, &view) != VK_SUCCESS) {
        LOG_ERROR() << "vulkan: vkCreateImageView failed";
    }
    return view;
}

bool VulkanRenderDevice::createRenderPass()
{
    VkAttachmentDescription color_attachment = {};
    color_attachment.format = color_format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth_attachment = {};
    depth_attachment.format = depth_format;
    depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depth_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_reference = {};
    color_reference.attachment = 0;
    color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depth_reference = {};
    depth_reference.attachment = 1;
    depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_reference;
    subpass.pDepthStencilAttachment = &depth_reference;

    // Two dependencies: the swapchain image must be available before we write colour, and the depth
    // buffer needs its own early/late fragment test stage
    VkSubpassDependency dependencies[2] = {};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].dstSubpass = 0;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                   | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                   | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].srcAccessMask = 0;
    dependencies[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[2] = {color_attachment, depth_attachment};

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 2;
    render_pass_info.pAttachments = attachments;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    render_pass_info.dependencyCount = 2;
    render_pass_info.pDependencies = dependencies;

    return check(vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass),
                 "vkCreateRenderPass");
}

bool VulkanRenderDevice::createDepthResources()
{
    // D32_SFLOAT is the usual choice; fall back to a combined depth/stencil format when unsupported
    const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                   VK_FORMAT_D24_UNORM_S8_UINT};
    depth_format = VK_FORMAT_UNDEFINED;
    for (const VkFormat candidate : candidates) {
        VkFormatProperties properties = {};
        vkGetPhysicalDeviceFormatProperties(physical_device, candidate, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            depth_format = candidate;
            break;
        }
    }
    if (depth_format == VK_FORMAT_UNDEFINED) {
        error = "no supported depth format";
        LOG_ERROR() << "vulkan: " << error;
        return false;
    }

    if (!createImage(extent.width, extent.height, depth_format, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                     depth_image, depth_memory)) {
        return false;
    }

    depth_view = createImageView(depth_image, depth_format, VK_IMAGE_ASPECT_DEPTH_BIT);
    return depth_view != VK_NULL_HANDLE;
}

bool VulkanRenderDevice::createFramebuffers()
{
    framebuffers.resize(swapchain_views.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < swapchain_views.size(); ++i) {
        VkImageView attachments[2] = {swapchain_views[i], depth_view};

        VkFramebufferCreateInfo framebuffer_info = {};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass;
        framebuffer_info.attachmentCount = 2;
        framebuffer_info.pAttachments = attachments;
        framebuffer_info.width = extent.width;
        framebuffer_info.height = extent.height;
        framebuffer_info.layers = 1;

        if (!check(vkCreateFramebuffer(device, &framebuffer_info, nullptr, &framebuffers[i]),
                   "vkCreateFramebuffer")) {
            return false;
        }
    }
    return true;
}

bool VulkanRenderDevice::createSwapchainObjects(std::uint32_t swapchain_width,
                                                std::uint32_t swapchain_height)
{
    VkSurfaceCapabilitiesKHR capabilities = {};
    if (!check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &capabilities),
               "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) {
        return false;
    }

    std::uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, formats.data());

    // Prefer a plain UNORM format: the OpenGL backend renders into an UNORM default framebuffer, so
    // the two backends produce comparable pixels (no implicit sRGB conversion on one side only)
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
    for (const VkSurfaceFormatKHR &candidate : formats) {
        if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM) {
            chosen = candidate;
            break;
        }
    }
    color_format = chosen.format;

    extent.width = capabilities.currentExtent.width == UINT32_MAX
                       ? std::clamp(swapchain_width, capabilities.minImageExtent.width,
                                    capabilities.maxImageExtent.width)
                       : capabilities.currentExtent.width;
    extent.height = capabilities.currentExtent.height == UINT32_MAX
                        ? std::clamp(swapchain_height, capabilities.minImageExtent.height,
                                     capabilities.maxImageExtent.height)
                        : capabilities.currentExtent.height;

    std::uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount) {
        image_count = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {};
    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain_info.surface = surface;
    swapchain_info.minImageCount = image_count;
    swapchain_info.imageFormat = color_format;
    swapchain_info.imageColorSpace = chosen.colorSpace;
    swapchain_info.imageExtent = extent;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchain_info.preTransform = capabilities.currentTransform;
    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // vsync on: FIFO is the only mode guaranteed to exist, and it is what the GL backend does too
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchain_info.clipped = VK_TRUE;
    swapchain_info.oldSwapchain = swapchain;

    VkSwapchainKHR new_swapchain = VK_NULL_HANDLE;
    if (!check(vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &new_swapchain),
               "vkCreateSwapchainKHR")) {
        return false;
    }
    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    }
    swapchain = new_swapchain;

    std::uint32_t actual_images = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &actual_images, nullptr);
    swapchain_images.resize(actual_images);
    vkGetSwapchainImagesKHR(device, swapchain, &actual_images, swapchain_images.data());

    swapchain_views.resize(swapchain_images.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < swapchain_images.size(); ++i) {
        swapchain_views[i] = createImageView(swapchain_images[i], color_format, VK_IMAGE_ASPECT_COLOR_BIT);
        if (swapchain_views[i] == VK_NULL_HANDLE) {
            return false;
        }
    }

    // The render pass is created once, from the first swapchain format; pipelines reference it
    if (render_pass == VK_NULL_HANDLE) {
        if (!createDepthResources()) {
            return false;
        }
        if (!createRenderPass()) {
            return false;
        }
    } else {
        // Recreate only the depth image: its size follows the swapchain
        if (depth_view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, depth_view, nullptr);
            depth_view = VK_NULL_HANDLE;
        }
        if (depth_image != VK_NULL_HANDLE) {
            vkDestroyImage(device, depth_image, nullptr);
            depth_image = VK_NULL_HANDLE;
        }
        if (depth_memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, depth_memory, nullptr);
            depth_memory = VK_NULL_HANDLE;
        }
        if (!createDepthResources()) {
            return false;
        }
    }

    if (!createFramebuffers()) {
        return false;
    }

    width = extent.width;
    height = extent.height;
    swapchain_ready = true;
    swapchain_out_of_date = false;

    LOG_INFO() << "vulkan: swapchain " << extent.width << "x" << extent.height << ", "
               << swapchain_images.size() << " images, format " << static_cast<int>(color_format);
    return true;
}

void VulkanRenderDevice::destroySwapchainObjects()
{
    for (VkFramebuffer &framebuffer : framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
            framebuffer = VK_NULL_HANDLE;
        }
    }
    framebuffers.clear();

    if (depth_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, depth_view, nullptr);
        depth_view = VK_NULL_HANDLE;
    }
    if (depth_image != VK_NULL_HANDLE) {
        vkDestroyImage(device, depth_image, nullptr);
        depth_image = VK_NULL_HANDLE;
    }
    if (depth_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, depth_memory, nullptr);
        depth_memory = VK_NULL_HANDLE;
    }

    for (VkImageView &view : swapchain_views) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
            view = VK_NULL_HANDLE;
        }
    }
    swapchain_views.clear();
    swapchain_images.clear();

    if (swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    swapchain_ready = false;
}

bool VulkanRenderDevice::recreateSwapchain()
{
    if (device == VK_NULL_HANDLE || surface == VK_NULL_HANDLE) {
        return false;
    }

    // Nothing may be in flight while the swapchain resources are replaced
    vkDeviceWaitIdle(device);
    destroySwapchainObjects();
    return createSwapchainObjects(width, height);
}

bool VulkanRenderDevice::createSwapchain(const SwapchainDesc &desc)
{
    error = nullptr;

    if (device == VK_NULL_HANDLE) {
        error = "create() must run before createSwapchain()";
        return false;
    }
    if (!desc.window.isValid()) {
        error = "no native window handle";
        return false;
    }

    vsync = desc.vsync;
    width = desc.width;
    height = desc.height;
    return createSwapchainObjects(width, height);
}

void VulkanRenderDevice::destroySwapchain()
{
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        destroySwapchainObjects();
    }
}

bool VulkanRenderDevice::resizeSwapchain(std::uint32_t new_width, std::uint32_t new_height)
{
    if (new_width == 0 || new_height == 0) {
        return false;
    }
    width = new_width;
    height = new_height;
    return recreateSwapchain();
}

// ---------------- resources ----------------

VulkanRenderDevice::BufferSlot *VulkanRenderDevice::bufferSlot(BufferHandle handle)
{
    const std::size_t index = static_cast<std::size_t>(handle);
    if (handle == kInvalidHandle || index > buffers.size()) {
        return nullptr;
    }
    BufferSlot &slot = buffers[index - 1];
    return slot.buffer == VK_NULL_HANDLE ? nullptr : &slot;
}

VulkanRenderDevice::ShaderSlot *VulkanRenderDevice::shaderSlot(ShaderHandle handle)
{
    const std::size_t index = static_cast<std::size_t>(handle);
    if (handle == kInvalidHandle || index > shaders.size()) {
        return nullptr;
    }
    ShaderSlot &slot = shaders[index - 1];
    return slot.module == VK_NULL_HANDLE ? nullptr : &slot;
}

VulkanRenderDevice::PipelineSlot *VulkanRenderDevice::pipelineSlot(PipelineHandle handle)
{
    const std::size_t index = static_cast<std::size_t>(handle);
    if (handle == kInvalidHandle || index > pipelines.size()) {
        return nullptr;
    }
    PipelineSlot &slot = pipelines[index - 1];
    return slot.pipeline == VK_NULL_HANDLE ? nullptr : &slot;
}

BufferHandle VulkanRenderDevice::createBuffer(const BufferDesc &desc)
{
    if (desc.size == 0) {
        error = "createBuffer: empty buffer";
        return kInvalidHandle;
    }

    BufferSlot slot;
    slot.size = desc.size;
    slot.usage = desc.usage;

    VkBufferCreateInfo buffer_info = {};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = desc.size;
    buffer_info.usage = bufferUsageFlags(desc.usage);
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (!check(vkCreateBuffer(device, &buffer_info, nullptr, &slot.buffer), "vkCreateBuffer")) {
        return kInvalidHandle;
    }

    VkMemoryRequirements requirements = {};
    vkGetBufferMemoryRequirements(device, slot.buffer, &requirements);

    // One memory strategy for every buffer: host visible and coherent, kept mapped. It is not what a
    // shipping renderer does (device-local + staging), but it keeps this backend honest and small.
    const std::uint32_t type_index = findMemoryType(
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (type_index == UINT32_MAX) {
        error = "no host-visible memory type for the buffer";
        LOG_ERROR() << "vulkan: " << error;
        vkDestroyBuffer(device, slot.buffer, nullptr);
        return kInvalidHandle;
    }

    VkMemoryAllocateInfo allocate_info = {};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = type_index;
    if (!check(vkAllocateMemory(device, &allocate_info, nullptr, &slot.memory), "vkAllocateMemory")) {
        vkDestroyBuffer(device, slot.buffer, nullptr);
        return kInvalidHandle;
    }
    if (!check(vkBindBufferMemory(device, slot.buffer, slot.memory, 0), "vkBindBufferMemory")) {
        vkDestroyBuffer(device, slot.buffer, nullptr);
        vkFreeMemory(device, slot.memory, nullptr);
        return kInvalidHandle;
    }
    if (!check(vkMapMemory(device, slot.memory, 0, desc.size, 0, &slot.mapped), "vkMapMemory")) {
        vkDestroyBuffer(device, slot.buffer, nullptr);
        vkFreeMemory(device, slot.memory, nullptr);
        return kInvalidHandle;
    }

    if (desc.initial_data != nullptr) {
        std::memcpy(slot.mapped, desc.initial_data, desc.size);
    }

    for (std::size_t i = 0; i < buffers.size(); ++i) {
        if (buffers[i].buffer == VK_NULL_HANDLE) {
            buffers[i] = slot;
            return static_cast<BufferHandle>(i + 1);
        }
    }

    buffers.push_back(slot);
    return static_cast<BufferHandle>(buffers.size());
}

void VulkanRenderDevice::destroyBuffer(BufferHandle buffer)
{
    BufferSlot *slot = bufferSlot(buffer);
    if (slot == nullptr) {
        return;
    }
    if (slot->mapped != nullptr) {
        vkUnmapMemory(device, slot->memory);
    }
    vkDestroyBuffer(device, slot->buffer, nullptr);
    vkFreeMemory(device, slot->memory, nullptr);
    *slot = BufferSlot{};

    // A descriptor set may still point at it; drop the cache so the next bind rewrites it
    std::fill(descriptor_buffer.begin(), descriptor_buffer.end(), VK_NULL_HANDLE);
    std::fill(descriptor_range.begin(), descriptor_range.end(), 0);
}

void VulkanRenderDevice::updateBuffer(BufferHandle buffer, const void *data, std::uint32_t size,
                                      std::uint32_t offset)
{
    BufferSlot *slot = bufferSlot(buffer);
    if (slot == nullptr || data == nullptr || size == 0) {
        error = "updateBuffer: bad arguments";
        return;
    }
    if (static_cast<VkDeviceSize>(offset) + size > slot->size) {
        error = "updateBuffer: out of range";
        LOG_ERROR() << "vulkan: updateBuffer out of range (" << offset << " + " << size << " > "
                    << slot->size << ")";
        return;
    }

    // Coherent memory: a plain memcpy is enough (no flush). The caller must not write into a region a
    // frame in flight is still reading - which is why constants are laid out per frame in flight.
    std::memcpy(static_cast<std::uint8_t *>(slot->mapped) + offset, data, size);
}

ShaderHandle VulkanRenderDevice::createShader(const ShaderDesc &desc)
{
    if (desc.code == nullptr || desc.size == 0 || (desc.size % 4) != 0) {
        error = "createShader: SPIR-V size must be a non-zero multiple of 4";
        LOG_ERROR() << "vulkan: " << error << " (" << desc.debug_name << ")";
        return kInvalidHandle;
    }

    const std::size_t word_count = desc.size / 4;
    std::vector<std::uint32_t> code(word_count);
    std::memcpy(code.data(), desc.code, desc.size);

    VkShaderModuleCreateInfo module_info = {};
    module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module_info.codeSize = desc.size;
    module_info.pCode = code.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (!check(vkCreateShaderModule(device, &module_info, nullptr, &module), "vkCreateShaderModule")) {
        LOG_ERROR() << "vulkan: shader module failed: " << (desc.debug_name ? desc.debug_name : "?");
        return kInvalidHandle;
    }

    ShaderSlot slot;
    slot.module = module;
    slot.stage = desc.stage;
    shaders.push_back(slot);
    return static_cast<ShaderHandle>(shaders.size());
}

void VulkanRenderDevice::destroyShader(ShaderHandle shader)
{
    ShaderSlot *slot = shaderSlot(shader);
    if (slot == nullptr) {
        return;
    }
    vkDestroyShaderModule(device, slot->module, nullptr);
    *slot = ShaderSlot{};
}

PipelineHandle VulkanRenderDevice::createPipeline(const PipelineDesc &desc)
{
    ShaderSlot *vertex_shader = shaderSlot(desc.vertex_shader);
    ShaderSlot *fragment_shader = shaderSlot(desc.fragment_shader);
    if (vertex_shader == nullptr || fragment_shader == nullptr) {
        error = "createPipeline: shader handles are invalid";
        return kInvalidHandle;
    }
    if (desc.attributes == nullptr || desc.attribute_count == 0 || desc.vertex_stride == 0) {
        error = "createPipeline: a vertex layout is required";
        return kInvalidHandle;
    }
    if (render_pass == VK_NULL_HANDLE) {
        error = "createPipeline: create the swapchain first (the render pass is derived from it)";
        return kInvalidHandle;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_shader->module;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_shader->module;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = desc.vertex_stride;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributes(desc.attribute_count);
    for (std::uint32_t i = 0; i < desc.attribute_count; ++i) {
        attributes[i].location = desc.attributes[i].location;
        attributes[i].binding = 0;
        attributes[i].format = vertexAttributeFormat(desc.attributes[i].format);
        attributes[i].offset = desc.attributes[i].offset;
    }

    VkPipelineVertexInputStateCreateInfo vertex_input = {};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    vertex_input.vertexAttributeDescriptionCount = desc.attribute_count;
    vertex_input.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_state = {};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterization = {};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;  // same as the GL backend: no culling yet
    rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterization.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {};
    depth_stencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable = desc.depth_test ? VK_TRUE : VK_FALSE;
    depth_stencil.depthWriteEnable = desc.depth_write ? VK_TRUE : VK_FALSE;
    depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;  // GL's default when depth test is enabled

    VkPipelineColorBlendAttachmentState blend_attachment = {};
    blend_attachment.blendEnable = VK_FALSE;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                      | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend = {};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &blend_attachment;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = 2;
    dynamic_state.pDynamicStates = dynamic_states;

    VkPipelineLayoutCreateInfo layout_info = {};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = 1;
    layout_info.pSetLayouts = &descriptor_set_layout;

    PipelineSlot slot;
    slot.attributes.assign(desc.attributes, desc.attributes + desc.attribute_count);
    slot.vertex_stride = desc.vertex_stride;

    if (!check(vkCreatePipelineLayout(device, &layout_info, nullptr, &slot.layout),
               "vkCreatePipelineLayout")) {
        return kInvalidHandle;
    }

    VkGraphicsPipelineCreateInfo pipeline_info = {};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterization;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pDepthStencilState = &depth_stencil;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = slot.layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;

    if (!check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                                        &slot.pipeline),
               "vkCreateGraphicsPipelines")) {
        vkDestroyPipelineLayout(device, slot.layout, nullptr);
        return kInvalidHandle;
    }

    pipelines.push_back(slot);
    return static_cast<PipelineHandle>(pipelines.size());
}

void VulkanRenderDevice::destroyPipeline(PipelineHandle pipeline)
{
    PipelineSlot *slot = pipelineSlot(pipeline);
    if (slot == nullptr) {
        return;
    }
    vkDestroyPipeline(device, slot->pipeline, nullptr);
    vkDestroyPipelineLayout(device, slot->layout, nullptr);
    if (bound_pipeline == slot) {
        bound_pipeline = nullptr;
    }
    *slot = PipelineSlot{};
}

// ---------------- frames ----------------

ICommandList &VulkanRenderDevice::getCommandList()
{
    return *command_list;
}

void VulkanRenderDevice::beginFrame()
{
    frame_active = false;
    if (!swapchain_ready || device == VK_NULL_HANDLE) {
        return;
    }

    if (swapchain_out_of_date) {
        if (!recreateSwapchain()) {
            return;
        }
    }

    vkWaitForFences(device, 1, &in_flight_fences[current_frame], VK_TRUE, UINT64_MAX);

    const VkResult acquire = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                                  image_available[current_frame], VK_NULL_HANDLE,
                                                  &image_index);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_out_of_date = true;
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        check(acquire, "vkAcquireNextImageKHR");
        return;
    }

    vkResetFences(device, 1, &in_flight_fences[current_frame]);
    check(vkResetCommandBuffer(currentCommandBuffer(), 0), "vkResetCommandBuffer");

    command_list->begin();
    frame_active = true;
}

void VulkanRenderDevice::endFrame()
{
    if (!frame_active) {
        return;
    }

    command_list->end();

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    const VkSemaphore wait_semaphores[] = {image_available[current_frame]};
    const VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = wait_semaphores;
    submit_info.pWaitDstStageMask = wait_stages;
    submit_info.commandBufferCount = 1;
    const VkCommandBuffer buffers[] = {currentCommandBuffer()};
    submit_info.pCommandBuffers = buffers;
    const VkSemaphore signal_semaphores[] = {render_finished[current_frame]};
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = signal_semaphores;

    if (!check(vkQueueSubmit(queue, 1, &submit_info, in_flight_fences[current_frame]),
               "vkQueueSubmit")) {
        frame_active = false;
        return;
    }

    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = signal_semaphores;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain;
    present_info.pImageIndices = &image_index;

    const VkResult present = vkQueuePresentKHR(queue, &present_info);
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR) {
        swapchain_out_of_date = true;  // recreated at the start of the next frame
    } else if (present != VK_SUCCESS) {
        check(present, "vkQueuePresentKHR");
    }

    current_frame = (current_frame + 1) % kFramesInFlight;
    frame_active = false;
}

void VulkanRenderDevice::waitIdle()
{
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }
}

void VulkanRenderDevice::destroy()
{
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }

    command_list.reset();

    for (BufferSlot &buffer : buffers) {
        if (buffer.mapped != nullptr) {
            vkUnmapMemory(device, buffer.memory);
        }
        if (buffer.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer.buffer, nullptr);
        }
        if (buffer.memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, buffer.memory, nullptr);
        }
    }
    buffers.clear();

    for (ShaderSlot &shader : shaders) {
        if (shader.module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, shader.module, nullptr);
        }
    }
    shaders.clear();

    for (PipelineSlot &pipeline : pipelines) {
        if (pipeline.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline.pipeline, nullptr);
        }
        if (pipeline.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pipeline.layout, nullptr);
        }
    }
    pipelines.clear();
    bound_pipeline = nullptr;

    if (device != VK_NULL_HANDLE) {
        destroySwapchainObjects();

        if (render_pass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, render_pass, nullptr);
            render_pass = VK_NULL_HANDLE;
        }
        for (VkSemaphore semaphore : image_available) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, semaphore, nullptr);
            }
        }
        for (VkSemaphore semaphore : render_finished) {
            if (semaphore != VK_NULL_HANDLE) {
                vkDestroySemaphore(device, semaphore, nullptr);
            }
        }
        for (VkFence fence : in_flight_fences) {
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(device, fence, nullptr);
            }
        }
        image_available.clear();
        render_finished.clear();
        in_flight_fences.clear();

        if (descriptor_pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
            descriptor_pool = VK_NULL_HANDLE;
        }
        if (descriptor_set_layout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
            descriptor_set_layout = VK_NULL_HANDLE;
        }
        descriptor_sets.clear();
        descriptor_buffer.clear();
        descriptor_range.clear();

        if (command_pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, command_pool, nullptr);
            command_pool = VK_NULL_HANDLE;
        }
        command_buffers.clear();

        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }

    swapchain_ready = false;
    frame_active = false;
    backend_name.clear();
}
